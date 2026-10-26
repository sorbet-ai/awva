# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
# awva_dsp: the synth's DSP, platform independent (no HAL, no libm calls, no heap, no globals):
# the voice engines and the master effects, the small helpers they share, and the engine contract
# (docs/README.md). Header only on purpose: the engines are static inline and compile into the one
# translation unit that runs them (an embedded host's synth thread, or sdk/ on the PC), so the
# compiler sees the whole DSP at once.
#   awva/engine.h           the contract: VoiceEngine, Effect, VoiceControls, EngineInfo
#   awva/adapter.h          an engine header in the voice_<name>.h shape as a VoiceEngine
#   awva/engine_set.h       a compile-time set of engines (a host that switches between them)
#   awva/engines/<name>.h   each engine as a VoiceEngine: awva::dsp::engines::<name>
#   awva/effects/fx.h       the master effects as an Effect: awva::dsp::effects::fx
#   awva/{controls,params,dspmath,svf}.h   the shared helpers
add_library(awva_dsp INTERFACE)
add_library(awva::dsp ALIAS awva_dsp)
target_include_directories(awva_dsp INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(awva_dsp INTERFACE cxx_std_20)

# The engines: every include/awva/engines/<name>.h (by name).
#   AWVA_ENGINES                 the names
#   AWVA_ENGINE_<name>_HEADER    the engine's own header (voice_<name>.h)
#   AWVA_ENGINE_<name>_PRESETS   its NUM_PRESETS (empty: no presets)
#   AWVA_ENGINE_<name>_TRAITS    its traits header as included (awva/engines/<name>.h; a project that
#                                adds an engine of its own sets this for it)
set(_dir "${CMAKE_CURRENT_SOURCE_DIR}/include/awva")
file(GLOB _engine_traits "${_dir}/engines/*.h")
list(SORT _engine_traits)
set(_voices "")
foreach(t IN LISTS _engine_traits)
  get_filename_component(v "${t}" NAME_WE)
  file(STRINGS "${t}" inc REGEX "^#include \"\\.\\./[a-z_0-9]+\\.h\"")
  list(GET inc 0 inc)
  string(REGEX REPLACE "^#include \"\\.\\./([a-z_0-9]+\\.h)\".*" "\\1" h "${inc}")
  if(NOT EXISTS "${_dir}/${h}")
    message(FATAL_ERROR "engines/${v}.h includes ../${h}, which doesn't exist")
  endif()
  set(np "")
  file(STRINGS "${_dir}/${h}" np_line REGEX "NUM_PRESETS = [0-9]+")
  if(np_line MATCHES "NUM_PRESETS = ([0-9]+)")
    set(np "${CMAKE_MATCH_1}")
  endif()
  set(AWVA_ENGINE_${v}_HEADER "${h}" PARENT_SCOPE)
  set(AWVA_ENGINE_${v}_TRAITS "awva/engines/${v}.h" PARENT_SCOPE)
  set(AWVA_ENGINE_${v}_PRESETS "${np}" PARENT_SCOPE)
  list(APPEND _voices ${v})
endforeach()
set(AWVA_ENGINES "${_voices}" PARENT_SCOPE)
# a new engine: configure again
set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_dir}" "${_dir}/engines")

# awva_engine_defines(<out-var> <voice>): the defines that select one engine where a single one is
# compiled (an embedded build with one engine, the target renders): SYNTH_ENGINE_HEADER, SYNTH_VOICE_NS
function(awva_engine_defines out voice)
  set(_t "${AWVA_ENGINE_${voice}_TRAITS}")
  if(NOT _t)
    set(_t "awva/engines/${voice}.h")
  endif()
  set(${out} "SYNTH_ENGINE_HEADER=\"${_t}\"" "SYNTH_VOICE_NS=${voice}" PARENT_SCOPE)
endfunction()
