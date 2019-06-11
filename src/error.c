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

#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "error.h"

// =============================================================================

void set_error (char **error, const char *fmt, ...) {
  if (!error)
    return;

  char *oldError = *error;

  va_list args;
  va_start(args, fmt);
  if (vasprintf(error, fmt, args) < 0)
    *error = NULL;
  va_end(args);

  // `oldError` can be reused as param. Do not free it before this point.
  free(oldError);
}
