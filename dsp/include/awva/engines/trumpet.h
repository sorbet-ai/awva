// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The trumpet engine (voice_trumpet.h) as a awva::VoiceEngine: awva::engines::trumpet.
// Its bore delay lines (80 KB): 64 KB StreamMemory, 16 KB BulkMemory.
#ifndef AWVA_ENGINES_TRUMPET_H_
#define AWVA_ENGINES_TRUMPET_H_

#include "../voice_trumpet.h"
#include "../adapter.h"

namespace awva {
namespace traits {
struct trumpet {
	AWVA_ENGINE_FUNCTIONS(trumpet)
	AWVA_ENGINE_PRESETS(trumpet)
	using StreamMemory = ::trumpet::StreamMemory;
	using BulkMemory = ::trumpet::BulkMemory;
	// init() writes every byte of both regions (reset of every voice): no zeroing on a switch (engine.h)
	static constexpr bool init_writes_memory = true;
	static constexpr const char* name = "trumpet";
	static constexpr const char* title = "Trumpet";
	// a short name (1 to 6 characters, for narrow displays) and a colour (0xrrggbb, not 0): required
	static constexpr const char* short_name = "tpt";
	static constexpr uint32_t led = 0x003030;
	static constexpr const char* summary = "physical trumpet: lips, mouthpiece and bore, played like the brass trumpet (1 preset)";
	// pressure the breath, pressure_rate the attack, timbre the lips (lip_tilt); motion.accel_g and sustain
	static constexpr uint32_t inputs = in_pressure | in_pressure_rate | in_timbre | in_accel | in_sustain;
	static constexpr int max_voices = ::trumpet::MAX_VOICES;
	static constexpr float tail_seconds = 2.0f;
	static constexpr Cost cost = {137.5f}; // measured 132.15 (42288 a block); silent 0.25 s after the release
	// the hero parameters (the contract suite checks the names)
	// the new model's: brassiness, the bell and the radiation's brightness, the tongue, the loudest
	// breath, the breath noise, the lips under timbre, the release (no presets to override them)
	static constexpr Heroes heroes = {{"brassiness", "bell_hz", "tone_hz", "tongue", "p_loud", "breath", "lip_tilt", "release"}};
	// every rate within the default 3 cents of 48 kHz (contract_rates.cpp): no rate tolerances
};
} // namespace traits

namespace engines {
using trumpet = Adapter<traits::trumpet>;
} // namespace engines
} // namespace awva

#endif
