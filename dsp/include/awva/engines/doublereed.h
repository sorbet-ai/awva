// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The double reed engine (voice_doublereed.h) as a awva::VoiceEngine: awva::engines::doublereed.
// Its bore delay lines (40 KB): BulkMemory.
#ifndef AWVA_ENGINES_DOUBLEREED_H_
#define AWVA_ENGINES_DOUBLEREED_H_

#include "../voice_doublereed.h"
#include "../adapter.h"

namespace awva {
namespace traits {
struct doublereed {
	AWVA_ENGINE_FUNCTIONS(doublereed)
	AWVA_ENGINE_PRESETS(doublereed)
	using StreamMemory = NoMemory;
	using BulkMemory = ::doublereed::BulkMemory;
	// init() writes every byte of its BulkMemory (reset of every voice): no zeroing on a switch (engine.h)
	static constexpr bool init_writes_memory = true;
	static constexpr const char* name = "doublereed";
	static constexpr const char* title = "Double Reed";
	static constexpr const char* short_name = "dreed";
	static constexpr uint32_t led = 0x004000;
	static constexpr const char* summary = "physical double reeds: reed, conical bore and tone holes, a living player (oboe, cor anglais, bassoon)";
	// pressure the breath, pressure_rate the tongue, timbre the embouchure; motion.accel_g (flutter) and the sustain (release)
	static constexpr uint32_t inputs = in_pressure | in_pressure_rate | in_timbre | in_accel | in_sustain;
	static constexpr int max_voices = ::doublereed::MAX_VOICES;
	static constexpr float tail_seconds = 2.5f; // the soak: silent 2.1 s after the release (the oboe, the worst)
	static constexpr Cost cost = {90.63f};      // 29000 a block / 320; measured 88.9 (oboe 28440, the worst preset)
	// the hero parameters: the reed's opening and stiffness (the embouchure), the lattice's cutoff
	// (the instrument's colour), the loudest breath, the player's wander (the tone's life), the
	// tongue, the embouchure under timbre, the release
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
