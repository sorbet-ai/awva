// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The ep engine (voice_ep.h) as a awva::VoiceEngine: awva::engines::ep.
#ifndef AWVA_ENGINES_EP_H_
#define AWVA_ENGINES_EP_H_

#include "../voice_ep.h"
#include "../adapter.h"

namespace awva {
namespace traits {
struct ep {
	AWVA_ENGINE_FUNCTIONS(ep)
	AWVA_ENGINE_PRESETS(ep)
	using StreamMemory = NoMemory;
	using BulkMemory = NoMemory;
	static constexpr const char* name = "ep";
	static constexpr const char* title = "Electric Piano";
	// a short name (1 to 6 characters, for narrow displays) and a colour (0xrrggbb, not 0): required
	static constexpr const char* short_name = "ep";
	static constexpr uint32_t led = 0x280040;
	static constexpr const char* summary = "electric pianos: modal tines and reeds, pickups, damper (14 presets)";
	static constexpr uint32_t inputs = in_pressure | in_pressure_rate | in_timbre | in_sustain;
	static constexpr int max_voices = ::ep::MAX_VOICES;
	static constexpr float tail_seconds = 3.0f;
	static constexpr Cost cost = {39.0625f};
	// the hero parameters (the contract suite checks the names)
	static constexpr Heroes heroes = {{"hard", "decay", "bell", "tick", "drive", "grit", "tone", "wobble"}};
	static constexpr PresetHeroes preset_heroes[] = {
	    {"wurli", {{"wurli", "hard", "decay", "drive", "grit", "tone", "wobble", "dyn"}}},
	};
};
} // namespace traits

namespace engines {
using ep = Adapter<traits::ep>;
} // namespace engines
} // namespace awva

#endif
