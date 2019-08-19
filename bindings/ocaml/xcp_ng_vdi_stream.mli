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

module XcpNgVdiStream:
  sig
    type stream
    val create: unit -> stream
    val open_in: stream -> string -> string -> string -> int
    val close: stream -> int
    val get_error_string: stream -> string
    val get_format: stream -> string
    val dump_info: stream -> int -> unit
    val read: stream -> int * Bigarray.int8_unsigned_elt
  end
