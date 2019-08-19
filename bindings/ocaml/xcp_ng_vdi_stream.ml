(*
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
 *)

module XcpNgVdiStream = struct
  type stream
  external create: unit -> stream = "stub_xcp_vdi_stream_new"
  external open_in: stream -> string -> string -> string -> int = "stub_xcp_vdi_stream_open"
  external close: stream -> int = "stub_xcp_vdi_stream_close"
  external get_error_string: stream -> string = "stub_xcp_vdi_stream_get_error_string"
  external get_format: stream -> string = "xcp_vdi_stream_get_format"
  external dump_info: stream -> int -> unit = "stub_xcp_vdi_stream_dump_info"
  external read: stream -> int * Bigarray.int8_unsigned_elt = "stub_xcp_vdi_stream_read"
end
