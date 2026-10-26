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
	static constexpr uint32_t inputs = in_pressure | in_pressure_rate | in_timbre | in_accel;
	static constexpr Budget budget = {::reed::MAX_VOICES, 20000, 2.0f};
	// the rates whose pitch is further from 48 kHz than the default 3 cents, each a known fault with its
	// bound (a cent or two above every preset's worst, measured 2026-10-25; contract_rates.cpp)
	static constexpr const char* rate_fault = "not diagnosed yet";
	static constexpr RateTolerance rate_tolerances[] = {
	    {11025, 39.0f, false, rate_fault},  // 37.7, sharp (as wind: likely the same cause)
	    {22050, 16.0f, false, rate_fault},  // 14.6
	    {88200, 7.0f, false, rate_fault},   // 5.5, flat
	    {96000, 7.5f, false, rate_fault},   // 6.0
	    {192000, 10.5f, false, rate_fault}, // 9.0
	};
};
} // namespace traits

namespace engines {
using reed = Adapter<traits::reed>;
} // namespace engines
} // namespace awva

#endif
