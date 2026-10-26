// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The clarinet engine (voice_clarinet.h) as a awva::VoiceEngine: awva::engines::clarinet.
// Its bore delay lines (102.4 KB, as long as 192 kHz needs): LargeMemory.
#ifndef AWVA_ENGINES_CLARINET_H_
#define AWVA_ENGINES_CLARINET_H_

#include "../voice_clarinet.h"
#include "../adapter.h"

namespace awva {
namespace traits {
struct clarinet {
	AWVA_ENGINE_FUNCTIONS(clarinet)
	AWVA_ENGINE_PRESETS(clarinet)
	using FastMemory = NoMemory;
	using LargeMemory = ::clarinet::LargeMemory;
	// init() writes every byte of its LargeMemory (every voice's four delay lines): no zeroing on a switch (engine.h)
	static constexpr bool init_writes_memory = true;
	static constexpr const char* name = "clarinet";
	static constexpr const char* title = "Clarinet";
	static constexpr const char* summary = "physical clarinets: a single reed on a cylindrical bore with register hole and tone-hole lattice, fitted to recordings (2 presets)";
	// pres the breath, vpres the tongue, but_y the embouchure; the pedal lengthens the release
	static constexpr uint32_t inputs = in_pressure | in_velocity | in_tilt_y | in_pedal;
	static constexpr Budget budget = {::clarinet::MAX_VOICES, 35000, 2.0f}; // profile.sh 34284; silent ~0.25 s after the release
	// below about 40 kHz the bass clarinet's reed (3.9 kHz) squeals: its coupling to the flow, one
	// sample late, lags too far at the reed's frequency (22050 Hz: the notes go up 15-45 semitones)
	static constexpr int min_rate = 44100;
	// the bass clarinet's short-tube notes (above its clarion) can need section B shorter than its
	// 3-sample least, so the bore stays a little long; how much depends on the rate (note 69: -16.9 c
	// at 48 kHz, 0 at 44.1 and 48.8 kHz), measured
	static constexpr RateTolerance rate_tolerances[] = {
	    {44100, 17.5f, false, "the bass clarinet's short tube clamped at 48 kHz: note 69 is -16.9 c there, in tune here"},   // 16.8
	    {48828, 17.5f, false, "the bass clarinet's short tube clamped at 48 kHz: note 69 is -16.9 c there, in tune here"},   // 16.8
	    {88200, 10.0f, false, "the bass clarinet's short tube clamped at 48 kHz and here, by different amounts (note 69)"},  // 8.9
	    {96000, 7.0f, false, "the bass clarinet's short tube clamped at 48 kHz and here, by different amounts (note 69)"},   // 6.2
	    {192000, 16.5f, false, "the bass clarinet's short tube clamped at 48 kHz and here, by different amounts (note 69)"}, // 15.5
	};
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
