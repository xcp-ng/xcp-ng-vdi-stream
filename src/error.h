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

#ifndef _XCP_NG_VDI_STREAM_ERROR_H_
#define _XCP_NG_VDI_STREAM_ERROR_H_

#ifdef DEBUG
  #include <stdio.h>
#endif // ifdef DEBUG

// =============================================================================

void set_error (char **error, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#ifdef DEBUG
  #define debug_log(FMT, ...) fprintf(stderr, FMT "\n", ##__VA_ARGS__)
#else
  #define debug_log(...)
#endif // ifdef DEBUG

#endif // ifndef _XCP_NG_VDI_STREAM_ERROR_H_
