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

// See: https://caml.inria.fr/pub/docs/manual-ocaml/intfc.html

// Can be built like this:
// ocamlc xcp_ng_vdi_stream.mli
// ocamlc -custom xcp_ng_vdi_stream.ml xcp_ng_vdi_stream_stubs.c -cclib ../../build/libxcp-ng-vdi-stream.so -I ../../include -a -o xcp_ng_vdi_stream.cma

#include <caml/alloc.h>
#include <caml/bigarray.h>
#include <caml/custom.h>
#include <caml/fail.h>
#include <caml/memory.h>

#include "xcp-ng/vdi-stream.h"

// =============================================================================

#define XcpVdiStream_val(value) (*((XcpVdiStream **)Data_custom_val(value)))

// -----------------------------------------------------------------------------

static void finalize_xcp_vdi_stream (value stream) {
  xcp_vdi_stream_destroy(XcpVdiStream_val(stream));
}

CAMLprim value stub_xcp_vdi_stream_new () {
  CAMLparam0();
  CAMLlocal1(result);

  XcpVdiStream *stream = xcp_vdi_stream_new();
  if (!stream)
    caml_failwith("Failed to create vdi stream");

  static struct custom_operations customOps = {
    .identifier = "XcpVdiStream",
    .finalize = finalize_xcp_vdi_stream,
    .compare = custom_compare_default,
    .hash = custom_hash_default,
    .serialize = custom_serialize_default,
    .deserialize = custom_deserialize_default
  };

  if ((result = caml_alloc_custom(&customOps, sizeof stream, 0, 1)))
    XcpVdiStream_val(result) = stream;
  CAMLreturn(result);
}

// -----------------------------------------------------------------------------

CAMLprim value stub_xcp_vdi_stream_open (value stream, value format, value filename, value base) {
  CAMLparam4(stream, format, filename, base);
  CAMLreturn(Val_int(xcp_vdi_stream_open(
    XcpVdiStream_val(stream),
    String_val(format),
    String_val(filename),
    String_val(base)
  )));
}

CAMLprim value stub_xcp_vdi_stream_close (value stream) {
  CAMLparam1(stream);
  CAMLreturn(Val_int(xcp_vdi_stream_close(XcpVdiStream_val(stream))));
}

// -----------------------------------------------------------------------------

CAMLprim value stub_xcp_vdi_stream_get_error_string (value stream) {
  CAMLparam1(stream);
  CAMLreturn(caml_copy_string(xcp_vdi_stream_get_error_string(XcpVdiStream_val(stream))));
}

// -----------------------------------------------------------------------------

CAMLprim value stub_xcp_vdi_stream_get_format (value stream) {
  CAMLparam1(stream);
  CAMLreturn(caml_copy_string(xcp_vdi_stream_get_format(XcpVdiStream_val(stream))));
}

// -----------------------------------------------------------------------------

void stub_xcp_vdi_stream_dump_info (value stream, value fd) {
  CAMLparam2(stream, fd);
  xcp_vdi_stream_dump_info(XcpVdiStream_val(stream), Int_val(fd));
  CAMLreturn0;
}

// -----------------------------------------------------------------------------

CAMLprim value stub_xcp_vdi_stream_read (value stream) {
  CAMLparam1(stream);
  CAMLlocal1(result);

  void *buf;
  ssize_t ret = xcp_vdi_stream_read(XcpVdiStream_val(stream), (const void **)&buf);

  result = caml_alloc_small(2, 0);
  Field(result, 0) = Val_int(ret);

  if (ret < 0)
    ret = 0;
  Field(result, 1) = caml_ba_alloc(CAML_BA_UINT8 | CAML_BA_C_LAYOUT, 1, buf, &ret);

  CAMLreturn(result);
}
