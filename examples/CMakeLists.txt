# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
#
# Using an engine in your own program (README.md, "Use an engine in your own program"):
#   c_host     plain C: an engine's component library loaded at run time, a MIDI 2.0 phrase, a WAV
#   mpe_file   C++ on the SDK: a standard (MPE) MIDI file through an engine, a WAV
# Built with the SDK (sdk/CMakeLists.txt adds this directory). Tests: example.* run each, twice
# (non-silent, the same hash both times), and the MPE render against awva_render's.
enable_language(C)

add_executable(c_host c_host/c_host.c)
set_target_properties(c_host PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_include_directories(c_host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../component/include")
target_link_libraries(c_host PRIVATE ${CMAKE_DL_LIBS})
if(UNIX)
  target_link_libraries(c_host PRIVATE m)
endif()
add_dependencies(c_host nsc_components)

add_executable(mpe_file mpe_file/mpe_file.cpp)
target_link_libraries(mpe_file PRIVATE awva_sdk awva_engines)

set(_run "${CMAKE_CURRENT_SOURCE_DIR}/run_example.cmake")
set(_out "${CMAKE_CURRENT_BINARY_DIR}/out")
add_test(NAME example.c_host COMMAND "${CMAKE_COMMAND}" -DMODE=twice "-DOUT=${_out}"
  "-DCMD=$<TARGET_FILE:c_host>;${AWVA_COMPONENT_DIR};va;${_out}/c_host_va.wav" -P "${_run}")
add_test(NAME example.mpe_file COMMAND "${CMAKE_COMMAND}" -DMODE=renderer "-DOUT=${_out}"
  "-DMPE=$<TARGET_FILE:mpe_file>" "-DRENDER=$<TARGET_FILE:awva_render>" -P "${_run}")
set_tests_properties(example.c_host example.mpe_file PROPERTIES LABELS "example;unit")
