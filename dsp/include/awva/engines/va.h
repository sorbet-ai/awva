// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The va engine (voice_va.h) as a awva::VoiceEngine: awva::engines::va.
#ifndef AWVA_ENGINES_VA_H_
#define AWVA_ENGINES_VA_H_

#include "../voice_va.h"
#include "../adapter.h"

namespace awva {
namespace traits {
struct va {
	AWVA_ENGINE_FUNCTIONS(va)
	AWVA_ENGINE_PRESETS(va)
	using FastMemory = NoMemory;
	using LargeMemory = NoMemory;
	static constexpr const char* name = "va";
	static constexpr const char* title = "Virtual Analog";
	static constexpr const char* summary = "virtual analog: saw/pulse/sub oscillators, sync, ladder-style filter, envelopes (8 presets)";
	static constexpr uint32_t inputs = in_pressure | in_velocity | in_tilt_y | in_shake | in_rotation | in_pedal;
	static constexpr Budget budget = {::va::MAX_VOICES, 24000, 10.0f};
	// the hero parameters (the contract suite checks the names)
	static constexpr Heroes heroes = {{"cut_base", "res", "cut_env", "drive", "saw2_detune", "sub", "release", "vibrato"}};
	static constexpr PresetHeroes preset_heroes[] = {
	    {"pad", {{"cut_base", "cut_env", "release", "saw2_detune", "drift", "res", "sub", "vibrato"}}},
	    {"bass", {{"cut_base", "res", "cut_env", "drive", "sub", "acc_level", "acc_cutoff", "release"}}},
	    {"acid", {{"cut_base", "res", "cut_env", "drive", "acc_cutoff", "acc_level", "acc_decay", "release"}}},
	};
};
} // namespace traits

namespace engines {
using va = Adapter<traits::va>;
} // namespace engines
} // namespace awva

#endif
