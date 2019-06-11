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

#ifndef _XCP_NG_VDI_STREAM_P_H_
#define _XCP_NG_VDI_STREAM_P_H_

#include <stdint.h>

#include "xcp-ng/vdi-stream.h"

#include "error.h"

// =============================================================================

// Stream blocks of 2MiB.
#define XCP_VDI_STREAM_CHUNK_SIZE (1u << 21)

// -----------------------------------------------------------------------------

typedef struct XcpStreamBuf XcpStreamBuf;
typedef struct XcpVdiDriver XcpVdiDriver;

struct XcpVdiStream {
  // Fields below this point can be freely used in streams.
  const XcpVdiDriver *driver;
  void *streamData;
  char *errorString;

  char *filename;
  char *base;

  // Internal opaque stream buf. It can't be used directly in streams.
  // xcp_vdi_stream_co_* functions must be called to update its state.
  XcpStreamBuf *streamBuf;
};

// -----------------------------------------------------------------------------

#define xcp_vdi_stream_set_error_string(STREAM, FMT, ...) set_error(&(STREAM)->errorString, FMT, ##__VA_ARGS__)

int xcp_vdi_stream_co_write (XcpVdiStream *stream, const void *buf, size_t count);
int xcp_vdi_stream_co_write_zeros (XcpVdiStream *stream, size_t count);

int xcp_vdi_stream_co_flush (XcpVdiStream *stream);

void *xcp_vdi_stream_get_buf (XcpVdiStream *stream);

size_t xcp_vdi_stream_get_buf_size (const XcpVdiStream *stream);

int xcp_vdi_stream_increase_size (XcpVdiStream *stream, size_t count);

uint64_t xcp_vdi_stream_get_current_offset (const XcpVdiStream *stream);

#endif // ifndef _XCP_NG_VDI_STREAM_P_H_
