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

#ifndef _XCP_NG_VDI_STREAM_GLOBAL_H_
#define _XCP_NG_VDI_STREAM_GLOBAL_H_

#include <limits.h>
#include <stdint.h>

#include <xcp-ng/generic/math.h>

// =============================================================================

// Fixed size: 512-byte sectors.
#define N_BITS_PER_SECTOR 9u
#define SECTOR_SIZE (1u << N_BITS_PER_SECTOR)

#define N_SECTORS_MAX_PER_REQUEST (XCP_MIN((uint64_t)INT_MAX, (uint64_t)SIZE_MAX) >> N_BITS_PER_SECTOR)

#define SECTOR_ROUND_UP(SIZE) ((SIZE + (SECTOR_SIZE - 1)) & ~(SECTOR_SIZE - 1))

#define SIZE_TO_SECTOR_COUNT(SIZE) SECTOR_ROUND_UP(SIZE) >> N_BITS_PER_SECTOR

void *aligned_block_alloc (size_t size);

#endif // ifndef _XCP_NG_VDI_STREAM_GLOBAL_H_
