// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The reed engine (voice_reed.h) as a awva::VoiceEngine: awva::engines::reed.
#ifndef AWVA_ENGINES_REED_H_
#define AWVA_ENGINES_REED_H_

#include "../voice_reed.h"
#include "../adapter.h"

namespace awva {
namespace traits {
struct reed {
	AWVA_ENGINE_FUNCTIONS(reed)
	using FastMemory = NoMemory;
	using LargeMemory = ::reed::LargeMemory;
	// init() writes every byte of both regions (reset of every voice): no zeroing on a switch (engine.h)
	static constexpr bool init_writes_memory = true;
	static constexpr const char* name = "reed";
	static constexpr const char* title = "Reed";
	static constexpr const char* summary = "single reed: clarinet to saxophone, one model, tilt morphs the bore";
	static constexpr uint32_t inputs = in_pressure | in_velocity | in_tilt_y | in_shake;
	static constexpr Budget budget = {::reed::MAX_VOICES, 20000, 2.0f};
};
} // namespace traits

namespace engines {
using reed = Adapter<traits::reed>;
} // namespace engines
} // namespace awva

#endif
