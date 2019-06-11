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

#ifndef _XCP_NG_VDI_STREAM_QCOW2_H_
#define _XCP_NG_VDI_STREAM_QCOW2_H_

#include <stdint.h>
#include <sys/queue.h>

#include <xcp-ng/generic/global.h>

// =============================================================================
// See: https://github.com/qemu/qemu/blob/523a2a42c3abd65b503610b2a18cd7fc74c6c61e/docs/interop/qcow2.txt
// And: https://people.gnome.org/~markmc/qcow-image-format.html
// And: https://blogs.igalia.com/berto/2017/02/08/qemu-and-the-qcow2-metadata-checks/
// And: https://people.igalia.com/berto/files/kvm-forum-2017-slides.pdf
// =============================================================================

#define QCOW2_MAGIC_NUMBER (('Q' << 24) | ('F' << 16) | ('I' << 8) | 0xFB)

// Like said in the spec file, qemu as of today has an implementation limit of 2 MB (1 << 21).
// At minimum one cluster is equal to a 512-byte sector.
#define QCOW2_MIN_CLUSTER_BITS 9
#define QCOW2_MAX_CLUSTER_BITS 21

#define QCOW2_MAX_REFCOUNT_ORDER 6

#define QCOW2_INCOMPATIBLE_FEATURE_DIRTY (1 << 0)
#define QCOW2_INCOMPATIBLE_FEATURE_CORRUPT (1 << 1)
#define QCOW2_INCOMPATIBLE_FEATURE_EXT_FILE (1 << 2)

#define QCOW2_MAX_L1_SIZE (1ULL << 22)

#define QCOW2_L1_ENTRY_FLAG_COPIED (1ULL << 63)

// Bits 9-55 of the offset into the image file at which the L2 table starts.
#define QCOW2_L1_ENTRY_L2_TABLE_OFFSET_MASK 0x00FFFFFFFFFFFE00ULL

#define QCOW2_L2_ENTRY_FLAG_ZERO (1ULL << 0)
#define QCOW2_L2_ENTRY_FLAG_COMPRESSED (1ULL << 62)
#define QCOW2_L2_ENTRY_FLAG_COPIED (1ULL << 63)

// Bits 9-55 of host cluster offset.
#define QCOW2_L2_ENTRY_HOST_CLUSTER_OFFSET_MASK 0x00FFFFFFFFFFFE00ULL

// -----------------------------------------------------------------------------

typedef struct {
  // Fields of the version 2 and 3.
  uint32_t magic;                 //   0-3: Must be equal to "QFI\xfb"
  uint32_t version;               //   4-7: Expected 2 or 3.

  uint64_t backingFileOffset;     //  8-15: Offset of the backing filename in the image.
  uint32_t backingFileSize;       // 16-19: Backing filename size.

  uint32_t clusterBits;           // 20-23: Bit number for addressing an offset within a cluster.
  uint64_t size;                  // 24-31: Virtual disk size in bytes.

  uint32_t cryptMethod;           // 32-35: 0 no encryption, 1 for AES, 2 for LUKS.

  uint32_t l1Size;                // 36-39: Entry number in active L1 table.
  uint64_t l1TableOffset;         // 40-47: L1 table offset.
  uint64_t refcountTableOffset;   // 48-55: Table offset.
  uint32_t refcountTableClusters; // 56-59: Number of clusters that the refcount table occupies.

  uint32_t nbSnapshots;           // 60-63: Snapshots number in this image.
  uint64_t snapshotsOffset;       // 64-71: Offset of the snapshots table.

  // Specific fields of the version 3.
  uint64_t incompatibleFeatures;  // 72-79: Bitmask, see spec file.
  uint64_t compatibleFeatures;    // 80-87: Other bitmask.
  uint64_t autoclearFeatures;     // 88-95: And yet another one.

  uint32_t refcountOrder;         // 96-99: Width of a reference count block entry.
  uint32_t headerLength;          // 100-103: Length of the header structure in bytes.
} XCP_PACKED QCow2Header;

typedef struct {
  uint32_t type; // 0-3: Header extension type.
  uint32_t len;  // 4-7: Length of the header extension data.
} XCP_PACKED QCow2Extension;

typedef enum {
  ClusterTypeAllocated = 1,
  ClusterTypeUnallocated = 2,
  ClusterTypeZero = 4,
  ClusterTypeCompressed = 8
} ClusterType;

// =============================================================================

typedef struct Qcow2L2CacheEntry {
  TAILQ_ENTRY(Qcow2L2CacheEntry) entry;
  TAILQ_ENTRY(Qcow2L2CacheEntry) sortedEntry;

  uint64_t l2TableOffset; // Key.
  uint64_t l2Table[];     // Value.
} Qcow2L2CacheEntry;

typedef struct {
  // Hash table of entries.
  TAILQ_HEAD(, Qcow2L2CacheEntry) *entries;

  // List of all entries sorted by  the most recently accessed.
  TAILQ_HEAD(Qcow2L2CacheHead, Qcow2L2CacheEntry) sortedEntries;

  size_t size;
  size_t capacity;
} QCow2L2Cache;

typedef struct {
  int fd; // Descriptor of the current image.
  char *filename;

  QCow2Header header;

  uint32_t clusterSize;         // Size of one cluster in bytes.

  uint64_t nbSectors;           // Total number of sectors.
  uint32_t nbSectorsPerCluster; // Number of sectors per cluster.

  uint64_t refcountTableSize;   // Number of entries in the refcount table.

  uint32_t refcountBits;        // Width in bits of a reference count block entry.

  uint32_t refcountBlockBits;   // Number of bits to address a refcount block entry.
  uint32_t refcountBlockSize;   // Number of refcount block entries.

  uint32_t l2Bits;              // Number of bits to address a L2 table entry.
  uint32_t l2Size;              // Number of L2 entries in one table.

  QCow2L2Cache l2Cache;         // LRU cache to L2 tables, a great boost to avoid disk access!

  uint64_t *l1Table;            // All L1 entries.

  char backingFile[1024];
} QCow2Image;

// =============================================================================

void qcow2_header_from_be (QCow2Header *header);
void qcow2_header_to_be (QCow2Header *header);

// -----------------------------------------------------------------------------

// Compute the cluster count required to store N L1 entries with M cluster bits.
XCP_DECL_UNUSED static inline uint32_t qcow2_cluster_count_from_l1_size (uint32_t l1Size, uint32_t clusterBits) {
  const uint32_t shift = clusterBits - 3;
  return (l1Size + ((1u << shift) - 1)) >> shift;
}

// -----------------------------------------------------------------------------

int qcow2_image_open (QCow2Image *image, const char *filename, char **error);
int qcow2_image_close (QCow2Image *image, char **error);

// -----------------------------------------------------------------------------

// Compute the minimum number of L1 entries to address N bytes.
XCP_DECL_UNUSED static inline uint32_t qcow2_image_l1_entry_count_from_size (const QCow2Image *image, uint64_t size) {
  const uint32_t shift = image->header.clusterBits + image->l2Bits;
  return (uint32_t)((size + (1ULL << shift) - 1) >> shift);
}

// Compute the required number of clusters to supports N data bytes.
XCP_DECL_UNUSED static inline uint32_t qcow2_image_cluster_count_from_size (
  const QCow2Image *image, uint64_t size
) {
  return (uint32_t)((size + (image->clusterSize - 1u)) >> image->header.clusterBits);
}

// Compute the padding in a cluster given an offset (image address).
XCP_DECL_UNUSED static inline uint32_t qcow2_image_offset_to_cluster_padding (
  const QCow2Image *image, uint64_t offset
) {
  // Because cluster size if a power of two, we can avoid modulo and instead using mask.
  return (uint32_t)(offset & (image->clusterSize - 1u));
}

// Compute the L1 index of a virtual address.
XCP_DECL_UNUSED static inline uint32_t qcow2_image_vaddr_to_l1_index (const QCow2Image *image, uint64_t vaddr) {
  return (uint32_t)(vaddr >> (image->l2Bits + image->header.clusterBits));
}

// Compute the L2 index of a virtual address.
XCP_DECL_UNUSED static inline uint32_t qcow2_image_vaddr_to_l2_index (const QCow2Image *image, uint64_t vaddr) {
  return (uint32_t)((vaddr >> image->header.clusterBits) & (image->l2Size - 1));
}

// -----------------------------------------------------------------------------

XCP_DECL_UNUSED static inline uint32_t qcow2_get_cluster_type_mask (uint64_t l2Entry) {
  if (l2Entry & QCOW2_L2_ENTRY_FLAG_COMPRESSED)
    return ClusterTypeCompressed;

  if (l2Entry & QCOW2_L2_ENTRY_FLAG_ZERO) {
    if (l2Entry & QCOW2_L2_ENTRY_HOST_CLUSTER_OFFSET_MASK)
      return ClusterTypeAllocated | ClusterTypeZero;
    return ClusterTypeUnallocated | ClusterTypeZero;
  }

  if (l2Entry & QCOW2_L2_ENTRY_HOST_CLUSTER_OFFSET_MASK)
    return ClusterTypeAllocated;

  return ClusterTypeUnallocated;
}

XCP_DECL_UNUSED static inline const char *qcow2_cluster_type_mask_to_string (uint32_t typeMask) {
  if (typeMask & ClusterTypeCompressed)
    return "{ Compressed }";

  if (typeMask & ClusterTypeZero) {
    if (typeMask & ClusterTypeAllocated)
      return "{ Allocated, Zero }";
    return "{ Unallocated, Zero }";
  }

  if (typeMask & ClusterTypeAllocated)
    return "{ Allocated }";

  return "{ Unallocated }";
}

// -----------------------------------------------------------------------------

// Find a sequential set of clusters (of a type mask) given an vaddr and a number of bytes to read.
// Return -1 if there is an error, otherwise return the clusters offset.
uint64_t qcow2_image_find_clusters_offset (
  const QCow2Image *image, uint64_t vaddr, size_t nBytes, size_t *nAvailableBytes, uint32_t *typeMask, char **error
);

// -----------------------------------------------------------------------------

typedef struct QCow2Chain {
  QCow2Image image;
  struct QCow2Chain *parent;
} QCow2Chain;

int qcow2_chain_open (QCow2Chain *chain, const char *filename, const char *base, char **error);
int qcow2_chain_close (QCow2Chain *chain, char **error);

XCP_DECL_UNUSED static inline bool qcow2_chain_base_is_itself (const QCow2Chain *chain) {
  return chain->parent == chain;
}

// Similar to qcow2_image_find_clusters_offset but used on a chain.
uint64_t qcow2_chain_find_clusters_offset (
  const QCow2Chain *chain,
  uint64_t vaddr,
  size_t nBytes,
  size_t *nAvailableBytes,
  uint32_t *typeMask,
  const QCow2Image **image,
  char **error
);

// Read data at vaddr.
ssize_t qcow2_chain_read (const QCow2Chain *chain, uint64_t vaddr, size_t nBytes, void *buf, char **error);

typedef int (*Qcow2ForeachCb)(
  uint64_t sector,
  uint64_t nAvailableBytes,
  uint32_t typeMask,
  const QCow2Image *image,
  uint64_t clustersOffset,
  void *userData,
  char **error
);

// Apply a callback on each contiguous clusters.
int qcow2_chain_foreach_clusters (const QCow2Chain *chain, Qcow2ForeachCb cb, void *userData, char **error);

#endif // ifndef _XCP_NG_VDI_STREAM_QCOW2_H_
