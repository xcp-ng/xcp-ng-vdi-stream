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

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include <xcp-ng/generic/math.h>

#include "global.h"

// =============================================================================

void *aligned_block_alloc (size_t size) {
  const size_t align = XCP_MAX((size_t)getpagesize(), 4096u);
  if (!size) size = align;

  void *ptr;
  int ret = posix_memalign(&ptr, align, size);
  if (ret) {
    errno = ret;
    return NULL;
  }
  return ptr;
}
