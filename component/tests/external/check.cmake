# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
#
# cmake -DAWVA=<awva> -DOUT=<a build directory> -P check.cmake: the outside project configures and
# builds, its component's generated sources are in its own build directory, and nothing was written
# next to the build directory's root (as an unset path would: /awva_components, D:\awva_...).
file(REMOVE_RECURSE "${OUT}")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_LIST_DIR}" -B "${OUT}" -G Ninja "-DAWVA=${AWVA}" RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "the outside project doesn't configure")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${OUT}" --target external_va RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "the outside project's component doesn't build")
endif()
if(NOT EXISTS "${OUT}/awva_components/external_va/component.cpp")
  message(FATAL_ERROR "the component's sources are not in the outside project's build directory")
endif()
foreach(p "/awva_components" "/external_va" "/awva_va_component")
  if(EXISTS "${p}")
    message(FATAL_ERROR "awva_add_component wrote outside the build directory: ${p}")
  endif()
endforeach()
message("awva_add_component from outside: ok")
