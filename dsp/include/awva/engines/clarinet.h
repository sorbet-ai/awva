// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The clarinet engine (voice_clarinet.h) as a awva::VoiceEngine: awva::engines::clarinet.
// Its bore delay lines (102.4 KB, as long as 192 kHz needs): BulkMemory.
#ifndef AWVA_ENGINES_CLARINET_H_
#define AWVA_ENGINES_CLARINET_H_

#include "../voice_clarinet.h"
#include "../adapter.h"

namespace awva {
namespace traits {
struct clarinet {
	AWVA_ENGINE_FUNCTIONS(clarinet)
	AWVA_ENGINE_PRESETS(clarinet)
	using StreamMemory = NoMemory;
	using BulkMemory = ::clarinet::BulkMemory;
	// init() writes every byte of its BulkMemory (every voice's four delay lines): no zeroing on a switch (engine.h)
	static constexpr bool init_writes_memory = true;
	static constexpr const char* name = "clarinet";
	static constexpr const char* title = "Clarinet";
	static constexpr const char* short_name = "clar";
	static constexpr uint32_t led = 0x003010; // the clarinet voice key's colour in the firmware
	static constexpr const char* summary = "physical clarinets: a single reed on a cylindrical bore with register hole and tone-hole lattice, fitted to recordings (2 presets)";
	// pressure the breath, pressure_rate the tongue, timbre the embouchure; sustain lengthens the release
	static constexpr uint32_t inputs = in_pressure | in_pressure_rate | in_timbre | in_sustain;
	static constexpr int max_voices = ::clarinet::MAX_VOICES;
	static constexpr float tail_seconds = 2.0f; // silent ~0.25 s after the release
	static constexpr Cost cost = {131.25f};     // measured 127.2 (40688 a block: the B-flat; the bass clarinet 34728)
	// (below 44.1 kHz the model runs at 2 to 4 times the rate, decimated: voice_clarinet.h OS_RATE)
	// the hero parameters: the reed's opening and resonance, the breath at ff and at the lightest touch, the lattice
	// cutoff, the tongue, the embouchure under tilt, the release
	static constexpr Heroes heroes = {{"zeta", "reed_hz", "g_loud", "p_soft", "lat_hz", "tongue", "emb", "release"}};
};
} // namespace traits

namespace engines {
using clarinet = Adapter<traits::clarinet>;
} // namespace engines
} // namespace awva

#endif
