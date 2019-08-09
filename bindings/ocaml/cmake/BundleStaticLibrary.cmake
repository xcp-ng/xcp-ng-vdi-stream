# ==============================================================================
# BundleStaticLibrary.cmake
#
# Original code of Cristian Adam.
# See: https://cristianadam.eu/20190501/bundling-together-static-libraries-with-cmake/
# Modified to rebuild bundled library if dependencies have been changed.
# ==============================================================================

function (bundle_static_library LIB_NAME BUNDLED_LIB_NAME)
  # 1. Collect dependencies.
  list(APPEND STATIC_LIBS ${LIB_NAME})
  function (collect_dependencies INPUT_TARGET)
    set(INPUT_LINK_LIBRARIES LINK_LIBRARIES)
    get_target_property(INPUT_TYPE ${INPUT_TARGET} TYPE)
    if (${INPUT_TYPE} STREQUAL "INTERFACE_LIBRARY")
      set(INPUT_LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    endif ()
    get_target_property(PUBLIC_DEPENDENCIES ${INPUT_TARGET} ${INPUT_LINK_LIBRARIES})
    foreach (DEPENDENCY IN LISTS PUBLIC_DEPENDENCIES)
      if (TARGET ${DEPENDENCY})
        get_target_property(ALIAS ${DEPENDENCY} ALIASED_TARGET)
        if (TARGET ${ALIAS})
          set(DEPENDENCY ${ALIAS})
        endif ()
        get_target_property(DEPENDENCY_TYPE ${DEPENDENCY} TYPE)
        if (${DEPENDENCY_TYPE} STREQUAL "STATIC_LIBRARY")
          list(APPEND STATIC_LIBS ${DEPENDENCY})
        endif ()

        get_property(ADDED GLOBAL PROPERTY ${LIB_NAME}_static_bundle_${DEPENDENCY})
        if (NOT ADDED)
          set_property(GLOBAL PROPERTY ${LIB_NAME}_static_bundle_${DEPENDENCY} ON)
          collect_dependencies(${DEPENDENCY})
        endif ()
      endif ()
    endforeach ()
    set(STATIC_LIBS ${STATIC_LIBS} PARENT_SCOPE)
  endfunction ()

  collect_dependencies(${LIB_NAME})
  list(REMOVE_DUPLICATES STATIC_LIBS)

  set(BUNDLED_LIB_FULLNAME
    ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}${BUNDLED_LIB_NAME}${CMAKE_STATIC_LIBRARY_SUFFIX}
  )
  set(BUNDLING_TARGET ${BUNDLED_LIB_NAME}-target)

  # 2. Create .ar file.
  if (CMAKE_C_COMPILER_ID MATCHES "^(Clang|GNU)$")
    set(AR_NAME "${CMAKE_CURRENT_BINARY_DIR}/${BUNDLED_LIB_NAME}.ar")
    file(WRITE ${AR_NAME}.in "CREATE ${BUNDLED_LIB_FULLNAME}\n")

    set(BUNDLED_DEPENDENCIES)
    foreach (STATIC_LIB IN LISTS STATIC_LIBS)
      file(APPEND ${AR_NAME}.in "ADDLIB $<TARGET_FILE:${STATIC_LIB}>\n")
      list(APPEND BUNDLED_DEPENDENCIES $<TARGET_FILE:${STATIC_LIB}>)
    endforeach ()

    file(APPEND ${AR_NAME}.in "SAVE\n")
    file(APPEND ${AR_NAME}.in "END\n")

    file(GENERATE OUTPUT ${AR_NAME} INPUT ${AR_NAME}.in)

    set(AR_BIN ${CMAKE_AR})
    if (CMAKE_INTERPROCEDURAL_OPTIMIZATION)
      set(AR_BIN ${CMAKE_C_COMPILER_AR})
    endif()

    add_custom_command(
      OUTPUT ${BUNDLED_LIB_FULLNAME}
      COMMAND ${AR_BIN} -M < ${AR_NAME}
      DEPENDS ${BUNDLED_DEPENDENCIES}
      VERBATIM
    )
    add_custom_target(${BUNDLING_TARGET} ALL DEPENDS ${BUNDLED_LIB_FULLNAME})
    add_dependencies(${BUNDLING_TARGET} ${LIB_NAME})
  else ()
    message(FATAL_ERROR "Unknown bundle scenario!")
  endif ()

  # 3. Import the bundled library.
  add_library(${BUNDLED_LIB_NAME} STATIC IMPORTED)
  set_target_properties(${BUNDLED_LIB_NAME}
    PROPERTIES
    IMPORTED_LOCATION ${BUNDLED_LIB_FULLNAME}
    INTERFACE_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:${LIB_NAME},INTERFACE_INCLUDE_DIRECTORIES>
  )
  add_dependencies(${BUNDLED_LIB_NAME} ${BUNDLING_TARGET})
endfunction ()
