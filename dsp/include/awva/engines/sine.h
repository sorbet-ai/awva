// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The sine engine (voice_sine.h) as a awva::VoiceEngine: awva::engines::sine.
#ifndef AWVA_ENGINES_SINE_H_
#define AWVA_ENGINES_SINE_H_

#include "../voice_sine.h"
#include "../adapter.h"

namespace awva {
namespace traits {
struct sine {
	AWVA_ENGINE_FUNCTIONS(sine)
	using StreamMemory = NoMemory;
	using BulkMemory = NoMemory;
	static constexpr const char* name = "sine";
	static constexpr const char* title = "Sine";
	// a short name (1 to 6 characters, for narrow displays) and a colour (0xrrggbb, not 0): required
	static constexpr const char* short_name = "sine";
	static constexpr MonoFamily mono = MonoFamily::glide; // (mono.h: it slides)
	static constexpr uint32_t led = 0x202030;
	static constexpr const char* summary = "the minimal example engine: one sine per voice, level from pressure";
	static constexpr uint32_t inputs = in_pressure;
	static constexpr int max_voices = ::sine::MAX_VOICES;
	static constexpr float tail_seconds = 1.0f;
	static constexpr Cost cost = {5.0f};
};
} // namespace traits

namespace engines {
using sine = Adapter<traits::sine>;
} // namespace engines
} // namespace awva

#endif
