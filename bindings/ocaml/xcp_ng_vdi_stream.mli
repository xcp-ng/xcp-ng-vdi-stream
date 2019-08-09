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

type xcp_vdi_stream

val xcp_vdi_stream_new: unit -> xcp_vdi_stream
val xcp_vdi_stream_open: xcp_vdi_stream -> string -> string -> string -> int
val xcp_vdi_stream_close: xcp_vdi_stream -> int
val xcp_vdi_stream_get_error_string: xcp_vdi_stream -> string
val xcp_vdi_stream_get_format: xcp_vdi_stream -> string
val xcp_vdi_stream_dump_info: xcp_vdi_stream -> int -> unit
val xcp_vdi_stream_read: xcp_vdi_stream -> int * Bigarray.int8_unsigned_elt
