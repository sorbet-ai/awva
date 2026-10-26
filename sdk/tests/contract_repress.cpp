// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The re-press (docs/conformance.md): a note pressed again while its voice still sounds its release
// (the same note played again, or a key path that gives a slur the voice the note had) is a new
// press. It speaks as a fresh press does, with its strike, its rise and its attack, over the tail.
//   re-press   every preset, on the middle of its tuning range, soft (a press to 0.25 over 80 ms)
//              and firm (to 0.6 over 12 ms). The note is held 0.5 s, released, and pressed again 60
//              or 150 ms later on the same voice, which is still sounding. The re-pressed note must
//              come within 6 dB of the fresh note's sustain (its mean level from 100 to 300 ms after
//              the press) at most 50 ms later than the fresh note does. A tail still that loud
//              counts as having arrived; a slow beat or chorus doesn't matter, only the arrival.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "contract.h"

using namespace contract; // NOLINT(google-build-using-namespace): the suite's own helpers

namespace {

constexpr double HOLD = 0.5;     // s, the first press
constexpr double LOOK = 0.3;     // s, the onset compared
constexpr double SUS_FROM = 0.1; // s, the fresh note's sustain from here to LOOK
constexpr float DROP_DB = 6.0f;
constexpr float LATE_MS = 50.0f;
constexpr int WIN = SR / 200; // 5 ms

// 5 ms power of x from sample a, one value per window, n windows
std::vector<double> power(const std::vector<float>& x, size_t a, int n) {
	std::vector<double> e(static_cast<size_t>(n), 0.0);
	for (int w = 0; w < n; w++) {
		const size_t s = a + static_cast<size_t>(w) * WIN;
		if (s + WIN > x.size()) break;
		double acc = 0.0;
		for (size_t i = s; i < s + WIN; i++) acc += static_cast<double>(x[i]) * x[i];
		e[static_cast<size_t>(w)] = acc / WIN;
	}
	return e;
}

// a press of voice 0 to level over rise seconds from block b0 (to 0: a release over 15 ms), the
// pressure rate as Keys gives it
struct Press {
	int b0 = 0;
	float level = 0.0f, rise = 0.01f;
	void apply(Keys& k, int b, float from) const {
		const float t = static_cast<float>(b - b0 + 1) * dsp::BLOCK / SR;
		NoteControls& c = k.vc[0];
		const float span = level > from ? rise : 0.015f;
		const float p = from + (level - from) * std::min(t / span, 1.0f);
		c.pressure_rate = std::clamp((p - c.pressure) * (static_cast<float>(SR) / dsp::BLOCK) * 0.0125f, -1.0f, 1.0f);
		c.pressure = k.target[0] = p;
		gate_of(c);
	}
};

struct Repress {
	bool sounding = false; // the voice still active at the second press
	float fresh_ms = 0.0f; // when the fresh note comes within DROP_DB of its sustain
	float again_ms = 0.0f; // when the re-pressed one does (LOOK: never)
	float again_db = 0.0f; // the re-pressed note's level over the fresh sustain's window, re the fresh
	[[nodiscard]] float late_ms() const { return again_ms - fresh_ms; }
};

Repress repress(int p, float note, float level, float rise, double gap) {
	const int nw = static_cast<int>(LOOK * SR) / WIN;
	const int b_rel = blocks(HOLD), b_again = blocks(HOLD + gap), n_look = blocks(LOOK + 0.05);
	// the fresh note: pressed from silence
	std::vector<float> fresh;
	{
		sdk::Engine e = contract::fresh(p);
		Keys k(e.voices());
		k.vc[0].pitch = note;
		const Press on{0, level, rise};
		play(e, k, n_look, &fresh, [&](int b) { on.apply(k, b, 0.0f); });
	}
	// the same note held, released, pressed again in its tail
	std::vector<float> again;
	Repress r;
	{
		sdk::Engine e = contract::fresh(p);
		Keys k(e.voices());
		k.vc[0].pitch = note;
		const Press on{0, level, rise}, off{b_rel, 0.0f, 0.0f}, on2{b_again, level, rise};
		play(e, k, b_again, &again, [&](int b) { b < b_rel ? on.apply(k, b, 0.0f) : off.apply(k, b, level); });
		r.sounding = e.active_voices() > 0;
		play(e, k, n_look, &again, [&](int b) { on2.apply(k, b + b_again, 0.0f); });
	}
	const std::vector<double> ef = power(fresh, 0, nw);
	const std::vector<double> ea = power(again, static_cast<size_t>(b_again) * dsp::BLOCK, nw);
	const int w0 = static_cast<int>(SUS_FROM * 200);
	double sf = 0.0, sa = 0.0;
	for (int w = w0; w < nw; w++) sf += ef[static_cast<size_t>(w)], sa += ea[static_cast<size_t>(w)];
	r.again_db = static_cast<float>(10.0 * std::log10((sa + 1e-30) / (sf + 1e-30)));
	const double thr = sf / (nw - w0) * std::pow(10.0, -DROP_DB / 10.0);
	const auto arrival = [&](const std::vector<double>& e) {
		int w = 0;
		while (w < nw && e[static_cast<size_t>(w)] < thr) w++;
		return static_cast<float>(w * 5);
	};
	r.fresh_ms = arrival(ef);
	r.again_ms = arrival(ea);
	return r;
}

} // namespace

TEST_CASE("re-press: a note pressed again in its release tail speaks as a fresh press") {
	struct Case {
		const char* how;
		float level, rise;
		double gap;
	};
	static constexpr Case cases[] = {{"soft", 0.25f, 0.08f, 0.06}, {"soft", 0.25f, 0.08f, 0.15}, {"firm", 0.6f, 0.012f, 0.06}, {"firm", 0.6f, 0.012f, 0.15}};
	for (int p : presets()) {
		const dsp::Tuning& t = dsp::tuning_of(info().conformance, p >= 0 ? entry().preset_name(p) : nullptr);
		const float note = std::round(0.5f * (t.low + t.high));
		Repress r;
		const Case* worst = nullptr;
		for (const Case& c : cases) {
			const Repress q = repress(p, note, c.level, c.rise, c.gap);
			if (worst == nullptr || q.late_ms() > r.late_ms()) r = q, worst = &c;
		}
		const std::string how = std::string(worst->how) + ", again after " + std::to_string(static_cast<int>(worst->gap * 1000)) + " ms" +
		    (r.sounding ? " (sounding)" : " (idle before)");
		CAPTURE(name());
		CAPTURE(preset_label(p));
		CAPTURE(note);
		CAPTURE(how);
		CAPTURE(r.fresh_ms);
		CAPTURE(r.again_ms);
		MESSAGE(name() << " preset " << preset_label(p) << ": note " << note << ", worst " << how << ": arrives " << r.late_ms() << " ms later than the fresh press ("
		               << r.again_ms << " against " << r.fresh_ms << " ms), its sustain " << r.again_db << " dB re the fresh");
		CONFORM("repress", p, r.late_ms() <= LATE_MS);
	}
}
