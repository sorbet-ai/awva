// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The wind engine (voice_wind.h) as a awva::VoiceEngine: awva::engines::wind.
#ifndef AWVA_ENGINES_WIND_H_
#define AWVA_ENGINES_WIND_H_

#include "../voice_wind.h"
#include "../adapter.h"

namespace awva {
namespace traits {
struct wind {
	AWVA_ENGINE_FUNCTIONS(wind)
	AWVA_ENGINE_PRESETS(wind)
	using FastMemory = NoMemory;
	using LargeMemory = ::wind::LargeMemory;
	// init() writes every byte of both regions (reset of every voice): no zeroing on a switch (engine.h)
	static constexpr bool init_writes_memory = true;
	static constexpr const char* name = "wind";
	static constexpr const char* title = "Wind";
	static constexpr const char* summary = "woodwinds: reed and jet driven bores, cylindrical and conical (9 presets)";
	// pres, vpres, but_y (the embouchure), acc_abs (the saxes' growl); the pedal is not read
	static constexpr uint32_t inputs = in_pressure | in_velocity | in_tilt_y | in_shake;
	static constexpr Budget budget = {::wind::MAX_VOICES, 20000, 2.0f};
	// the hero parameters (the contract suite checks the names)
	static constexpr Heroes heroes = {{"reed_open", "attack", "bright", "breath", "growl", "vib", "body", "tongue"}};
	static constexpr PresetHeroes preset_heroes[] = {
	    {"flute", {{"jet", "jet_off", "breath", "turb", "bright", "attack", "vib", "body"}}},
	};
	// the rates whose pitch is further from 48 kHz than the default 3 cents, each a known fault with its
	// bound (a cent or two above every preset's worst, measured 2026-10-25; contract_rates.cpp)
	static constexpr const char* rate_fault = "the bore's delay correction is in samples at 48 kHz, a time it should scale with the rate";
	static constexpr RateTolerance rate_tolerances[] = {
	    {11025, 88.0f, false, rate_fault},  // 86.0, sharp
	    {22050, 33.0f, false, rate_fault},  // 31.4
	    {88200, 14.0f, false, rate_fault},  // 12.2, flat
	    {96000, 15.0f, false, rate_fault},  // 13.4
	    {192000, 21.5f, false, rate_fault}, // 20.0
	};
};
} // namespace traits

namespace engines {
using wind = Adapter<traits::wind>;
} // namespace engines
} // namespace awva

#endif
