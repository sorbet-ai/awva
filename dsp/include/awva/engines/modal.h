// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The modal engine (voice_modal.h) as a awva::VoiceEngine: awva::engines::modal.
#ifndef AWVA_ENGINES_MODAL_H_
#define AWVA_ENGINES_MODAL_H_

#include "../voice_modal.h"
#include "../adapter.h"

namespace awva {
namespace traits {
struct modal {
	AWVA_ENGINE_FUNCTIONS(modal)
	AWVA_ENGINE_PRESETS(modal)
	using FastMemory = NoMemory;
	using LargeMemory = NoMemory;
	static constexpr const char* name = "modal";
	static constexpr const char* title = "Modal";
	static constexpr const char* summary = "tuned percussion: banks of damped resonators, struck or rubbed (6 presets)";
	static constexpr uint32_t inputs = in_pressure | in_velocity | in_tilt_y | in_rotation | in_pedal;
	static constexpr Budget budget = {::modal::MAX_VOICES, 18000, 5.0f};
	// the hero parameters (the contract suite checks the names)
	static constexpr Heroes heroes = {{"hard", "decay", "upper", "keytrack", "hfdamp", "damper", "tremdepth", "rub"}};
};
} // namespace traits

namespace engines {
using modal = Adapter<traits::modal>;
} // namespace engines
} // namespace awva

#endif
