# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
#
# A project outside awva that builds an engine as its own component (awva_add_component), as a
# plugin does: component.add_component_from_outside configures and builds it (check.cmake).
cmake_minimum_required(VERSION 3.25)
project(awva_external LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
add_library(awva_host_options INTERFACE)
enable_testing()
add_subdirectory("${AWVA}/dsp" awva_dsp)
add_subdirectory("${AWVA}/sdk" awva_sdk)
awva_add_component(external_va va STATIC)
