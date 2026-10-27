// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The flute engine (voice_flute.h) as a awva::VoiceEngine: awva::engines::flute.
// Its delay lines (bore and jet, 120 KB: long enough at 192 kHz): BulkMemory.
#ifndef AWVA_ENGINES_FLUTE_H_
#define AWVA_ENGINES_FLUTE_H_

#include "../voice_flute.h"
#include "../adapter.h"

namespace awva {
namespace traits {
struct flute {
	AWVA_ENGINE_FUNCTIONS(flute)
	AWVA_ENGINE_PRESETS(flute)
	using StreamMemory = NoMemory;
	using BulkMemory = ::flute::BulkMemory;
	static constexpr const char* name = "flute";
	static constexpr const char* title = "Flute";
	static constexpr const char* short_name = "flute";
	static constexpr uint32_t led = 0x004000;
	static constexpr const char* summary = "physical flutes: an air jet on the edge of an open-open bore (flute, alto flute, piccolo)";
	// pressure the breath, pressure_rate the tongue, timbre the jet angle and cover; motion.accel_g (vibrato) and sustain
	static constexpr uint32_t inputs = in_pressure | in_pressure_rate | in_timbre | in_accel | in_sustain;
	static constexpr int max_voices = ::flute::MAX_VOICES;
	static constexpr float tail_seconds = 2.0f;
	static constexpr Cost cost = {115.625f}; // measured 114.3 (36588 a block: the alto flute)
	// the hero parameters: the breath, the jet (its phase, the edge offset, its noise, overblowing), the bore's
	// brightness, the tongue, the release (no presets to override them)
	static constexpr Heroes heroes = {{"p_loud", "theta", "y0", "turb", "ob_amt", "cut_hz", "tongue", "release"}};
};
} // namespace traits

namespace engines {
using flute = Adapter<traits::flute>;
} // namespace engines
} // namespace awva

#endif
