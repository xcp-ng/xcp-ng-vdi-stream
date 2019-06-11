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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xcp-ng/vdi-stream.h"

// =============================================================================

int main (int argc, char *argv[]) {
  if (argc < 4) {
    fprintf(stderr, "Usage: %s <output> <format> <vdi> [base]\n", *argv);
    return EXIT_FAILURE;
  }

  int ret = EXIT_SUCCESS;
  FILE *output = NULL;
  XcpVdiStream *stream = xcp_vdi_stream_new();
  if (!stream) {
    fprintf(stderr, "Unable to alloc stream.\n");
    goto fail;
  }

  if (xcp_vdi_stream_open(stream, argv[2], argv[3], argc >= 5 ? argv[4] : NULL) < 0) {
    fprintf(stderr, "Unable to open stream because: `%s`.\n", xcp_vdi_stream_get_error_string(stream));
    goto fail;
  }

  if (!(output = fopen(argv[1], "wb"))) {
    fprintf(stderr, "Unable to open `%s` because: `%s`.\n", argv[1], strerror(errno));
    goto fail;
  }

  for (;;) {
    const void *buf;
    const ssize_t ret = xcp_vdi_stream_read(stream, &buf);
    if (ret < 0) {
      fprintf(stderr, "Error during stream: `%s`.\n", xcp_vdi_stream_get_error_string(stream));
      goto fail;
    }
    if (ret == 0)
      break; // Terminated. \o/

    if (fwrite(buf, (size_t)ret, 1, output) != 1) {
      fprintf(stderr, "Failed to write stream to file.\n");
      goto fail;
    }
  }

  goto success;

fail:
  ret = EXIT_FAILURE;

success:
  xcp_vdi_stream_destroy(stream);
  if (output)
    fclose(output);

  return ret;
}
