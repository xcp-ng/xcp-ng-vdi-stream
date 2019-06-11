# ==============================================================================
# tests/CMakeLists.txt
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

set(STREAM_TO_FILE "${CMAKE_BINARY_DIR}/tools/stream-to-file")

file(GLOB QCOW2_IMAGES images/*.qcow2)
foreach (IMAGE_PATH ${QCOW2_IMAGES})
  get_filename_component(IMAGE ${IMAGE_PATH} NAME_WLE)
  add_test(
    NAME "ExportFullQCow2Image${IMAGE}"
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/check-qcow2-export" ${STREAM_TO_FILE} "${IMAGE}.qcow2"
  )
endforeach ()

foreach (IMAGE_PATH ${QCOW2_IMAGES})
  get_filename_component(IMAGE ${IMAGE_PATH} NAME_WLE)
  math(EXPR MAX "${IMAGE}")
  foreach (IMAGE_BASE RANGE 1 ${MAX} 1)
    add_test(
      NAME "ExportDeltaQCow2Image${IMAGE}-${IMAGE_BASE}"
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/check-qcow2-export" ${STREAM_TO_FILE} "${IMAGE}.qcow2" "${IMAGE_BASE}.qcow2"
    )
  endforeach ()
endforeach ()
