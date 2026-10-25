// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// Sound presets on the PC (awva/preset.h is the format): an engine's values saved by name as a
// preset, a preset loaded onto an engine (by name: it survives the engine's versions, the report
// says what didn't apply as written), and the factory presets as files.
//
//   awva::sdk::Engine e(*awva::sdk::find_engine("va"));
//   e.init(48000, 10);
//   const auto r = awva::sdk::load_preset_json(e, text);   // r.exact, r.clamped, r.skipped, ...
//   const std::string json = awva::sdk::to_json(awva::sdk::save_preset(e, "My pad"));
#ifndef AWVA_SDK_PRESETS_H_
#define AWVA_SDK_PRESETS_H_

#include <string>
#include <string_view>
#include <vector>

#include "awva/preset.h"
#include "awva_sdk/engines.h"

namespace awva::sdk {

// what loading a preset did
struct PresetReport {
	bool loaded = false;                                // false: refused (another engine, or not a preset: refused says why)
	bool exact = false;                                 // the same schema hash, every value as written, nothing skipped or missing
	std::string refused;                                // "engine <id>" (another engine's), or the format error
	std::vector<std::string> clamped, skipped, missing; // (names, up to preset::MAX_REPORTED each)
};

// the engine's parameter schema hash (preset.h schema_hash: the component's schema_hash too)
[[nodiscard]] uint32_t params_hash(const EngineEntry& e);
// the engine's default values: its default preset's (what init() gives)
[[nodiscard]] std::vector<float> default_values(const EngineEntry& e);

// p onto e: its values by name as a preset sets them (preset.h resolve: clamped, skipped, missing
// reported); the defaults for what it doesn't name. Refused (nothing changes) for another engine.
PresetReport load_preset(Engine& e, const preset::Preset& p);
PresetReport load_preset_json(Engine& e, std::string_view json);

// e's current values as a preset: its id, the schema hash, every parameter by name, the current
// preset's heroes; version, description and modes as given
[[nodiscard]] preset::Preset save_preset(Engine& e, std::string_view title, std::string_view description = {}, std::string_view version = {});

// the JSON text (preset.h write), "" if it didn't fit (it always does: capacities are the format's)
[[nodiscard]] std::string to_json(const preset::Preset& p);
// a JSON text as a preset; false (and error) if it isn't one
bool from_json(std::string_view json, preset::Preset& p, preset::Error* error = nullptr);

} // namespace awva::sdk

#endif // AWVA_SDK_PRESETS_H_
