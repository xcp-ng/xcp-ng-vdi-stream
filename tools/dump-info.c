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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "xcp-ng/vdi-stream.h"

// =============================================================================

int main (int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <format> <vdi>\n", *argv);
    return EXIT_FAILURE;
  }

  XcpVdiStream *stream = xcp_vdi_stream_new();
  if (!stream) {
    fprintf(stderr, "Unable to alloc stream.\n");
    return EXIT_FAILURE;
  }

  if (xcp_vdi_stream_open(stream, argv[1], argv[2], NULL) < 0) {
    fprintf(stderr, "Unable to open stream because: `%s`.\n", xcp_vdi_stream_get_error_string(stream));
    xcp_vdi_stream_destroy(stream);
    return EXIT_FAILURE;
  }

  xcp_vdi_stream_dump_info(stream, STDOUT_FILENO);
  xcp_vdi_stream_destroy(stream);

  return EXIT_SUCCESS;
}
