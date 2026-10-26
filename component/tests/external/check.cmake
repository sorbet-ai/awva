# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
#
# cmake -DAWVA=<awva> -DOUT=<a build directory> [the parent build's toolchain] -P check.cmake: the
# outside project configures and builds, its component's generated sources are in its own build
# directory, and this run wrote nothing at a filesystem root (as an unset path would:
# /awva_components, D:\awva_...; what an older build left there doesn't count).
#
# The toolchain is the build's that runs the test, so the outside project builds as it does
# whatever the environment's default compiler is (component/CMakeLists.txt passes them):
#   GENERATOR, GENERATOR_PLATFORM, GENERATOR_TOOLSET, MAKE_PROGRAM, C_COMPILER, CXX_COMPILER,
#   TOOLCHAIN_FILE, BUILD_TYPE (empty ones are left out)
cmake_minimum_required(VERSION 3.25) # (a script: the policies of the version it is written for)
include("${CMAKE_CURRENT_LIST_DIR}/root_snapshot.cmake")

set(roots "/")
if(OUT MATCHES "^([A-Za-z]:/)")
  list(APPEND roots "${CMAKE_MATCH_1}")
endif()
list(REMOVE_DUPLICATES roots)
awva_root_snapshot(before ${roots})

set(gen Ninja)
if(GENERATOR)
  set(gen "${GENERATOR}")
endif()
set(cfg -G "${gen}" "-DAWVA=${AWVA}")
if(GENERATOR_PLATFORM)
  list(APPEND cfg -A "${GENERATOR_PLATFORM}")
endif()
if(GENERATOR_TOOLSET)
  list(APPEND cfg -T "${GENERATOR_TOOLSET}")
endif()
foreach(v MAKE_PROGRAM C_COMPILER CXX_COMPILER TOOLCHAIN_FILE BUILD_TYPE)
  if(${v})
    list(APPEND cfg "-DCMAKE_${v}=${${v}}")
  endif()
endforeach()

file(REMOVE_RECURSE "${OUT}")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_LIST_DIR}" -B "${OUT}" ${cfg} RESULT_VARIABLE rc OUTPUT_VARIABLE log ERROR_VARIABLE log)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "the outside project doesn't configure (${cfg}):\n${log}")
endif()
set(build --build "${OUT}" --target external_va)
if(BUILD_TYPE)
  list(APPEND build --config "${BUILD_TYPE}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" ${build} RESULT_VARIABLE rc OUTPUT_VARIABLE log ERROR_VARIABLE log)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "the outside project's component doesn't build (${cfg}):\n${log}")
endif()
if(NOT EXISTS "${OUT}/awva_components/external_va/component.cpp")
  message(FATAL_ERROR "the component's sources are not in the outside project's build directory")
endif()

awva_root_snapshot(after ${roots})
awva_root_written(written "${before}" "${after}")
if(written)
  list(JOIN written "\n  " w)
  message(FATAL_ERROR "awva_add_component wrote outside the build directory:\n  ${w}")
endif()
message("awva_add_component from outside: ok")
