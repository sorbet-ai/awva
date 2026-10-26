// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The flute engine (voice_flute.h) as a awva::VoiceEngine: awva::engines::flute.
// Its delay lines (bore and jet, 120 KB: long enough at 192 kHz): LargeMemory.
#ifndef AWVA_ENGINES_FLUTE_H_
#define AWVA_ENGINES_FLUTE_H_

#include "../voice_flute.h"
#include "../adapter.h"

namespace awva {
namespace traits {
struct flute {
	AWVA_ENGINE_FUNCTIONS(flute)
	AWVA_ENGINE_PRESETS(flute)
	using FastMemory = NoMemory;
	using LargeMemory = ::flute::LargeMemory;
	static constexpr const char* name = "flute";
	static constexpr const char* summary = "physical flutes: an air jet on the edge of an open-open bore (flute, alto flute, piccolo)";
	// pres the breath, vpres the tongue, but_y the jet angle and cover; acc_abs (vibrato) and the pedal
	static constexpr uint32_t inputs = in_pressure | in_velocity | in_tilt_y | in_shake | in_pedal;
	static constexpr Budget budget = {::flute::MAX_VOICES, 37000, 2.0f}; // profile.sh 36588 (alto flute)
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
