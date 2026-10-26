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
	// a short name (1 to 6 characters, for narrow displays) and a colour (0xrrggbb, not 0): required
	static constexpr const char* short_name = "modal";
	static constexpr uint32_t led = 0x001840;
	static constexpr const char* summary = "tuned percussion: banks of damped resonators, struck or rubbed (6 presets)";
	static constexpr uint32_t inputs = in_pressure | in_pressure_rate | in_timbre | in_spin | in_sustain;
	static constexpr Budget budget = {::modal::MAX_VOICES, 18000, 5.0f};
	// the hero parameters (the contract suite checks the names)
	static constexpr Heroes heroes = {{"hard", "decay", "upper", "keytrack", "hfdamp", "damper", "tremdepth", "rub"}};
	// the presets inharmonic by design (voice_modal.h models): no period to compare with the note,
	// so the tuning check measures the partial tuned to the note, which the model sets exactly
	// (measured 2026-10-26: within 0.01 c for the bell, 0.11 c for the glass)
	static constexpr PresetTuning preset_tuning[] = {
	    {"bell", {.cents = 2.0f, .partial = 1.0f, .why = "a church bell's partials are inharmonic (hum 0.5, prime 1, tierce 1.183, quint 1.506, nominal 2, ...): the prime is the note"}},
	    {"wine glass", {.cents = 2.0f, .partial = 1.0f, .why = "a glass's (m,0) modes are inharmonic (1, 2.32, 4.25, 6.63, 9.4): the first is the note"}},
	};
};
} // namespace traits

namespace engines {
using modal = Adapter<traits::modal>;
} // namespace engines
} // namespace awva

#endif
