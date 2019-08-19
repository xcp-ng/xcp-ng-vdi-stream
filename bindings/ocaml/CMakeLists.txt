# ==============================================================================
# bindings/ocaml/CMakeLists.txt
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

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(OCaml REQUIRED)

set(XCP_OCAML_LIB_STATIC ${XCP_LIB}-static-ocaml)
set(XCP_OCAML_LIB_SHARED ${XCP_LIB}-shared-ocaml)

set(XCP_OCAML_BUNDLED_LIB ${XCP_LIB}-bundled-ocaml)

set(XCP_OCAML_LIB_STATIC_DEST "${CMAKE_CURRENT_SOURCE_DIR}/libxcp_ng_vdi_stream_stubs.a")
set(XCP_OCAML_LIB_SHARED_DEST "${CMAKE_CURRENT_SOURCE_DIR}/dllxcp_ng_vdi_stream_stubs.so")

set(OCAML_SOURCES
  xcp_ng_vdi_stream_stubs.c
)

# ------------------------------------------------------------------------------
# Libraries.
# ------------------------------------------------------------------------------

# Create static and shared libraries.
foreach (TYPE STATIC SHARED)
  add_library(${XCP_OCAML_LIB_${TYPE}} ${TYPE} ${OCAML_SOURCES})
  target_include_directories(${XCP_OCAML_LIB_${TYPE}}
    PUBLIC
      $<BUILD_INTERFACE:${XCP_NG_VDI_STREAM_INCLUDE_DIR}>
  )
  target_link_libraries(${XCP_OCAML_LIB_${TYPE}} PRIVATE ${XCP_NAMESPACE}::${XCP_MODULE} Ocaml::Ocaml)

  set_target_properties(${XCP_OCAML_LIB_${TYPE}} PROPERTIES
    LINKER_LANGUAGE C
    POSITION_INDEPENDENT_CODE ON
  )
endforeach ()

# Create a bundled static library.
include(BundleStaticLibrary)
bundle_static_library(${XCP_OCAML_LIB_STATIC} ${XCP_OCAML_BUNDLED_LIB})

# ------------------------------------------------------------------------------
# Commands to copy libraries in bindings/ocaml source dir.
# ------------------------------------------------------------------------------

add_custom_command(
  OUTPUT ${XCP_OCAML_LIB_STATIC_DEST}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${XCP_OCAML_BUNDLED_LIB}> ${XCP_OCAML_LIB_STATIC_DEST}
  DEPENDS $<TARGET_FILE:${XCP_OCAML_BUNDLED_LIB}>
  VERBATIM
)
add_custom_target("${XCP_OCAML_LIB_STATIC}-copy" DEPENDS ${XCP_OCAML_LIB_STATIC_DEST})
add_dependencies("${XCP_OCAML_LIB_STATIC}-copy" ${XCP_OCAML_BUNDLED_LIB})

add_custom_command(
  OUTPUT ${XCP_OCAML_LIB_SHARED_DEST}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${XCP_OCAML_LIB_SHARED}> ${XCP_OCAML_LIB_SHARED_DEST}
  DEPENDS $<TARGET_FILE:${XCP_OCAML_LIB_SHARED}>
  VERBATIM
)
add_custom_target("${XCP_OCAML_LIB_SHARED}-copy" DEPENDS ${XCP_OCAML_LIB_SHARED_DEST})
add_dependencies("${XCP_OCAML_LIB_SHARED}-copy" ${XCP_OCAML_LIB_SHARED})

# ------------------------------------------------------------------------------
# Build package.
# ------------------------------------------------------------------------------

find_program(DUNE NAMES dune)
if (NOT DUNE)
  cmake(FATAL_ERROR "Unable to find dune required to build OCaml binding.")
endif ()

set(DUNE_BUILD_INSTALL_FILE "${PROJECT_SOURCE_DIR}/xcp_ng_vdi_stream.install")

add_custom_command(
  OUTPUT ${DUNE_BUILD_INSTALL_FILE}
  COMMAND rm -f ${DUNE_BUILD_INSTALL_FILE}
  COMMAND ${DUNE} build -p xcp-ng-vdi-stream --build-dir=dune-build --verbose
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  DEPENDS ${XCP_OCAML_LIB_STATIC_DEST} ${XCP_OCAML_LIB_SHARED_DEST} "${CMAKE_CURRENT_SOURCE_DIR}/dune"
)
add_custom_target(dune ALL DEPENDS ${DUNE_BUILD_INSTALL_FILE})
add_dependencies(dune ${XCP_OCAML_LIB_STATIC} ${XCP_OCAML_LIB_SHARED})
