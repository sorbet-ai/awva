// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The master effects (fx.h) as a awva::Effect: awva::effects::fx (mono in, stereo out).
#ifndef AWVA_EFFECTS_FX_H_
#define AWVA_EFFECTS_FX_H_

#include "../fx.h"
#include "../adapter.h"

namespace awva {
namespace traits {
struct fx {
	using Engine = ::fx::Engine;
	using StreamMemory = ::fx::StreamMemory;
	using BulkMemory = ::fx::BulkMemory;
	static constexpr const char* name = "fx";
	static constexpr const char* summary = "master effects: drive, phaser, chorus, tremolo, delay, plate reverb, limiter";
	static constexpr int inputs = 1, outputs = 2;
	static constexpr Cost cost = {443.75f};                // a frame (14200 a block), its defaults (all stages on: about 28k a block)
	static constexpr Heroes heroes = {{"rvmix", "dlmix"}}; // the effect amounts an app shows first
	static void init(Engine& e, int sample_rate) { ::fx::init(e, sample_rate); }
	static void process(Engine& e, const float* const* in, float* const* out) { ::fx::process(e, in[0], out[0], out[1]); }
};
} // namespace traits

namespace effects {
using fx = EffectAdapter<traits::fx>;
} // namespace effects
} // namespace awva

#endif
