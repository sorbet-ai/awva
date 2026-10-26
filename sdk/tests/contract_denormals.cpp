// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// denormals: the engine's tail doesn't stay in subnormal floats (docs/conformance.md). On a PC a
// decaying state in the subnormal range costs tens of cycles an operation, so an engine left in a
// subnormal tail spends many times its silent cost on nothing anyone hears.
//   raw       the engine in the host's own FPU mode (IEEE): after the release tail, its subnormal
//             samples and their cost are reported, not failed (the agreed design is the host's
//             guard: the component's process() flushes subnormals)
//   guarded   under the host's guard (nsc::ScopedNoDenormals, as the component's process() runs):
//             after the tail no subnormal sample and nothing above -120 dBFS (whether it is
//             exactly 0 is reported). That the guard leaves the samples the same in 24-bit PCM is
//             contract.cpp's FTZ guard check.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#include "contract.h"
#include "nsc/no_denormals.h"

using namespace contract; // NOLINT(google-build-using-namespace): the suite's own helpers

namespace {

struct Tail {
	long nonzero = 0, subnormal = 0; // samples in the second after the tail
	float worst = 0.0f;              // the largest of them
	double cost = 0.0;               // a block after the tail against a held block (time)
	std::vector<float> out;          // the whole render
};

// the phrase (released at 1.5 s), then the tail and 1 s more: the last second
Tail after_tail(int p) {
	const float tail = info().tail_seconds;
	sdk::Engine e = fresh(p);
	Keys k(e.voices());
	const int from = blocks(1.5 + tail), n = blocks(1.5 + tail + 1.0);
	Tail t;
	double held = 0.0, after = 0.0;
	int nheld = 0;
	for (int b = 0; b < n; b++) {
		k.step();
		phrase(k, b);
		float out[dsp::BLOCK];
		float* const o[1] = {out};
		const auto t0 = std::chrono::steady_clock::now();
		e.process(k.vc.data(), k.g, o);
		const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
		t.out.insert(t.out.end(), out, out + dsp::BLOCK);
		if (b >= blocks(0.5) && b < blocks(1.4)) held += ns, nheld++;
		if (b < from) continue;
		after += ns;
		for (float x : out) {
			t.nonzero += x != 0.0f;
			t.subnormal += subnormal(x);
			t.worst = std::max(t.worst, std::fabs(x));
		}
	}
	t.cost = held > 0.0 ? (after / (n - from)) / (held / nheld) : 0.0;
	return t;
}

} // namespace

TEST_CASE("denormals: no subnormal tail under the host's FTZ guard, the raw tail reported") {
	int stuck = 0, not_zero = 0;
	long most = 0;
	double cost = 0.0;
	std::vector<Tail> raw;
	for (int p : presets()) {
		raw.push_back(after_tail(p));
		const Tail& r = raw.back();
		if (r.subnormal > 0) stuck++;
		most = std::max(most, r.subnormal);
		cost = std::max(cost, r.cost);
	}
	MESSAGE(name() << " raw [denormals.raw]: " << stuck << " of " << presets().size() << " presets end in a subnormal tail (up to " << most
	               << " subnormal samples a second); a block after the tail costs up to " << cost << " x a held block");
#if !defined(__x86_64__) && !defined(_M_X64) && !defined(__aarch64__)
	MESSAGE("the guard flushes nothing on this CPU: the guarded check is skipped");
	return;
#endif
	for (size_t i = 0; i < presets().size(); i++) {
		const int p = presets()[i];
		CAPTURE(name());
		CAPTURE(preset_label(p));
		Tail g;
		{
			const nsc::ScopedNoDenormals guard;
			g = after_tail(p);
		}
		CAPTURE(g.nonzero);
		CAPTURE(g.worst);
		const bool quiet = g.subnormal == 0 && g.worst < INAUDIBLE;
		CONFORM("denormals", p, quiet);
		if (g.nonzero > 0) not_zero++;
	}
	MESSAGE(name() << " guarded: " << not_zero << " of " << presets().size() << " presets leave tiny normal samples after the tail (not exactly 0)");
}
