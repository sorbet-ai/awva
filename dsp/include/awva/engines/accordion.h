// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The accordion engine (voice_accordion.h) as a awva::VoiceEngine: awva::engines::accordion.
#ifndef AWVA_ENGINES_ACCORDION_H_
#define AWVA_ENGINES_ACCORDION_H_

#include "../voice_accordion.h"
#include "../adapter.h"

namespace awva {
namespace traits {
struct accordion {
	AWVA_ENGINE_FUNCTIONS(accordion)
	AWVA_ENGINE_PRESETS(accordion)
	using FastMemory = NoMemory;
	using LargeMemory = NoMemory;
	static constexpr const char* name = "accordion";
	static constexpr const char* title = "Accordion";
	static constexpr const char* summary =
	    "free-reed accordion: registers, musette tuning, reed chambers, cassotto, a shared bellows (9 presets)";
	// pres drives the bellows, but_y the tilt expression; acc_abs the bellows shake, the pedal its reversal
	static constexpr uint32_t inputs = in_pressure | in_timbre | in_shake | in_pedal;
	static constexpr Budget budget = {::accordion::MAX_VOICES, 32000, 1.0f}; // master (preset 6) 31556; silent 0.67 s after the release
	// the hero parameters: how it plays (the musette's beating, the bellows, the pressure response, the
	// reeds' speech, the cassotto, the chamber's brightness, the pallet bend, the tilt expression)
	static constexpr Heroes heroes = {{"beat", "bellows", "curve", "speak_ms", "cassotto", "ch_hz", "bend", "tilt"}};
};
} // namespace traits

namespace engines {
using accordion = Adapter<traits::accordion>;
} // namespace engines
} // namespace awva

#endif
