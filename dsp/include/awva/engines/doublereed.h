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
	static constexpr const char* summary = "physical double reeds: reed, conical bore and tone holes, fitted to recordings (3 presets)";
	// pres the breath, vpres the tongue, but_y the embouchure; acc_abs (flutter) and the pedal (release)
	static constexpr uint32_t inputs = in_pressure | in_velocity | in_tilt_y | in_shake | in_pedal;
	// profile.sh 27352 (the worst preset); ten voices at ff with notes jumping every 0.25 s: 69.7k
	static constexpr Budget budget = {::doublereed::MAX_VOICES, 28000, 2.0f};
	// the hero parameters: the reed's opening and stiffness (the embouchure), the lattice's cutoff
	// (the instrument's colour), the loudest breath, the air, the tongue, the embouchure under tilt,
	// the release
	static constexpr Heroes heroes = {{"reed_open", "p_close", "lattice_hz", "p_loud", "breath", "tongue", "emb_tilt", "release"}};
};
} // namespace traits

namespace engines {
using doublereed = Adapter<traits::doublereed>;
} // namespace engines
} // namespace awva

#endif
