// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// A host's limits, and whether an engine fits them. The limits are a host's own (its RAM per
// memory region, its processor); awva has no limits of its own beyond the engines' declarations
// (EngineInfo: max_voices, cost, regions). A host project keeps its profile and checks every
// engine it ships against it: the contract suite runs fits() on AWVA_HOST_PROFILE (the example
// below unless a build defines another).
#ifndef AWVA_SDK_HOST_PROFILE_H_
#define AWVA_SDK_HOST_PROFILE_H_

#include <cstddef>

#include "awva/engine.h"

namespace awva::sdk {

struct HostProfile {
	const char* name;
	size_t hot_max, stream_max, bulk_max; // the bytes it has for each region of one engine
	double instr_per_second;              // reference instructions (COST_REFERENCE) it runs a second for the engine
	int sample_rate;
	int voices;
};

// how an engine fits a profile: each region, and the share of the processor it takes
struct Fit {
	bool hot, stream, bulk;
	double cpu; // the share of instr_per_second the engine takes at the profile's voices and rate
	[[nodiscard]] constexpr bool ok() const { return hot && stream && bulk && cpu < 1.0; }
};
[[nodiscard]] constexpr Fit fit(const EngineInfo& in, const HostProfile& h) {
	const int voices = h.voices < in.max_voices ? h.voices : in.max_voices;
	return {in.regions.hot < h.hot_max, in.regions.stream <= h.stream_max, in.regions.bulk <= h.bulk_max,
	    static_cast<double>(in.cost.instr_per_voice_frame) * voices * h.sample_rate / h.instr_per_second};
}

// An example: a 480 MHz Cortex-M7 (the reference core) running 10 voices at 48828 Hz, with
// 128 KB of tightly coupled RAM for the hot region, 256 KB of fast RAM and 512 KB of other RAM.
inline constexpr HostProfile example_profile = {"a 480 MHz Cortex-M7, 10 voices at 48828 Hz", 128 * 1024, 256 * 1024, 512 * 1024, 480e6, 48828, 10};

} // namespace awva::sdk

#endif // AWVA_SDK_HOST_PROFILE_H_
