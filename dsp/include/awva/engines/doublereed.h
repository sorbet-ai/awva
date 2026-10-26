// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The double reed engine (voice_doublereed.h) as a awva::VoiceEngine: awva::engines::doublereed.
// Its bore delay lines (40 KB): LargeMemory.
#ifndef AWVA_ENGINES_DOUBLEREED_H_
#define AWVA_ENGINES_DOUBLEREED_H_

#include "../voice_doublereed.h"
#include "../adapter.h"

namespace awva {
namespace traits {
struct doublereed {
	AWVA_ENGINE_FUNCTIONS(doublereed)
	AWVA_ENGINE_PRESETS(doublereed)
	using FastMemory = NoMemory;
	using LargeMemory = ::doublereed::LargeMemory;
	// init() writes every byte of its region (reset of every voice): no zeroing on a switch (engine.h)
	static constexpr bool init_writes_memory = true;
	static constexpr const char* name = "doublereed";
	static constexpr const char* summary = "physical double reeds: reed, conical bore and tone holes, a living player (oboe, cor anglais, bassoon)";
	// pres the breath, vpres the tongue, but_y the embouchure; acc_abs (flutter) and the pedal (release)
	static constexpr uint32_t inputs = in_pressure | in_velocity | in_tilt_y | in_shake | in_pedal;
	static constexpr Budget budget = {::doublereed::MAX_VOICES, 29000, 2.0f}; // profile.sh: oboe 28440 (the worst preset)
	// the hero parameters: the reed's opening and stiffness (the embouchure), the lattice's cutoff
	// (the instrument's colour), the loudest breath, the player's wander (the tone's life), the
	// tongue, the embouchure under tilt, the release
	static constexpr Heroes heroes = {{"reed_open", "p_close", "lattice_hz", "p_loud", "wander", "tongue", "emb_tilt", "release"}};
	// the bassoon is checked on its own range (Bb1 to E5, as fitted: every note there speaks in its register);
	// above it, past a bassoon's top, the notes are not its (at C6 a register below, 95 c flat)
	static constexpr PresetTuning preset_tuning[] = {
	    {"bassoon", {.low = 34.0f, .high = 76.0f, .why = "a bassoon's range, Bb1 to E5"}},
	};
};
} // namespace traits

namespace engines {
using doublereed = Adapter<traits::doublereed>;
} // namespace engines
} // namespace awva

#endif
