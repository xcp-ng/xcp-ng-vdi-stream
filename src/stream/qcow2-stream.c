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
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <xcp-ng/generic/endian.h>

#include "global.h"
#include "image-format/qcow2.h"
#include "vdi-driver.h"
#include "vdi-stream-p.h"

// =============================================================================

#define QCOW2_END_OF_HEADER_EXTENSION_LENGTH 8

#define HEX_LENGTH(VAR) ((int)(2 * sizeof(VAR)))

#define qcow2_debug_log(FMT, ...) debug_log("[qcow2-stream] " FMT, ##__VA_ARGS__)

// -----------------------------------------------------------------------------

typedef struct {
  XcpVdiStream *stream;

  uint64_t currentL2TableOffset;
  uint32_t currentL1Index;
} L1TableWriteState;

static int clusters_cb_write_l1_table (
  uint64_t sector,
  uint64_t nAvailableBytes,
  uint32_t typeMask,
  const QCow2Image *image,
  uint64_t clustersOffset,
  void *userData,
  char **error
) {
  XCP_UNUSED(clustersOffset);
  XCP_UNUSED(error);
  XCP_UNUSED(image);

  L1TableWriteState *state = userData;
  XcpVdiStream *stream = state->stream;

  const uint64_t startVaddr = sector << N_BITS_PER_SECTOR;

  qcow2_debug_log(
    "Src at vaddr %#0*" PRIx64 ": %zuB of %s at offset %#0*" PRIx64 " in %s.", HEX_LENGTH(startVaddr),
    startVaddr, nAvailableBytes, qcow2_cluster_type_mask_to_string(typeMask), HEX_LENGTH(clustersOffset),
    clustersOffset, image->filename
  );

  // Compute the current l1 index using the first opened image
  // (because cluster bits can be lower/greater in the cb image param).
  const QCow2Image *rootImage = &((QCow2Chain *)stream->streamData)->image;

  const uint64_t endVaddr = startVaddr + nAvailableBytes;
  uint64_t lastL1Index;
  if (typeMask & (ClusterTypeAllocated | ClusterTypeZero))
    lastL1Index = qcow2_image_vaddr_to_l1_index(
      rootImage,
      XCP_ROUND_UP(endVaddr, 1ULL << (rootImage->header.clusterBits + rootImage->l2Bits))
    );
  else
    lastL1Index = qcow2_image_vaddr_to_l1_index(rootImage, endVaddr);
  assert(lastL1Index <= rootImage->header.l1Size);

  assert(state->currentL1Index <= lastL1Index + 1);
  if (state->currentL1Index >= lastL1Index)
    return 0; // Nothing to do: Entry already written.

  if (typeMask & (ClusterTypeAllocated | ClusterTypeZero)) {
    qcow2_debug_log(
      "Write allocated L1 entrie(s) for L2 table at %#0*" PRIx64 " (count=%" PRIu64 ").",
      HEX_LENGTH(state->currentL2TableOffset), state->currentL2TableOffset, lastL1Index - state->currentL1Index
    );

    for (; state->currentL1Index < lastL1Index; ++state->currentL1Index) {
      const uint64_t l1Entry = xcp_to_be_u64(state->currentL2TableOffset | QCOW2_L1_ENTRY_FLAG_COPIED);
      if (xcp_vdi_stream_co_write(stream, &l1Entry, sizeof l1Entry) < 0)
        return -1;

      // Create a new L2 table (i.e. a new allocated L1 entry).
      state->currentL2TableOffset += rootImage->clusterSize;
    }
  } else {
    const uint64_t l1Entry = xcp_to_be_u64(QCOW2_L1_ENTRY_FLAG_COPIED);
    for (; state->currentL1Index < lastL1Index; ++state->currentL1Index)
      if (xcp_vdi_stream_co_write(stream, &l1Entry, sizeof l1Entry) < 0)
        return -1;
  }

  return 0;
}

// -----------------------------------------------------------------------------

typedef struct {
  XcpVdiStream *stream;

  uint64_t dataOffset; // Offset to write data referenced by L2 tables entries.

  size_t accSectorCount;
  ClusterType accType;

  uint32_t currentL1Index;
  bool currentL1EntryWritten;
} L2TablesWriteState;

static int write_l2_entries (XcpVdiStream *stream, uint64_t *dataOffset, uint32_t typeMask, size_t clusterCount) {
  if (!clusterCount)
    return 0;

  const QCow2Image *image = &((QCow2Chain *)stream->streamData)->image;

  qcow2_debug_log(
    "Write L2 entries of %s for cluster at %#0*" PRIx64 ": %zuB (%zu clusters).",
    qcow2_cluster_type_mask_to_string(typeMask), HEX_LENGTH(*dataOffset),
    typeMask & (ClusterTypeUnallocated | ClusterTypeZero) ? 0 : *dataOffset,
    clusterCount << image->header.clusterBits, clusterCount
  );

  // Write Unallocated or Zeroed L2 table entry.
  if (typeMask & (ClusterTypeUnallocated | ClusterTypeZero)) {
    uint64_t l2Entry = QCOW2_L2_ENTRY_FLAG_COPIED;
    if (typeMask & ClusterTypeZero)
      l2Entry |= QCOW2_L2_ENTRY_FLAG_ZERO;
    xcp_to_be_u64_p(&l2Entry);

    for (size_t i = 0; i < clusterCount; ++i)
      if (xcp_vdi_stream_co_write(stream, &l2Entry, sizeof l2Entry) < 0)
        return -1;

    return 0;
  }

  // Write Allocated L2 table entry.
  const uint64_t clusterSize = image->clusterSize;
  for (size_t i = 0; i < clusterCount; ++i) {
    const uint64_t l2Entry = xcp_to_be_u64(*dataOffset | QCOW2_L2_ENTRY_FLAG_COPIED);
    if (xcp_vdi_stream_co_write(stream, &l2Entry, sizeof l2Entry) < 0)
      return -1;
    *dataOffset += clusterSize;
  }

  return 0;
}

static int clusters_cb_write_l2_tables (
  uint64_t sector,
  uint64_t nAvailableBytes,
  uint32_t typeMask,
  const QCow2Image *image,
  uint64_t clustersOffset,
  void *userData,
  char **error
) {
  XCP_UNUSED(clustersOffset);
  XCP_UNUSED(error);
  XCP_UNUSED(image);

  L2TablesWriteState *state = userData;
  XcpVdiStream *stream = state->stream;

  const QCow2Image *rootImage = &((QCow2Chain *)stream->streamData)->image;
  const uint32_t l1Index = qcow2_image_vaddr_to_l1_index(rootImage, sector << N_BITS_PER_SECTOR);

  assert(state->currentL1Index <= l1Index);
  if (state->currentL1Index < l1Index) {
    state->currentL1Index = l1Index;
    state->currentL1EntryWritten = false;
    state->accSectorCount = 0;
    state->accType = ClusterTypeUnallocated;
  }

  const uint32_t nbSectorsPerCluster = rootImage->nbSectorsPerCluster;

  // Write accumulated Unallocated l2 entries at L2 table start if necessary.
  if (!state->currentL1EntryWritten && (typeMask & (ClusterTypeAllocated | ClusterTypeZero))) {
    assert(state->accType == ClusterTypeUnallocated);

    if (write_l2_entries(stream, &state->dataOffset, state->accType, state->accSectorCount / nbSectorsPerCluster) < 0)
      return -1;

    state->currentL1EntryWritten = true;
    state->accSectorCount %= nbSectorsPerCluster;
  }

  // Downgrade current acc type if needed.
  state->accSectorCount += nAvailableBytes >> N_BITS_PER_SECTOR;
  if (state->accSectorCount && !(
    ((state->accType & ClusterTypeZero) && (typeMask & (ClusterTypeAllocated | ClusterTypeZero))) ||
    ((state->accType & ClusterTypeAllocated) && (typeMask & ClusterTypeAllocated)) ||
    (state->accType & ClusterTypeUnallocated)
  )) {
    if (state->accSectorCount < nbSectorsPerCluster)
      return 0; // Do not downgrade type because previous L2 entry has not been written yet.

    // Write first previous L2 entry with previous type when necessary.
    if (write_l2_entries(stream, &state->dataOffset, state->accType, 1) < 0)
      return -1;
    state->accSectorCount -= nbSectorsPerCluster;
  }

  state->accType = typeMask;

  if (!state->currentL1EntryWritten)
    return 0;

  if (write_l2_entries(stream, &state->dataOffset, state->accType, state->accSectorCount / nbSectorsPerCluster) < 0)
    return -1;
  state->accSectorCount %= nbSectorsPerCluster;
  if (!state->accSectorCount)
    state->accType = ClusterTypeUnallocated;

  return 0;
}

// -----------------------------------------------------------------------------

typedef struct {
  XcpVdiStream *stream;

  size_t accSectorCount;
  bool accWritten;
} ClustersDataWriteState;

static int write_clusters_data (XcpVdiStream *stream, uint64_t sector, uint64_t nBytes) {
  QCow2Chain *chain = stream->streamData;

  uint64_t vaddr = sector << N_BITS_PER_SECTOR;
  if (!qcow2_image_offset_to_cluster_padding(&chain->image, vaddr)) {
    qcow2_debug_log(
      "Write from vaddr %#0*" PRIx64 " to offset %#0*" PRIx64 ": %" PRIu64 "B.",
      HEX_LENGTH(vaddr), vaddr, HEX_LENGTH(vaddr),
      xcp_vdi_stream_get_current_offset(stream), nBytes
    );
  }

  while (nBytes) {
    const size_t bufSize = xcp_vdi_stream_get_buf_size(stream);
    const size_t nBytesToRead = XCP_MIN(nBytes, XCP_VDI_STREAM_CHUNK_SIZE - bufSize);
    char *dest = (char *)xcp_vdi_stream_get_buf(stream) + bufSize;

    const ssize_t ret = qcow2_image_read(&chain->image, vaddr, nBytesToRead, dest, &stream->errorString);
    if (ret < 0)
      return -1;

    assert(bufSize + (size_t)ret <= XCP_VDI_STREAM_CHUNK_SIZE);
    xcp_vdi_stream_increase_size(stream, (size_t)ret);

    vaddr += (size_t)ret;
    nBytes -= (size_t)ret;
  }

  return 0;
}

static int clusters_cb_write_data (
  uint64_t sector,
  uint64_t nAvailableBytes,
  uint32_t typeMask,
  const QCow2Image *image,
  uint64_t clustersOffset,
  void *userData,
  char **error
) {
  XCP_UNUSED(clustersOffset);
  XCP_UNUSED(error);
  XCP_UNUSED(image);

  ClustersDataWriteState *state = userData;
  XcpVdiStream *stream = state->stream;

  const uint64_t sectorCount = nAvailableBytes >> N_BITS_PER_SECTOR;
  const uint32_t nbSectorsPerCluster = ((QCow2Chain *)stream->streamData)->image.nbSectorsPerCluster;

  if (typeMask & ClusterTypeAllocated && !(typeMask & ClusterTypeZero)) {
    // Write accumulated sectors.
    if (!state->accWritten) {
      assert(sector >= state->accSectorCount);
      // Do not use xcp_vdi_stream_co_write_zeros here, we must use write_clusters_data because if the current
      // cluster size is greater than a parent cluster size, we need to copy data of the parent(s) base image in
      // this specific condition. We can observe this case with the export of tests/images/10.qcow with
      // base=tests/images/2.qcow: Cluster data of 1.qcow is merged in bigger clusters of 10.qcow.
      if (
        state->accSectorCount &&
        write_clusters_data(stream, sector - state->accSectorCount, state->accSectorCount << N_BITS_PER_SECTOR) < 0
      )
        return -1;
      state->accWritten = true;
    }

    // Write data.
    if (write_clusters_data(stream, sector, nAvailableBytes) < 0)
      return -1;

    state->accSectorCount = (state->accSectorCount + sectorCount) % nbSectorsPerCluster;
    if (!state->accSectorCount)
      state->accWritten = false;
  } else if (state->accWritten) {
    assert(state->accSectorCount);
    if (state->accSectorCount + sectorCount >= nbSectorsPerCluster) {
      // See the previous note.
      const uint64_t remaining = nbSectorsPerCluster - state->accSectorCount;
      if (write_clusters_data(stream, sector, remaining << N_BITS_PER_SECTOR) < 0)
        return -1;

      state->accSectorCount = (sectorCount - remaining) % nbSectorsPerCluster;
      state->accWritten = false;
    } else {
      // See the previous note.
      state->accSectorCount += sectorCount;
      if (write_clusters_data(stream, sector, sectorCount << N_BITS_PER_SECTOR) < 0)
        return -1;
    }
  } else
    state->accSectorCount = (state->accSectorCount + sectorCount) % nbSectorsPerCluster;

  return 0;
}

// -----------------------------------------------------------------------------

static int qcow2_stream_open (XcpVdiStream *stream) {
  return qcow2_chain_open(stream->streamData, stream->filename, stream->base, &stream->errorString);
}

static int qcow2_stream_close (XcpVdiStream *stream) {
  return qcow2_chain_close(stream->streamData, &stream->errorString);
}

// -----------------------------------------------------------------------------

static void qcow2_stream_dump_info (const XcpVdiStream *stream, int fd) {
  const QCow2Image *image = &((QCow2Chain *)stream->streamData)->image;
  const QCow2Header *header = &image->header;

  dprintf(fd, "QCOW Image Header\n");
  dprintf(fd, "version: %" PRIu32 "\n", header->version);
  dprintf(fd, "header length: %" PRIu32 "\n", header->headerLength);
  dprintf(fd, "virtual size: %" PRIu64 " bytes\n", header->size);
  dprintf(fd, "backing file: %s\n", *image->backingFile ? image->backingFile : "");
  dprintf(fd, "crypt method: %" PRIu32 "\n", header->cryptMethod);
  dprintf(fd, "cluster size: %" PRIu32 " bytes\n", image->clusterSize);
  dprintf(fd, "nb sectors per cluster: %" PRIu32 "\n", image->nbSectorsPerCluster);
  dprintf(fd, "refcount table size (max nb of entries): %" PRIu64 "\n", image->refcountTableSize);
  dprintf(fd, "refcount block size: %" PRIu32 "\n", image->refcountBlockSize);
  dprintf(fd, "l1 size (current nb of entries): %" PRIu32 "\n", header->l1Size);
  dprintf(fd, "l2 size (max nb of entries): %" PRIu32 "\n", image->l2Size);
  dprintf(fd, "nb snapshots: %" PRIu32 "\n", header->nbSnapshots);
  dprintf(fd, "incompatible features: %#" PRIx64 "\n", header->incompatibleFeatures);
  dprintf(fd, "compatible features: %#" PRIx64 "\n", header->compatibleFeatures);
  dprintf(fd, "autoclear features: %#" PRIx64 "\n", header->autoclearFeatures);
}

// -----------------------------------------------------------------------------

static int qcow2_stream_init_header (XcpVdiStream *stream, QCow2Header *header) {
  const QCow2Image *image = &((QCow2Chain *)stream->streamData)->image;
  const QCow2Header *headerSrc = &image->header;

  memset(header, 0, sizeof *header);

  header->magic = headerSrc->magic;
  header->version = headerSrc->version;

  header->refcountOrder = headerSrc->refcountOrder;

  header->headerLength = sizeof *header; // TODO: Copy data before/after header length???

  header->clusterBits = headerSrc->clusterBits;
  header->size = headerSrc->size;

  // Use the minimum l1Size.
  header->l1Size = qcow2_image_l1_entry_count_from_size(image, header->size);

  const uint64_t clusterSize = 1u << header->clusterBits;
  header->l1TableOffset = clusterSize * 2;

  header->refcountTableOffset = clusterSize;
  header->refcountTableClusters = 1;

  const char *base = stream->base;
  if (base) {
    const uint32_t backingFileSize = (uint32_t)strlen(base);
    if (backingFileSize >= sizeof image->backingFile) {
      xcp_vdi_stream_set_error_string(stream, "Backing filename size len greater than %zu", sizeof image->backingFile);
      return -1;
    }

    const uint64_t backingFileOffset = header->headerLength + QCOW2_END_OF_HEADER_EXTENSION_LENGTH;
    assert(backingFileOffset && backingFileOffset + backingFileSize <= clusterSize);

    header->backingFileOffset = backingFileOffset;
    header->backingFileSize = backingFileSize;
  }

  return 0;
}

ssize_t qcow2_stream_read (XcpVdiStream *stream) {
  const QCow2Chain *chain = stream->streamData;
  const QCow2Image *image = &chain->image;

  qcow2_debug_log("Starting stream of `%s` (base=`%s`).", image->filename, stream->base);

  // 1. Write header.
  QCow2Header header;
  if (qcow2_stream_init_header(stream, &header) < 0)
    return -1;

  qcow2_debug_log("Cluster bits: %" PRIu32 ".", header.clusterBits);
  qcow2_debug_log("L1 size: %" PRIu32 ".", header.l1Size);
  qcow2_debug_log("L1 table offset: %#" PRIx64 ".", header.l1TableOffset);
  {
    QCow2Header copy = header;
    qcow2_header_to_be(&copy);
    if (xcp_vdi_stream_co_write(stream, &copy, sizeof copy) < 0)
      return -1;
  }

  // TODO: Write extensions in the future + other data after header.

  // 2. Write backing filename.
  if (stream->base) {
    if (
      xcp_vdi_stream_co_write_zeros(stream, header.backingFileOffset - header.headerLength) < 0 ||
      xcp_vdi_stream_co_write(stream, stream->base, header.backingFileSize) < 0
    )
      return -1;
  } else if (xcp_vdi_stream_co_write_zeros(stream, QCOW2_END_OF_HEADER_EXTENSION_LENGTH) < 0)
    return -1;

  // Write bytes padding.
  {
    const size_t offset = header.headerLength + QCOW2_END_OF_HEADER_EXTENSION_LENGTH + header.backingFileSize;
    if (xcp_vdi_stream_co_write_zeros(stream, image->clusterSize - offset) < 0)
      return -1;
  }
  assert(xcp_vdi_stream_get_current_offset(stream) == image->clusterSize);

  // 3. Write refcount table.
  if (xcp_vdi_stream_co_write_zeros(stream, header.refcountTableClusters << header.clusterBits) < 0)
    return -1;
  assert(xcp_vdi_stream_get_current_offset(stream) == image->clusterSize << 1);

  // 4. Write L1 tables.
  const uint64_t l2TablesOffset =
    header.l1TableOffset + (qcow2_cluster_count_from_l1_size(header.l1Size, header.clusterBits) << header.clusterBits);
  uint64_t dataOffset;
  qcow2_debug_log("L2 tables offset: %#" PRIx64 ".", l2TablesOffset);
  {
    L1TableWriteState state = {
      .stream = stream,
      .currentL2TableOffset = l2TablesOffset,
      .currentL1Index = 0
    };
    if (qcow2_chain_foreach_clusters(chain, clusters_cb_write_l1_table, &state, &stream->errorString) < 0)
      return -1;

    const uint64_t l1Entry = xcp_to_be_u64(QCOW2_L1_ENTRY_FLAG_COPIED);
    for (; state.currentL1Index < header.l1Size; ++state.currentL1Index)
      if (xcp_vdi_stream_co_write(stream, &l1Entry, sizeof l1Entry) < 0)
        return -1;

    // Data offset is just after the contiguous L2 tables.
    dataOffset = state.currentL2TableOffset;

    if (xcp_vdi_stream_co_write_zeros(stream, l2TablesOffset - (header.l1TableOffset + header.l1Size * sizeof(uint64_t))) < 0)
      return -1;
  }
  assert(xcp_vdi_stream_get_current_offset(stream) == l2TablesOffset);

  // 5. Write L2 tables.
  const uint64_t l2EntryCount = XCP_DIV_ROUND_UP(header.size, image->clusterSize);
  const uint64_t l2TableCount = XCP_DIV_ROUND_UP(l2EntryCount, image->l2Size);
  uint64_t endOffset;
  qcow2_debug_log("L2 entry count: %" PRIu64 ".", l2EntryCount);
  qcow2_debug_log("L2 table count: %" PRIu64 ".", l2TableCount);
  qcow2_debug_log("Data offset: %#" PRIx64 ".", dataOffset);
  {
    L2TablesWriteState state = {
      .stream = stream,
      .dataOffset = dataOffset,
      .accSectorCount = 0,
      .accType = ClusterTypeUnallocated,
      .currentL1Index = 0,
      .currentL1EntryWritten = false
    };
    if (qcow2_chain_foreach_clusters(chain, clusters_cb_write_l2_tables, &state, &stream->errorString) < 0)
      return -1;
    endOffset = state.dataOffset;

    // Little trick to write last entry.
    // It exists a particular case: Do not write acc sectors if L1 entry had not been written.
    if (state.accSectorCount && state.currentL1EntryWritten) {
      assert(image->nbSectorsPerCluster > state.accSectorCount);
      clusters_cb_write_l2_tables(
        0, (image->nbSectorsPerCluster - state.accSectorCount) << N_BITS_PER_SECTOR,
        ClusterTypeUnallocated, image, 0, &state, &stream->errorString
      );
      assert(!state.accSectorCount);
    }

    // Write unused l2 tables.
    if (state.currentL1EntryWritten) {
      const uint64_t l2Entry = xcp_to_be_u64(QCOW2_L2_ENTRY_FLAG_COPIED);
      for (uint64_t i = (l2TableCount << image->l2Bits) - l2EntryCount; i; --i)
        if (xcp_vdi_stream_co_write(stream, &l2Entry, sizeof l2Entry) < 0)
          return -1;
    }
  }
  assert(xcp_vdi_stream_get_current_offset(stream) == dataOffset);

  // 6. Write data.
  {
    ClustersDataWriteState state = {
      .stream = stream,
      .accSectorCount = 0,
      .accWritten = false
    };
    if (qcow2_chain_foreach_clusters(chain, clusters_cb_write_data, &state, &stream->errorString) < 0)
      return -1;

    // Write padding bytes.
    if (state.accSectorCount)
      if (xcp_vdi_stream_co_write_zeros(stream, (image->nbSectorsPerCluster - state.accSectorCount) << N_BITS_PER_SECTOR) < 0)
        return -1;
  }
  XCP_UNUSED(endOffset); // Avoid warning when compiled in release mode.
  assert(xcp_vdi_stream_get_current_offset(stream) == endOffset);

  // Flush remaining bytes.
  return xcp_vdi_stream_co_flush(stream);
}

// =============================================================================

static XcpVdiDriver driver = {
  .name = "qcow2",
  .streamDataSize = sizeof(QCow2Chain),

  .open = qcow2_stream_open,
  .close = qcow2_stream_close,
  .dumpInfo = qcow2_stream_dump_info,
  .read = qcow2_stream_read
};
xcp_vdi_driver_register(driver);
