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
	using FastMemory = NoMemory;
	using LargeMemory = NoMemory;
	static constexpr const char* name = "ep";
	static constexpr const char* title = "Electric Piano";
	// a short name (1 to 6 characters, for narrow displays) and a colour (0xrrggbb, not 0): required
	static constexpr const char* short_name = "ep";
	static constexpr uint32_t led = 0x280040;
	static constexpr const char* summary = "electric pianos: modal tines and reeds, pickups, damper (14 presets)";
	static constexpr uint32_t inputs = in_pressure | in_pressure_rate | in_timbre | in_sustain;
	static constexpr Budget budget = {::ep::MAX_VOICES, 12500, 3.0f};
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
