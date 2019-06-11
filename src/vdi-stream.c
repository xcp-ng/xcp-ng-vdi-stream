/*
 * xcp-ng-vdi-stream
 * Copyright (C) 2019  Vates SAS - ronan.abhamon@vates.fr
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <xcp-ng/generic/coroutine.h>

#include "global.h"
#include "vdi-driver.h"
#include "vdi-stream-p.h"

// =============================================================================

struct XcpStreamBuf {
  void *buf;     // Stream buffer.
  size_t size;   // Current buffer byte count. Must be lower than XCP_VDI_STREAM_CHUNK_SIZE.
  ssize_t coRet; // Returned value to the user.

  uint64_t offset; // Current offset position (i.e. quantity of total data written).

  XcpCoroutine *coroutine; // Coroutine to stream buffer.
};

// -----------------------------------------------------------------------------

static void reset_stream_data (XcpVdiStream *stream) {
  stream->driver = NULL;

  free(stream->streamData);
  stream->streamData = NULL;

  free(stream->filename);
  stream->filename = NULL;

  free(stream->base);
  stream->base = NULL;

  if (stream->streamBuf) {
    free(stream->streamBuf->buf);
    free(stream->streamBuf);
    stream->streamBuf = NULL;
  }
}

// -----------------------------------------------------------------------------

XcpVdiStream *xcp_vdi_stream_new () {
  return calloc(sizeof(XcpVdiStream), 1);
}

void xcp_vdi_stream_destroy (XcpVdiStream *stream) {
  if (stream) {
    xcp_vdi_stream_close(stream);
    free(stream->errorString);
    free(stream);
  }
}

int xcp_vdi_stream_open (XcpVdiStream *stream, const char *format, const char *filename, const char *base) {
  xcp_vdi_stream_close(stream);

  const XcpVdiDriver *driver = xcp_vdi_driver_find(format);
  if (!driver) {
    xcp_vdi_stream_set_error_string(stream, "Unknown `%s` format", format);
    return -1;
  }
  stream->driver = driver;

  if (!(stream->streamData = malloc(driver->streamDataSize))) {
    xcp_vdi_stream_set_error_string(
      stream, "Unable to alloc driver data (size=%zu) for `%s` format", driver->streamDataSize, format
    );
    return -1;
  }

  if (!(stream->filename = strdup(filename)) || (base && !(stream->base = strdup(base)))) {
    xcp_vdi_stream_set_error_string(stream, "Unable to copy base and/or filename (%s)", strerror(errno));
    reset_stream_data(stream);
    return -1;
  }

  const int ret = (*stream->driver->open)(stream);
  if (ret < 0)
    reset_stream_data(stream);
  return ret;
}

int xcp_vdi_stream_close (XcpVdiStream *stream) {
  int ret = 0;
  if (stream->driver) {
    // Exit coroutine properly.
    // If the last coRet value is positive (i.e. data exists), we force it to -1.
    // After that the coroutine is resumed to kill itself.
    XcpStreamBuf *streamBuf = stream->streamBuf;
    if (streamBuf && streamBuf->coRet > 0) {
      streamBuf->coRet = -1;
      xcp_coroutine_resume(streamBuf->coroutine);
    }

    // Close and reset data.
    ret = (*stream->driver->close)(stream);
    reset_stream_data(stream);
  }
  return ret;
}

const char *xcp_vdi_stream_get_error_string (const XcpVdiStream *stream) {
  return stream->errorString;
}

const char *xcp_vdi_stream_get_format (const XcpVdiStream *stream) {
  return stream->driver ? stream->driver->name : NULL;
}

void xcp_vdi_stream_dump_info (const XcpVdiStream *stream, int fd) {
  if (stream->driver)
    (*stream->driver->dumpInfo)(stream, fd);
}

static void xcp_vdi_stream_co_read_wrapper (void *userData) {
  XcpVdiStream *stream = (XcpVdiStream *)userData;
  stream->streamBuf->coRet = (*stream->driver->read)(stream);
}

ssize_t xcp_vdi_stream_read (XcpVdiStream *stream, const void **buf) {
  // 1. Create buf and coroutine if necessary.
  if (!stream->streamBuf) {
    if (!stream->driver) {
      xcp_vdi_stream_set_error_string(stream, "Driver not loaded");
      return -1;
    }

    if (!(stream->streamBuf = calloc(1, sizeof *stream->streamBuf))) {
      xcp_vdi_stream_set_error_string(stream, "Failed to create XcpStreamBuf (%s)", strerror(errno));
      return -1;
    }

    XcpStreamBuf *streamBuf = stream->streamBuf;
    if (!(streamBuf->buf = aligned_block_alloc(XCP_VDI_STREAM_CHUNK_SIZE)))
      xcp_vdi_stream_set_error_string(stream, "Failed to allocate buffer of XcpStreamBuf (%s)", strerror(errno));
    else if (!(streamBuf->coroutine = xcp_coroutine_create(xcp_vdi_stream_co_read_wrapper, stream)))
      xcp_vdi_stream_set_error_string(stream, "Failed to create stream coroutine (%s)", strerror(errno));
    else
      goto resume;

    free(streamBuf->buf);
    free(streamBuf);
    stream->streamBuf = NULL;
    return -1;
  } else {
    // Do not continue if the last coRet is an error or EOF.
    const ssize_t coRet = stream->streamBuf->coRet;
    if (coRet <= 0)
      return coRet;
  }

resume:
  {
    XcpStreamBuf *streamBuf = stream->streamBuf;
    xcp_coroutine_resume(streamBuf->coroutine);
    *buf = streamBuf->buf;
    return streamBuf->coRet;
  }
}

// -----------------------------------------------------------------------------

int xcp_vdi_stream_co_write (XcpVdiStream *stream, const void *buf, size_t count) {
  XcpStreamBuf *streamBuf = stream->streamBuf;
  assert(streamBuf->size <= XCP_VDI_STREAM_CHUNK_SIZE);

  while (count) {
    const size_t nBytes = XCP_MIN(XCP_VDI_STREAM_CHUNK_SIZE - streamBuf->size, count);
    memcpy((char *)streamBuf->buf + streamBuf->size, buf, count);
    streamBuf->size += nBytes;
    streamBuf->offset += nBytes;

    if (streamBuf->size == XCP_VDI_STREAM_CHUNK_SIZE) {
      const int ret = xcp_vdi_stream_co_flush(stream);
      if (ret < 0)
        return ret;
    }

    count -= nBytes;
  }

  return 0;
}

int xcp_vdi_stream_co_write_zeros (XcpVdiStream *stream, size_t count) {
  XcpStreamBuf *streamBuf = stream->streamBuf;
  assert(streamBuf->size <= XCP_VDI_STREAM_CHUNK_SIZE);

  while (count) {
    const size_t nBytes = XCP_MIN(XCP_VDI_STREAM_CHUNK_SIZE - streamBuf->size, count);
    memset((char *)streamBuf->buf + streamBuf->size, 0, nBytes);
    streamBuf->size += nBytes;
    streamBuf->offset += nBytes;

    if (streamBuf->size == XCP_VDI_STREAM_CHUNK_SIZE) {
      const int ret = xcp_vdi_stream_co_flush(stream);
      if (ret < 0)
        return ret;
    }

    count -= nBytes;
  }

  return 0;
}

int xcp_vdi_stream_co_flush (XcpVdiStream *stream) {
  XcpStreamBuf *streamBuf = stream->streamBuf;
  if (!streamBuf->size)
    return 0;

  streamBuf->coRet = (ssize_t)streamBuf->size;
  xcp_coroutine_yield();

  // The coRet can be set to -1 on coroutine destruction.
  if (streamBuf->coRet < 0)
    return -1;

  streamBuf->size = 0;
  streamBuf->coRet = 0;

  return 0;
}

void *xcp_vdi_stream_get_buf (XcpVdiStream *stream) {
  return stream->streamBuf->buf;
}

size_t xcp_vdi_stream_get_buf_size (const XcpVdiStream *stream) {
  return stream->streamBuf->size;
}

int xcp_vdi_stream_increase_size (XcpVdiStream *stream, size_t count) {
  if (!count)
    return 0;

  XcpStreamBuf *streamBuf = stream->streamBuf;
  const size_t newSize = streamBuf->size + count;
  assert(newSize <= XCP_VDI_STREAM_CHUNK_SIZE);

  streamBuf->size = newSize;
  streamBuf->offset += count;

  if (newSize == XCP_VDI_STREAM_CHUNK_SIZE)
    return xcp_vdi_stream_co_flush(stream);
  return 0;
}

uint64_t xcp_vdi_stream_get_current_offset (const XcpVdiStream *stream) {
  return stream->streamBuf->offset;
}
