# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
#
# awva_conformance on its own, for a host's CI (docs/conformance.md):
#   cmake -S component/conformance -B build/conformance -G Ninja && cmake --build build/conformance
#   build/conformance/awva_conformance <component library> [--known=TAG,...]
# With -DAWVA_CONFORMANCE_LIBRARIES=<library>;...: one ctest per check of each (ctest --test-dir
# build/conformance).
cmake_minimum_required(VERSION 3.25)
project(awva_conformance LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
include("${CMAKE_CURRENT_SOURCE_DIR}/conformance.cmake")
set(AWVA_CONFORMANCE_LIBRARIES "" CACHE STRING "component libraries to check (ctest)")
foreach(lib IN LISTS AWVA_CONFORMANCE_LIBRARIES)
  get_filename_component(_n "${lib}" NAME_WE)
  awva_conformance_tests("conformance.${_n}." "${lib}")
endforeach()
