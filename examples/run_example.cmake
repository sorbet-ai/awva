# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
#
# The examples' tests (CMakeLists.txt):
#   MODE=twice     run CMD twice: it must succeed (a non-silent, finite render) with the same hash
#   MODE=renderer  mpe_file writes its demo, renders it, and its hash must be awva_render's
file(MAKE_DIRECTORY "${OUT}")

function(run_hash out_var)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE so ERROR_VARIABLE se)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${ARGN}: exit ${rc}\n${so}${se}")
  endif()
  string(REGEX MATCH "hash ([0-9a-f]+)" m "${so}")
  if(NOT m)
    message(FATAL_ERROR "${ARGN}: no hash in\n${so}")
  endif()
  set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
  message(STATUS "${so}")
endfunction()

if(MODE STREQUAL "twice")
  run_hash(a ${CMD})
  run_hash(b ${CMD})
  if(NOT a STREQUAL b)
    message(FATAL_ERROR "not deterministic: ${a} then ${b}")
  endif()
elseif(MODE STREQUAL "renderer")
  set(mid "${OUT}/demo_mpe.mid")
  execute_process(COMMAND "${MPE}" --demo "${mid}" RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "mpe_file --demo failed")
  endif()
  run_hash(a "${MPE}" "${mid}" "${OUT}/mpe_va.wav" va)
  run_hash(b "${MPE}" "${mid}" "${OUT}/mpe_va.wav" va)
  run_hash(r "${RENDER}" --engine va --midi "${mid}")
  if(NOT a STREQUAL b OR NOT a STREQUAL r)
    message(FATAL_ERROR "mpe_file ${a} / ${b}, awva_render ${r}: they must be the same")
  endif()
else()
  message(FATAL_ERROR "MODE?")
endif()
