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

#ifndef _XCP_NG_VDI_STREAM_H_
#define _XCP_NG_VDI_STREAM_H_

#include <sys/types.h>

// =============================================================================

#ifdef __cplusplus
extern "C" {
#endif // ifdef __cplusplus

typedef struct XcpVdiStream XcpVdiStream;

XcpVdiStream *xcp_vdi_stream_new ();
void xcp_vdi_stream_destroy (XcpVdiStream *stream);

int xcp_vdi_stream_open (XcpVdiStream *stream, const char *format, const char *filename, const char *base);
int xcp_vdi_stream_close (XcpVdiStream *stream);

const char *xcp_vdi_stream_get_error_string (const XcpVdiStream *stream);

const char *xcp_vdi_stream_get_format (const XcpVdiStream *stream);

void xcp_vdi_stream_dump_info (const XcpVdiStream *stream, int fd);

ssize_t xcp_vdi_stream_read (XcpVdiStream *stream, const void **buf);

#ifdef __cplusplus
}
#endif // ifdef __cplusplus

#endif // ifndef _XCP_NG_VDI_STREAM_H_
