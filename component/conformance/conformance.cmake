# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
#
# awva_conformance: the component ABI's conformance checks on any component library
# (awva_conformance.cpp, docs/conformance.md). It needs only nsc.h, the loader and doctest.
# Included by component/CMakeLists.txt (awva's own components) and by CMakeLists.txt here (built on
# its own, for a host's CI).
#   AWVA_DOCTEST_DIR   doctest (third_party/doctest of awva unless given)
#
# awva_conformance_tests(<prefix> <library> [KNOWN <tag>...] [LABELS <label>...]): one ctest per
# check of the library, named <prefix><check>; KNOWN: its known failures (reported, not failed)
set(_conf_dir "${CMAKE_CURRENT_LIST_DIR}")
if(NOT AWVA_DOCTEST_DIR)
  set(AWVA_DOCTEST_DIR "${_conf_dir}/../../third_party/doctest")
endif()
add_executable(awva_conformance "${_conf_dir}/awva_conformance.cpp")
target_include_directories(awva_conformance PRIVATE "${_conf_dir}/../include" "${AWVA_DOCTEST_DIR}")
target_compile_features(awva_conformance PRIVATE cxx_std_20)
target_link_libraries(awva_conformance PRIVATE ${CMAKE_DL_LIBS})
if(TARGET awva_host_options)
  target_link_libraries(awva_conformance PRIVATE awva_host_options)
endif()
include("${AWVA_DOCTEST_DIR}/cmake/doctest.cmake")

function(awva_conformance_tests prefix library)
  cmake_parse_arguments(_c "" "" "KNOWN;LABELS" ${ARGN})
  list(JOIN _c_KNOWN "," _known)
  if(NOT _c_LABELS)
    set(_c_LABELS conformance)
  endif()
  doctest_discover_tests(awva_conformance TEST_PREFIX "${prefix}" EXTRA_ARGS "${library}" "--known=${_known}"
    PROPERTIES LABELS "${_c_LABELS}")
endfunction()
