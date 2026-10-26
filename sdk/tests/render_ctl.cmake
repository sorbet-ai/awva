# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
# awva_render: the controls an engine got (--ctl-out) played back (--ctl) give the same render, and
# --transpose moves only the note. Run by ctest: cmake -DRENDER=<awva_render> -DREC=<keys.csv> -DOUT=<dir> -P render_ctl.cmake
foreach(v RENDER REC OUT)
  if(NOT DEFINED ${v})
    message(FATAL_ERROR "render_ctl.cmake: ${v} is not set")
  endif()
endforeach()
file(MAKE_DIRECTORY "${OUT}")
execute_process(COMMAND "${RENDER}" --engine va --rec "${REC}" --seconds 2 --ctl-out "${OUT}/a.ctl" --manifest "${OUT}/a.json"
  OUTPUT_VARIABLE a RESULT_VARIABLE ra ERROR_QUIET)
execute_process(COMMAND "${RENDER}" --engine va --ctl "${OUT}/a.ctl" OUTPUT_VARIABLE b RESULT_VARIABLE rb ERROR_QUIET)
if(NOT ra EQUAL 0 OR NOT rb EQUAL 0 OR NOT a STREQUAL b OR a STREQUAL "")
  message(FATAL_ERROR "the controls' render differs: '${a}' (exit ${ra}) vs '${b}' (exit ${rb})")
endif()
file(READ "${OUT}/a.json" m)
string(JSON kind GET "${m}" input_kind)
string(JSON hash GET "${m}" hash)
if(NOT kind STREQUAL "rec" OR NOT a MATCHES "${hash}")
  message(FATAL_ERROR "the manifest doesn't name the render: ${kind} ${hash} vs ${a}")
endif()
execute_process(COMMAND "${RENDER}" --engine va --rec "${REC}" --seconds 2 --transpose 12 --ctl-out "${OUT}/t.ctl" OUTPUT_QUIET ERROR_QUIET)
file(SIZE "${OUT}/a.ctl" sa)
file(SIZE "${OUT}/t.ctl" st)
if(NOT sa EQUAL st)
  message(FATAL_ERROR "--transpose changed the controls' length")
endif()
message(STATUS "controls round trip: ${a}")
