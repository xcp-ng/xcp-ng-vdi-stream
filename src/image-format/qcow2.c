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
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <xcp-ng/generic/endian.h>
#include <xcp-ng/generic/file.h>
#include <xcp-ng/generic/io.h>
#include <xcp-ng/generic/path.h>

#include "error.h"
#include "global.h"
#include "image-format/qcow2.h"

// =============================================================================

#define QCOW2_L2_CACHE_SIZE 2097152 // 2MiB (Minimum size must be equal to QCOW2_MAX_CLUSTER_BITS = 21).

static int qcow2_l2_cache_init (QCow2Image *image, char **error) {
  // Some static checks to ensure a valid comportment.
  static_assert(QCOW2_L2_CACHE_SIZE >= (1 << QCOW2_MAX_CLUSTER_BITS), "");
  static_assert(QCOW2_MIN_CLUSTER_BITS == 9, "");
  static_assert(QCOW2_MAX_CLUSTER_BITS == 21, "");

  static const uint32_t bitToPrimeNumber[] = {
    4099, 2053, 1031, 521, 257, 131, 67, 37, 17, 11, 5, 3, 1
  };

  QCow2L2Cache *cache = &image->l2Cache;
  cache->size = 0;
  cache->capacity = bitToPrimeNumber[image->header.clusterBits - QCOW2_MIN_CLUSTER_BITS];
  TAILQ_INIT(&cache->sortedEntries);
  if (!(cache->entries = malloc(cache->capacity * sizeof *cache->entries))) {
    set_error(error, "Failed to create L2 cache (%s)", strerror(errno));
    return -1;
  }
  for (size_t i = 0; i < cache->capacity; ++i)
    TAILQ_INIT(&cache->entries[i]);

  return 0;
}

static void qcow2_l2_cache_uninit (QCow2Image *image) {
  QCow2L2Cache *cache = &image->l2Cache;

  Qcow2L2CacheEntry *entry;
  while ((entry = TAILQ_FIRST(&cache->sortedEntries))) {
    TAILQ_REMOVE(&cache->sortedEntries, entry, sortedEntry);
    free(entry);
  }

  free(cache->entries);
  cache->entries = NULL;
}

static inline uint32_t qcow2_l2_cache_hash (const QCow2Image *image, uint64_t l2TableOffset) {
  // TODO: Maybe use a seed.
  return (uint32_t)((l2TableOffset >> 32) ^ (l2TableOffset & 0xFFFFFFFF)) % (uint32_t)image->l2Cache.capacity;
}

static uint64_t *qcow2_l2_cache_get_table (QCow2Image *image, uint64_t l2TableOffset, char **error) {
  const uint32_t hash = qcow2_l2_cache_hash(image, l2TableOffset);
  QCow2L2Cache *cache = &image->l2Cache;

  // 1. First case: entry already exists => update and return it.
  Qcow2L2CacheEntry *entry;
  TAILQ_FOREACH(entry, &cache->entries[hash], entry) {
    if (entry->l2TableOffset == l2TableOffset) {
      TAILQ_REMOVE(&cache->sortedEntries, entry, sortedEntry);
      TAILQ_INSERT_HEAD(&cache->sortedEntries, entry, sortedEntry);
      return entry->l2Table;
    }
  }

  const uint32_t clusterSize = image->clusterSize;
  if (cache->size < cache->capacity) {
    // 2. Entry must be created.
    if (!(entry = aligned_block_alloc(sizeof *entry + clusterSize))) {
      set_error(error, "Unable to create new L2 table cache entry (%s)", strerror(errno));
      return NULL;
    }
    ++cache->size;
  } else {
    // 3. Reuse the oldest entry.
    entry = TAILQ_LAST(&cache->sortedEntries, Qcow2L2CacheHead);
    TAILQ_REMOVE(&cache->entries[hash], entry, entry);
    TAILQ_REMOVE(&cache->sortedEntries, entry, sortedEntry);
  }

  entry->l2TableOffset = l2TableOffset;
  TAILQ_INSERT_TAIL(&cache->entries[hash], entry, entry);
  TAILQ_INSERT_HEAD(&cache->sortedEntries, entry, sortedEntry);

  const XcpError ret = xcp_fd_pread(image->fd, entry->l2Table, clusterSize, (off_t)l2TableOffset);
  if (ret == XCP_ERR_ERRNO) {
    set_error(error, "Unable to read L2 table at offset %#" PRIx64 " in %s (%s)", l2TableOffset, image->filename, strerror(errno));
    return NULL;
  }
  if ((size_t)ret != clusterSize) {
    set_error(error, "Truncated L2 table at offset %#" PRIx64 " in %s", l2TableOffset, image->filename);
    return NULL;
  }

  return entry->l2Table;
}

// =============================================================================

void qcow2_header_from_be (QCow2Header *header) {
  XCP_C_WARN_PUSH
  XCP_C_WARN_DISABLE_ADDRESS_OF_PACKED_MEMBER

  xcp_from_be_u32_p(&header->magic);
  xcp_from_be_u32_p(&header->version);
  xcp_from_be_u64_p(&header->backingFileOffset);
  xcp_from_be_u32_p(&header->backingFileSize);
  xcp_from_be_u32_p(&header->clusterBits);
  xcp_from_be_u64_p(&header->size);
  xcp_from_be_u32_p(&header->cryptMethod);
  xcp_from_be_u32_p(&header->l1Size);
  xcp_from_be_u64_p(&header->l1TableOffset);
  xcp_from_be_u64_p(&header->refcountTableOffset);
  xcp_from_be_u32_p(&header->refcountTableClusters);
  xcp_from_be_u32_p(&header->nbSnapshots);
  xcp_from_be_u64_p(&header->snapshotsOffset);

  if (header->version == 3) {
    xcp_from_be_u64_p(&header->incompatibleFeatures);
    xcp_from_be_u64_p(&header->compatibleFeatures);
    xcp_from_be_u64_p(&header->autoclearFeatures);
    xcp_from_be_u32_p(&header->refcountOrder);
    xcp_from_be_u32_p(&header->headerLength);
  }

  XCP_C_WARN_POP
}

void qcow2_header_to_be (QCow2Header *header) {
  const uint32_t version = header->version;

  XCP_C_WARN_PUSH
  XCP_C_WARN_DISABLE_ADDRESS_OF_PACKED_MEMBER

  xcp_to_be_u32_p(&header->magic);
  xcp_to_be_u32_p(&header->version);
  xcp_to_be_u64_p(&header->backingFileOffset);
  xcp_to_be_u32_p(&header->backingFileSize);
  xcp_to_be_u32_p(&header->clusterBits);
  xcp_to_be_u64_p(&header->size);
  xcp_to_be_u32_p(&header->cryptMethod);
  xcp_to_be_u32_p(&header->l1Size);
  xcp_to_be_u64_p(&header->l1TableOffset);
  xcp_to_be_u64_p(&header->refcountTableOffset);
  xcp_to_be_u32_p(&header->refcountTableClusters);
  xcp_to_be_u32_p(&header->nbSnapshots);
  xcp_to_be_u64_p(&header->snapshotsOffset);

  if (version == 3) {
    xcp_to_be_u64_p(&header->incompatibleFeatures);
    xcp_to_be_u64_p(&header->compatibleFeatures);
    xcp_to_be_u64_p(&header->autoclearFeatures);
    xcp_to_be_u32_p(&header->refcountOrder);
    xcp_to_be_u32_p(&header->headerLength);
  }

  XCP_C_WARN_POP
}

// =============================================================================

static int qcow2_image_close_basic (QCow2Image *image, char **error) {
  XCP_UNUSED(error);
  if (image->fd < 0)
    return 0;

  free(image->filename);
  free(image->l1Table);

  qcow2_l2_cache_uninit(image);

  xcp_fd_close(image->fd);
  image->fd = -1;

  return 0;
}

static int qcow2_image_open_basic (QCow2Image *image, const char *filename, char **error) {
  QCow2Header *header = &image->header;

  image->parent = NULL;
  if ((image->fd = open(filename, O_RDONLY)) < 0) {
    set_error(error, "%s", strerror(errno));
    return -1;
  }

  // Reset some fields to avoid crash if qcow2_image_close is called.
  {
    *image->backingFile = '\0';
    image->l1Table = NULL;

    QCow2L2Cache *cache = &image->l2Cache;
    cache->entries = NULL;
    TAILQ_INIT(&cache->sortedEntries);
  }

  if (!(image->filename = strdup(filename))) {
    set_error(error, "Failed to copy filename");
    goto fail;
  }

  // 1. Read QCOW2 header.
  {
    const XcpError ret = xcp_fd_pread(image->fd, header, sizeof *header, 0);
    if (ret == XCP_ERR_ERRNO) {
      set_error(error, "Failed to read QCOW2 header (%s)", strerror(errno));
      goto fail;
    }
    if ((size_t)ret != sizeof *header) {
      set_error(error, "Truncated QCOW2 header");
      goto fail;
    }
  }

  qcow2_header_from_be(header);

  // 2. Check and parse QCOW2 header.
  if (header->magic != QCOW2_MAGIC_NUMBER) {
    set_error(error, "Not a QCOW2 image");
    goto fail;
  }
  if (header->version != 2 && header->version != 3) {
    set_error(error, "Invalid QCOW2 version '%d'", header->version);
    goto fail;
  }

  if (header->clusterBits < QCOW2_MIN_CLUSTER_BITS || header->clusterBits > QCOW2_MAX_CLUSTER_BITS) {
    set_error(error, "Invalid cluster bits '%d'", header->clusterBits);
    goto fail;
  }
  image->clusterSize = 1u << header->clusterBits;
  image->nbSectors = SIZE_TO_SECTOR_COUNT(header->size);
  image->nbSectorsPerCluster = 1u << (header->clusterBits - N_BITS_PER_SECTOR);

  if (header->version == 3) {
    if (header->incompatibleFeatures & (QCOW2_INCOMPATIBLE_FEATURE_DIRTY | QCOW2_INCOMPATIBLE_FEATURE_CORRUPT)) {
      set_error(error, "Image is dirty or corrupted");
      goto fail;
    }

    // Ignore compatible features.

    if (header->refcountOrder > QCOW2_MAX_REFCOUNT_ORDER) {
      set_error(error, "Reference count order reached its limit");
      goto fail;
    }

    if (header->headerLength < sizeof *header) {
      set_error(error, "Header is too short");
      goto fail;
    }
  } else {
    // Default values of the spec file.
    header->incompatibleFeatures = 0;
    header->compatibleFeatures = 0;
    header->autoclearFeatures = 0;
    header->refcountOrder = 4;
    header->headerLength = 72;
  }

  // The header size must be lower or equal to cluster size.
  // So the header must be entirely in the first cluster!
  if (header->headerLength > image->clusterSize) {
    set_error(error, "Header is so big");
    goto fail;
  }

  // The backing filename is stored in the space between the end of the header extensions
  // and the beginning of the next cluster.
  if (header->backingFileOffset && header->backingFileOffset + header->backingFileSize > image->clusterSize) {
    set_error(error, "Invalid backing file offset/size");
    goto fail;
  }

  if (!header->refcountTableClusters) {
    set_error(error, "No refcount table");
    goto fail;
  }

  // The size of one refcount table entry is equal to 64 bits (i.e. (1 << 3) == 8 bytes).
  image->refcountTableSize = header->refcountTableClusters << (header->clusterBits - 3);

  image->refcountBits = 1u << header->refcountOrder;

  // Or more simply: image->refcountBlockSize = image->clusterSize * 8 / image->refcountBits
  image->refcountBlockBits = header->clusterBits - (header->refcountOrder - 3);
  image->refcountBlockSize = 1u << image->refcountBlockBits;

  // The size of one L2 table entry is equal to 64 bits (i.e. (1 << 3) == 8 bytes)
  // and a L2 table occupies one cluster.
  image->l2Bits = header->clusterBits - 3;
  image->l2Size = 1u << image->l2Bits;

  // 3. Read backing filename.
  if (header->backingFileOffset) {
    const uint32_t size = header->backingFileSize;
    if (size >= XCP_MIN(image->clusterSize - header->backingFileOffset, sizeof image->backingFile)) {
      set_error(error, "Backing filename is so big");
      goto fail;
    }
    if (xcp_fd_pread(image->fd, image->backingFile, size, (off_t)header->backingFileOffset) < 0) {
      set_error(error, "Failed to read backing filename (%s)", strerror(errno));
      goto fail;
    }
    image->backingFile[size] = '\0';
  } else
    *image->backingFile = '\0';

  // 4. Init L2 cache.
  if (qcow2_l2_cache_init(image, error) < 0)
    goto fail;

  // 5. Read L1 table.
  {
    const uint32_t l1Size = header->l1Size;
    const uint32_t minL1Size = qcow2_image_l1_entry_count_from_size(image, header->size);
    if (minL1Size > INT_MAX) {
      set_error(error, "Image is so big");
      goto fail;
    }

    if (l1Size < minL1Size) {
      set_error(error, "L1 table is so small");
      goto fail;
    }

    if (!l1Size)
      return 0;

    const size_t expectedBytes = l1Size * sizeof *image->l1Table;
    if (!(image->l1Table = aligned_block_alloc(SECTOR_ROUND_UP(expectedBytes)))) {
      set_error(error, "Failed to alloc l1 table (%s)", strerror(errno));
      goto fail;
    }

    const XcpError ret = xcp_fd_pread(image->fd, image->l1Table, expectedBytes, (off_t)image->header.l1TableOffset);
    if (ret == XCP_ERR_ERRNO) {
      set_error(error, "Failed to read L1 table (%s)", strerror(errno));
      goto fail;
    }
    if ((size_t)ret != expectedBytes) {
      set_error(error, "Truncated L1 table");
      goto fail;
    }

    for (uint32_t i = 0; i < l1Size; ++i)
      xcp_from_be_u64_p(&image->l1Table[i]);
  }

  // TODO: Supports features. (Or maybe log error avoid usage of incompatible features.)
  // TODO: Supports crypto.
  // TODO: Use QCOW2_INCOMPATIBLE_FEATURE_EXT_FILE.
  // TODO: Check the validity of the reference count table and L1 table. (Ignore snapshots.)

  return 0;

fail:
  qcow2_image_close_basic(image, NULL);
  return -1;
}

static int qcow2_image_open_rec (QCow2Image *child, char **error) {
  if (!*child->backingFile)
    return 0;

  char *dir = xcp_path_parent_dir(child->filename);
  if (!dir) {
    set_error(error, "Unable to compute parent dir of `%s`\n", child->filename);
    return -1;
  }

  // 1. Compute absolute parent path.
  char *combinedParentPath = xcp_path_combine(dir, child->backingFile);
  free(dir);
  char absParentPath[PATH_MAX];
  if (!combinedParentPath || !realpath(combinedParentPath, absParentPath)) {
    free(combinedParentPath);
    set_error(error, "Unable to get abs parent path of `%s` (%s)\n", child->backingFile, strerror(errno));
    return -1;
  }
  free(combinedParentPath);

  // 2. Open parent image.
  QCow2Image *parent = malloc(sizeof *parent);
  if (!parent) {
    set_error(error, "Unable to alloc parent image");
    return -1;
  }

  if (qcow2_image_open_basic(parent, absParentPath, error) < 0) {
    set_error(error, "Failed to open parent image `%s`: `%s`", absParentPath, *error);
    free(parent);
    return -1;
  }
  child->parent = parent;

  // 3. Open parent of parent image...
  return qcow2_image_open_rec(parent, error);
}

int qcow2_image_open (QCow2Image *image, const char *filename, char **error) {
  char absoluteFilename[PATH_MAX];
  if (!realpath(filename, absoluteFilename)) {
    set_error(error, "Unable to get abs path of image `%s` (%s)", filename, strerror(errno));
    return -1;
  }

  if (qcow2_image_open_basic(image, absoluteFilename, error) < 0) {
    set_error(error, "Failed to open image `%s`: `%s`", absoluteFilename, *error);
    return -1;
  }

  if (qcow2_image_open_rec(image, error) < 0) {
    qcow2_image_close(image, NULL);
    return -1;
  }
  return 0;
}

int qcow2_image_close (QCow2Image *image, char **error) {
  XCP_UNUSED(error);

  qcow2_image_close_basic(image, NULL);
  for (image = image->parent; image; ) {
    QCow2Image *cur = image;
    qcow2_image_close_basic(cur, NULL);
    image = image->parent;
    free(cur);
  }

  return 0;
}

// -----------------------------------------------------------------------------

static uint32_t qcow2_compute_contiguous_cluster_count (
  const QCow2Image *image, const uint64_t *l2Slice, uint32_t maxClusterCount
) {
  const uint64_t l2Entry = xcp_from_be_u64(l2Slice[0]);
  const uint32_t typeMask = qcow2_get_cluster_type_mask(l2Entry);

  assert(!(typeMask & ClusterTypeCompressed));
  assert(
    ((typeMask & ClusterTypeAllocated) && !(typeMask & ClusterTypeUnallocated)) ||
    ((typeMask & ClusterTypeUnallocated) && !(typeMask & ClusterTypeAllocated))
  );

  // First case:
  // Read all contiguous unallocated clusters of type:
  // { ClusterTypeUnallocated } XOR { ClusterTypeUnallocated, ClusterTypeZero }.
  if (typeMask & ClusterTypeUnallocated) {
    uint32_t i = 0;
    for (i = 0; i < maxClusterCount; ++i)
      if (qcow2_get_cluster_type_mask(xcp_from_be_u64(l2Slice[i])) != typeMask)
        break;
    return i;
  }

  // Second case:
  // Read all contiguous allocated clusters of type:
  // { ClusterTypeAllocated } XOR { ClusterTypeAllocated, ClusterTypeZero }.
  const uint64_t mask =
    QCOW2_L2_ENTRY_FLAG_ZERO | QCOW2_L2_ENTRY_FLAG_COMPRESSED | QCOW2_L2_ENTRY_HOST_CLUSTER_OFFSET_MASK;
  uint64_t clustersOffset = l2Entry & mask;
  assert(clustersOffset);

  uint32_t i;
  for (i = 0; i < maxClusterCount; ++i) {
    if (clustersOffset != (xcp_from_be_u64(l2Slice[i]) & mask))
      break;
    clustersOffset += image->clusterSize;
  }
  return i;
}

uint64_t qcow2_image_find_clusters_offset (
  const QCow2Image *image, uint64_t vaddr, size_t nBytes, size_t *nAvailableBytes, uint32_t *typeMask, char **error
) {
  uint64_t clustersOffset = 0;

  // 1. Cumpute available bytes at this vaddr.
  const uint32_t clusterPadding = qcow2_image_offset_to_cluster_padding(image, vaddr);
  nBytes += clusterPadding;

  const uint32_t l2Index = qcow2_image_vaddr_to_l2_index(image, vaddr);
  *nAvailableBytes = (uint64_t)(image->l2Size - l2Index) << image->header.clusterBits;
  if (nBytes > *nAvailableBytes)
    nBytes = *nAvailableBytes;

  // 2. Compute L2 slice.
  uint64_t l2TableOffset;
  uint64_t *l2Table;

  const uint32_t l1Index = qcow2_image_vaddr_to_l1_index(image, vaddr);
  if (l1Index >= image->header.l1Size) {
    *typeMask = ClusterTypeUnallocated;
    goto end;
  }

  if (!(l2TableOffset = image->l1Table[l1Index] & QCOW2_L1_ENTRY_L2_TABLE_OFFSET_MASK)) {
    *typeMask = ClusterTypeUnallocated;
    goto end;
  }

  // Check if L2 table is correctly aligned on a cluster.
  if (qcow2_image_offset_to_cluster_padding(image, l2TableOffset)) {
    set_error(error, "Unaligned L2 table at L1 index %" PRIu32 ": %#" PRIx64, l1Index, l2TableOffset);
    return (uint64_t)-1;
  }
  if (!(l2Table = qcow2_l2_cache_get_table((QCow2Image *)image, l2TableOffset, error)))
    return (uint64_t)-1;

  // 3. Compute clusters offset.
  {
    const uint64_t l2Entry = xcp_from_be_u64(l2Table[l2Index]);
    *typeMask = qcow2_get_cluster_type_mask(l2Entry);
    clustersOffset = l2Entry & QCOW2_L2_ENTRY_HOST_CLUSTER_OFFSET_MASK;
  }

  // 4. Count contiguous cluster of the same type and update available bytes.
  if (*typeMask & ClusterTypeCompressed) {
    set_error(error, "Unsupported compressed cluster");
    return (uint64_t)-1;
  }

  if (*typeMask & ClusterTypeAllocated) {
    // Check if cluster is correctly aligned.
    if (qcow2_image_offset_to_cluster_padding(image, clustersOffset)) {
      set_error(
        error, "Unaligned cluster at (L1 Index: %" PRIu32 ", L2 index: %" PRIu32 ", L2 table offset: %#" PRIx64 ", %#" PRIx64 ")",
        l1Index, l2Index, l2TableOffset, clustersOffset
      );
      return (uint64_t)-1;
    }
  } else if (!(*typeMask & ClusterTypeUnallocated)) {
    // Missing cluster type?
    abort();
  }

  {
    uint32_t clusterCount = qcow2_image_cluster_count_from_size(image, nBytes);
    clusterCount = qcow2_compute_contiguous_cluster_count(image, &l2Table[l2Index], clusterCount);
    *nAvailableBytes = clusterCount << image->header.clusterBits;
  }

end:
  assert(!qcow2_image_offset_to_cluster_padding(image, *nAvailableBytes));

  if (nBytes < *nAvailableBytes)
    *nAvailableBytes = nBytes;
  assert(*nAvailableBytes >= clusterPadding);

  *nAvailableBytes -= clusterPadding;

  return clustersOffset;
}

// -----------------------------------------------------------------------------

ssize_t qcow2_image_read (const QCow2Image *image, uint64_t vaddr, size_t nBytes, void *buf, char **error) {
  const uint64_t startVaddr = vaddr;
  while (nBytes) {
    // 1. Find contiguous clusters at vaddr.
    size_t nAvailableBytes;
    uint32_t typeMask;
    const uint64_t clustersOffset =
      qcow2_image_find_clusters_offset(image, vaddr, nBytes, &nAvailableBytes, &typeMask, error);
    if (clustersOffset == (uint64_t)-1)
      return -1;

    // TODO: Support it one day. Cannot be reached because qcow2_find_clusters_offset returns -1
    // if the cluster is compressed.
    assert(!(typeMask & ClusterTypeCompressed));

    // 2. Read.
    uint64_t readOffset = 0;
    ssize_t ret;

    if ((typeMask & ClusterTypeZero) || ((typeMask & ClusterTypeUnallocated) && !image->parent)) {
      memset(buf, 0, nAvailableBytes);
      goto next;
    }

    if (typeMask & ClusterTypeAllocated) {
      readOffset = clustersOffset + qcow2_image_offset_to_cluster_padding(image, vaddr);
      ret = xcp_fd_pread(image->fd, buf, nAvailableBytes, (off_t)readOffset);
    } else if (typeMask & ClusterTypeUnallocated)
      ret = qcow2_image_read(image->parent, vaddr, nAvailableBytes, buf, error);
    else
      abort();

    if (ret < 0) {
      set_error(
        error, "Failed to read %s block(s) at offset %#" PRIx64 " (%s)",
        typeMask & ClusterTypeAllocated ? "allocated" : "unallocated", readOffset, strerror(errno)
      );
      return -1;
    }
    if ((size_t)ret != nAvailableBytes) {
      set_error(
        error, "Truncated read (expected=%zu, current=%zu) of %s block(s) at offset %#" PRIx64,
        nAvailableBytes, ret, typeMask & ClusterTypeAllocated ? "allocated" : "unallocated", readOffset
      );
      return -1;
    }

  next:
    nBytes -= nAvailableBytes;
    *(char **)&buf += nAvailableBytes;
    vaddr += nAvailableBytes;
  }

  return (ssize_t)(vaddr - startVaddr);
}

// =============================================================================

int qcow2_chain_open (QCow2Chain *chain, const char *filename, const char *base, char **error) {
  // 1. Open image.
  QCow2Image *image = &chain->image;
  if (qcow2_image_open(image, filename, error) < 0)
    return -1;

  // 2. Find base.
  if (!base) {
    chain->base = NULL;
    return 0;
  }

  char absBase[PATH_MAX];
  if (!realpath(base, absBase)) {
    set_error(error, "Unable to compute absolute base path (%s)", strerror(errno));
    qcow2_chain_close(chain, NULL);
    return -1;
  }

  for (; image; image = image->parent)
    if (!strcmp(image->filename, absBase)) {
      chain->base = image;
      return 0; // Base found!
    }

  set_error(error, "Unable to find base `%s`", absBase);
  qcow2_chain_close(chain, NULL);
  return -1;
}

int qcow2_chain_close (QCow2Chain *chain, char **error) {
  chain->base = NULL;
  return qcow2_image_close(&chain->image, error);
}

// -----------------------------------------------------------------------------

static inline size_t qcow2_image_get_max_bytes (const QCow2Image *image, uint64_t vaddr, size_t nBytes) {
  const uint32_t clusterPadding = qcow2_image_offset_to_cluster_padding(image, vaddr);
  nBytes += clusterPadding;

  const uint32_t l2Index = qcow2_image_vaddr_to_l2_index(image, vaddr);
  size_t nAvailableBytes = (uint64_t)(image->l2Size - l2Index) << image->header.clusterBits;
  if (nAvailableBytes > nBytes)
    nAvailableBytes = nBytes;

  nAvailableBytes -= clusterPadding;
  return nAvailableBytes;
}

uint64_t qcow2_chain_find_clusters_offset (
  const QCow2Chain *chain,
  uint64_t vaddr,
  size_t nBytes,
  size_t *nAvailableBytes,
  uint32_t *typeMask,
  const QCow2Image **image,
  char **error
) {
  // Special case when image base is itself.
  // N available unallocated aligned fake bytes are computed.
  if (&chain->image == chain->base) {
    *nAvailableBytes = qcow2_image_get_max_bytes(&chain->image, vaddr, nBytes);
    *typeMask = ClusterTypeUnallocated;
    *image = &chain->image;
    return 0;
  }

  uint64_t clustersOffset = 0;
  for (const QCow2Image *it = &chain->image; it && it != chain->base; it = it->parent) {
    *image = it;
    clustersOffset = qcow2_image_find_clusters_offset(*image, vaddr, nBytes, nAvailableBytes, typeMask, error);
    if (clustersOffset == (uint64_t)-1)
      break;

    if (*typeMask & (ClusterTypeAllocated | ClusterTypeZero))
      break;

    nBytes = XCP_MIN(nBytes, *nAvailableBytes);
  }

  assert(*image);
  return clustersOffset;
}

// -----------------------------------------------------------------------------

int qcow2_chain_foreach_clusters (const QCow2Chain *chain, Qcow2ForeachCb cb, void *userData, char **error) {
  const uint64_t nbSectors = chain->image.nbSectors;
  for (uint64_t sector = 0; sector < nbSectors; ) {
    const uint64_t vaddr = sector << N_BITS_PER_SECTOR;
    const size_t nBytes = XCP_MIN((nbSectors - sector), N_SECTORS_MAX_PER_REQUEST) << N_BITS_PER_SECTOR;

    size_t nAvailableBytes;
    uint32_t typeMask;

    const QCow2Image *image;
    const uint64_t clustersOffset =
      qcow2_chain_find_clusters_offset(chain, vaddr, nBytes, &nAvailableBytes, &typeMask, &image, error);
    if (clustersOffset == (uint64_t)-1)
      return -1;

    if ((*cb)(sector, nAvailableBytes, typeMask, image, clustersOffset, userData, error) < 0)
      return -1;

    // If nAvailableBytes is not aligned, there is a big problem in qcow2_chain_find_clusters_offset...
    assert(!(nAvailableBytes & (SECTOR_SIZE - 1)));
    sector += nAvailableBytes >> N_BITS_PER_SECTOR;
  }

  return 0;
}
