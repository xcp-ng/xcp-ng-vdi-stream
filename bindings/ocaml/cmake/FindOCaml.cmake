# ==============================================================================
# FindOCaml.cmake
#
# Copyright (C) 2019  xcp-ng-vdi-stream
# Copyright (C) 2019  Vates SAS
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
# ==============================================================================

# Find the OCaml headers.
#
# This will define the following variables:
#   OCaml_FOUND
#   OCaml_INCLUDE_DIRS
#
# and the following imported targets:
#   Ocaml::Ocaml

find_program(OCAMLFIND NAMES ocamlfind)

if (OCAMLFIND)
  execute_process(
    COMMAND ${OCAMLFIND} ocamlc -where
    OUTPUT_VARIABLE OCaml_INCLUDE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
endif ()

mark_as_advanced(OCaml_FOUND OCaml_INCLUDE_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OCaml
  REQUIRED_VARS OCaml_INCLUDE_DIR
)

if (OCaml_FOUND)
  set(OCaml_INCLUDE_DIRS ${OCaml_INCLUDE_DIR})
endif ()

if (OCaml_FOUND AND NOT TARGET Ocaml::Ocaml)
  add_library(Ocaml::Ocaml INTERFACE IMPORTED)
  set_target_properties(Ocaml::Ocaml PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${OCaml_INCLUDE_DIRS}"
  )
endif ()
