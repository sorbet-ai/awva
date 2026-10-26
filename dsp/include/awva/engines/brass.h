// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The brass engine (voice_brass.h) as a awva::VoiceEngine: awva::engines::brass.
#ifndef AWVA_ENGINES_BRASS_H_
#define AWVA_ENGINES_BRASS_H_

#include "../voice_brass.h"
#include "../adapter.h"

namespace awva {
namespace traits {
struct brass {
	AWVA_ENGINE_FUNCTIONS(brass)
	AWVA_ENGINE_PRESETS(brass)
	using FastMemory = NoMemory;
	using LargeMemory = ::brass::LargeMemory;
	// init() writes every byte of both regions (reset of every voice): no zeroing on a switch (engine.h)
	static constexpr bool init_writes_memory = true;
	static constexpr const char* name = "brass";
	static constexpr const char* title = "Brass";
	// a short name (1 to 6 characters, for narrow displays) and a colour (0xrrggbb, not 0): required
	static constexpr const char* short_name = "brass";
	static constexpr uint32_t led = 0x402800;
	static constexpr const char* summary = "brass: a lip source and a physical bore, bell, mutes (9 presets)";
	static constexpr uint32_t inputs = in_pressure | in_pressure_rate | in_timbre | in_accel | in_sustain;
	static constexpr Budget budget = {::brass::MAX_VOICES, 30000, 2.0f};
	// the hero parameters (the contract suite checks the names)
	static constexpr Heroes heroes = {{"brassiness", "lip_loud", "tone_hz", "breath", "mute", "release", "vibrato", "pair"}};
	static constexpr PresetHeroes preset_heroes[] = {
	    {"harmon", {{"mute", "mute_bp", "mute_hz", "wah", "brassiness", "tone_hz", "vibrato", "release"}}},
	    {"plunger", {{"mute", "mute_lp", "mute_hz", "wah", "brassiness", "tone_hz", "vibrato", "release"}}},
	    {"section", {{"pair", "detune", "brassiness", "lip_loud", "tone_hz", "breath", "vibrato", "release"}}},
	};
	// the rates whose pitch is further from 48 kHz than the default 3 cents, each a known fault with its
	// bound (a cent or two above every preset's worst, measured 2026-10-25; contract_rates.cpp)
	static constexpr const char* rate_fault = "not diagnosed yet";
	static constexpr RateTolerance rate_tolerances[] = {
	    {11025, 7.0f, false, rate_fault}, // 5.3
	    {22050, 8.0f, false, rate_fault}, // 6.1
	};
};
} // namespace traits

namespace engines {
using brass = Adapter<traits::brass>;
} // namespace engines
} // namespace awva

#endif
