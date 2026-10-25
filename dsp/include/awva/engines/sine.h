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
	using FastMemory = NoMemory;
	using LargeMemory = NoMemory;
	static constexpr const char* name = "sine";
	static constexpr const char* summary = "the minimal example engine: one sine per voice, level from pressure";
	static constexpr uint32_t inputs = in_pressure;
	static constexpr Budget budget = {::sine::MAX_VOICES, 1600, 1.0f};
};
} // namespace traits

namespace engines {
using sine = Adapter<traits::sine>;
} // namespace engines
} // namespace awva

#endif
