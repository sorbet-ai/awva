// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// soak: the long run, behind the ctest label "soak" (ctest -L soak; not in CI's default run):
// every preset, in three phases (after the sax lane's soak):
//   chaos   random playing (notes in and out of range, instant strikes, pressure to 1.5, tilts,
//           shake, pedal) with random parameter writes, AWVA_SOAK_SECONDS long (default 60)
//   held    the preset's values back, every voice at full pressure on fixed notes with slow
//           tilts, 10 s
//   decay   everything released, the tail and 2 s more
// Checked: finite and bounded throughout, silent and idle after the decay. Reported per phase:
// the peak, subnormal samples, the cost of a block (a subnormal tail shows as a decay costing
// more than the chaos).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "contract.h"

using namespace contract; // NOLINT(google-build-using-namespace): the suite's own helpers

namespace {

struct Phase {
	const char* name;
	long nonfinite = 0, subnormals = 0, blocks = 0;
	float peak = 0.0f;
	double ns = 0.0;
};

[[nodiscard]] double soak_seconds() {
	const char* s = std::getenv("AWVA_SOAK_SECONDS");
	const double v = s ? std::atof(s) : 0.0;
	return v > 0.0 ? v : 60.0;
}

} // namespace

TEST_CASE("soak: chaos, every voice held, the decay: finite, bounded, then silent and idle" * doctest::skip()) {
	const double chaos = soak_seconds();
	for (int p : presets()) {
		CAPTURE(name());
		CAPTURE(preset_label(p));
		sdk::Engine e = fresh(p);
		std::vector<float> preset_values;
		for (int i = 0; i < e.num_params(); i++) preset_values.push_back(*e.param(i));
		Keys k(e.voices());
		Rng r{0x50a1u + static_cast<uint32_t>(p + 1)};
		Phase ph[3] = {{"chaos"}, {"held"}, {"decay"}};
		const int n[3] = {blocks(chaos), blocks(10.0), blocks(info().budget.tail_seconds + 2.0)};
		int last_loud = -1;
		for (int f = 0; f < 3; f++) {
			if (f == 1) { // (the preset's values back: the tail is declared for them)
				for (int i = 0; i < e.num_params(); i++) e.set_param(i, preset_values[static_cast<size_t>(i)]);
				for (int v = 0; v < e.voices(); v++) k.press(v, static_cast<float>(36 + 5 * v), 1.0f);
				k.g = dsp::globals_at_rest;
			}
			if (f == 2) k.release_all();
			for (int b = 0; b < n[f]; b++) {
				k.step();
				if (f == 0) {
					fuzz_controls(r, k);
					if (e.num_params() > 0 && r.next() < 0.05f) {
						const int i = static_cast<int>(r.next() * e.num_params()) % e.num_params();
						e.set_param(i, fuzz_value(r, *e.param_desc(i)));
					}
				} else if (f == 1) {
					const double t = static_cast<double>(b) * dsp::BLOCK / SR;
					for (size_t v = 0; v < k.vc.size(); v++) {
						k.vc[v].but_x = static_cast<float>(0.5 * std::sin(0.2 * t + 2.0 * static_cast<double>(v)));
						k.vc[v].but_y = static_cast<float>(0.8 * std::sin(0.3 * t + static_cast<double>(v)));
					}
				}
				float out[dsp::BLOCK];
				float* const o[1] = {out};
				const auto t0 = std::chrono::steady_clock::now();
				e.process(k.vc.data(), k.g, o);
				ph[f].ns += std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
				ph[f].blocks++;
				for (float x : out) {
					if (!std::isfinite(x)) ph[f].nonfinite++;
					else ph[f].peak = std::max(ph[f].peak, std::fabs(x));
					ph[f].subnormals += subnormal(x);
					if (f == 2 && std::fabs(x) > SILENT) last_loud = b;
				}
			}
		}
		for (const Phase& x : ph) {
			MESSAGE(name() << " preset " << preset_label(p) << " " << std::string(x.name) << ": " << x.blocks << " blocks, peak " << x.peak << ", subnormal samples " << x.subnormals
			               << ", " << x.ns / std::max(1L, x.blocks) << " ns a block");
			CHECK(x.nonfinite == 0);
			CHECK(x.peak <= PEAK_FUZZ);
		}
		const double silent_after = static_cast<double>(last_loud + 1) * dsp::BLOCK / SR;
		CAPTURE(silent_after);
		CHECK(silent_after <= info().budget.tail_seconds);
		CHECK(e.active_voices() == 0);
	}
}
