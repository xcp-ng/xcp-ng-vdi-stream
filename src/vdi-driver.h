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

#ifndef _XCP_NG_VDI_STREAM_DRIVER_H_
#define _XCP_NG_VDI_STREAM_DRIVER_H_

#include <sys/types.h>

// =============================================================================

typedef struct XcpVdiStream XcpVdiStream;

typedef struct XcpVdiDriver {
  const char *name;
  size_t streamDataSize;

  int (*open)(XcpVdiStream *stream);
  int (*close)(XcpVdiStream *stream);
  void (*dumpInfo)(const XcpVdiStream *stream, int fd);
  ssize_t (*read)(XcpVdiStream *stream);
} XcpVdiDriver;

// -----------------------------------------------------------------------------

const XcpVdiDriver *xcp_vdi_driver_find (const char *format);

// -----------------------------------------------------------------------------

void __xcp_vdi_driver_register_internal (const XcpVdiDriver *driver);

#define xcp_vdi_driver_register(VDI_DRIVER) \
  static void __attribute__((constructor)) __xcp_vdi_driver_register ## function () { \
    __xcp_vdi_driver_register_internal(&(VDI_DRIVER)); \
  }

#endif // ifndef _XCP_NG_VDI_STREAM_DRIVER_H_
