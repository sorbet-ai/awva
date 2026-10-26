// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The bowed engine (voice_bowed.h) as a awva::VoiceEngine: awva::engines::bowed.
#ifndef AWVA_ENGINES_BOWED_H_
#define AWVA_ENGINES_BOWED_H_

#include "../voice_bowed.h"
#include "../adapter.h"

namespace awva {
namespace traits {
struct bowed {
	AWVA_ENGINE_FUNCTIONS(bowed)
	AWVA_ENGINE_PRESETS(bowed)
	using StreamMemory = ::bowed::StreamMemory;
	using BulkMemory = ::bowed::BulkMemory;
	// init() writes every byte of both regions (reset of every voice): no zeroing on a switch (engine.h)
	static constexpr bool init_writes_memory = true;
	static constexpr const char* name = "bowed";
	static constexpr const char* title = "Bowed Strings";
	// a short name (1 to 6 characters, for narrow displays) and a colour (0xrrggbb, not 0): required
	static constexpr const char* short_name = "bowed";
	static constexpr MonoFamily mono = MonoFamily::glide; // (mono.h: a finger slides on the string)
	static constexpr uint32_t led = 0x400000;
	static constexpr const char* summary = "bowed strings: friction-driven waveguide strings, modal body, section (9 presets)";
	static constexpr uint32_t inputs = in_pressure | in_pressure_rate | in_timbre | in_accel | in_spin | in_sustain;
	static constexpr int max_voices = ::bowed::MAX_VOICES;
	static constexpr float tail_seconds = 2.0f;
	static constexpr Cost cost = {100.0f};
	// the hero parameters (the contract suite checks the names)
	static constexpr Heroes heroes = {{"force_hi", "pos", "rosin", "bright", "body_amt", "damp", "vibrato", "ensemble"}};
	static constexpr PresetHeroes preset_heroes[] = {
	    {"pizzicato", {{"pizz_decay", "pizz_bright", "pos", "bright", "body_amt", "damp", "rel", "vibrato"}}},
	    {"section", {{"ensemble", "ens_detune", "ens_spread", "force_hi", "pos", "bright", "body_amt", "vibrato"}}},
	};
};
} // namespace traits

namespace engines {
using bowed = Adapter<traits::bowed>;
} // namespace engines
} // namespace awva

#endif
