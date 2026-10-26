// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// rates: at every rate a component takes (nsc_desc min_rate .. max_rate: from the engine's min_rate,
// 11025 unless its traits declare a higher one, to 192000) the engine is finite, bounded, sounds, is
// silent within its tail, and holds its pitch at 48 kHz within
// EngineInfo::conformance.rate_cents (3 by default) or, at a rate its traits list in
// rate_tolerances (a known fault, measured, with its reason), within that rate's bound
// (docs/conformance.md). The rate test of contract.cpp, moved here unchanged; its exemptions are now
// the engines' rate_tolerances, with the same bounds and reasons.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

#include "contract.h"

using namespace contract; // NOLINT(google-build-using-namespace): the suite's own helpers

namespace {

constexpr int kRates[] = {11025, 22050, 44100, 48000, 48828, 88200, 96000, 192000};

// note 69 held at 0.6 for 1 s (the keys glide as Keys does, in seconds), then released; the samples
// of 0.5..1.0 s for the pitch, and the release's last loud sample
struct RateRun {
	std::vector<float> held;
	Stats s;
	float silent_after = 0.0f; // seconds after the release
};
RateRun run_at(int rate, int preset) {
	sdk::Engine e(entry());
	e.init(rate, nvoices());
	if (preset >= 0) e.set_preset(preset);
	std::vector<NoteControls> vc(static_cast<size_t>(e.voices()), dsp::note_at_rest);
	InstrumentControls g = dsp::instrument_at_rest;
	const float dt = static_cast<float>(dsp::BLOCK) / static_cast<float>(rate); // a block, s
	const int held_blocks = static_cast<int>(1.0f / dt), total = static_cast<int>((1.0f + info().tail_seconds + 0.5f) / dt);
	RateRun r;
	vc[0].pitch = 69.0f;
	for (int b = 0; b < total; b++) {
		NoteControls& c = vc[0];
		const float target = b < held_blocks ? 0.6f : 0.0f;
		const float step = dt / 0.02f; // 1 per 20 ms
		const float np = c.pressure < target ? std::min(target, c.pressure + step) : std::max(target, c.pressure - step);
		c.pressure_rate = std::clamp((np - c.pressure) / dt * 0.0125f, -1.0f, 1.0f);
		c.pressure = np;
		for (NoteControls& v : vc) gate_of(v);
		float out[dsp::BLOCK];
		float* const o[1] = {out};
		e.process(vc.data(), g, o);
		for (float x : out) {
			if (!std::isfinite(x)) r.s.nonfinite++;
			else {
				r.s.peak = std::max(r.s.peak, std::fabs(x));
				if (std::fabs(x) > SILENT && b >= held_blocks) r.silent_after = static_cast<float>(b + 1 - held_blocks) * dt;
			}
		}
		if (b >= held_blocks / 2 && b < held_blocks) r.held.insert(r.held.end(), out, out + dsp::BLOCK);
	}
	return r;
}

// the fundamental in Hz by autocorrelation (the first lag with at least 0.9 of the best
// correlation over 100..2000 Hz, interpolated); 0 if nothing periodic (best below 0.5)
float pitch_hz(const std::vector<float>& x, int rate) {
	const int lo = rate / 2000, hi = rate / 100, n = static_cast<int>(x.size()) - hi;
	if (n <= 0) return 0.0f;
	std::vector<double> r(static_cast<size_t>(hi + 2), 0.0);
	double e0 = 0.0;
	for (int i = 0; i < n; i++) e0 += static_cast<double>(x[static_cast<size_t>(i)]) * x[static_cast<size_t>(i)];
	if (e0 <= 0.0) return 0.0f;
	double best = 0.0;
	for (int lag = lo; lag <= hi + 1; lag++) {
		double s = 0.0;
		for (int i = 0; i < n; i++) s += static_cast<double>(x[static_cast<size_t>(i)]) * x[static_cast<size_t>(i) + static_cast<size_t>(lag)];
		r[static_cast<size_t>(lag)] = s / e0;
		if (lag <= hi) best = std::max(best, r[static_cast<size_t>(lag)]);
	}
	if (best < 0.5) return 0.0f;
	for (int lag = lo + 1; lag <= hi; lag++) {
		const size_t k = static_cast<size_t>(lag); // (lag > lo >= 0)
		const double a = r[k - 1], b = r[k], c = r[k + 1];
		if (b >= 0.9 * best && b >= a && b >= c) {
			const double d = a - 2 * b + c, off = d != 0.0 ? 0.5 * (a - c) / d : 0.0;
			return static_cast<float>(rate / (lag + off));
		}
	}
	return 0.0f;
}

} // namespace

TEST_CASE("rates: at every rate from min_rate to max_rate, finite, bounded, within the declared cents of 48 kHz, silent within the tail") {
	const float tail = info().tail_seconds;
	float worst[std::size(kRates)] = {}; // cents from 48 kHz, every preset's worst (the log's table)
	bool unpitched[std::size(kRates)] = {};
	for (int p : presets()) {
		const RateRun ref = run_at(48000, p);
		const float f48 = pitch_hz(ref.held, 48000);
		for (size_t i = 0; i < std::size(kRates); i++) {
			const int rate = kRates[i];
			if (rate < info().min_rate) continue; // (below the rates it is made for: its component refuses them)
			const dsp::RateTolerance* x = dsp::rate_tolerance(info().conformance, rate);
			CAPTURE(std::string(info().name));
			CAPTURE(p);
			CAPTURE(rate);
			const RateRun r = run_at(rate, p);
			CHECK(r.s.nonfinite == 0);
			if (!(x && x->may_not_sound)) CHECK(r.s.peak > 1e-3f); // it sounds
			CHECK(r.s.peak < PEAK_PLAYING);
			CHECK(r.silent_after <= tail);
			if (f48 > 0.0f) { // a periodic note at 48 kHz: the same pitch within rate_cents (or the rate's tolerance)
				const float f = pitch_hz(r.held, rate);
				CAPTURE(f48);
				CAPTURE(f);
				if (f <= 0.0f) {
					unpitched[i] = true;
					if (!(x && x->may_not_sound)) CHECK(f > 0.0f);
					continue;
				}
				const float cents = std::fabs(1200.0f * std::log2(f / f48));
				worst[i] = std::max(worst[i], cents);
				CHECK(cents < (x ? x->cents : info().conformance.rate_cents));
			}
		}
	}
	std::string table;
	for (size_t i = 0; i < std::size(kRates); i++) {
		char s[48];
		if (kRates[i] < info().min_rate) std::snprintf(s, sizeof s, " %d:(refused)", kRates[i]);
		else std::snprintf(s, sizeof s, " %d:%s%.1f", kRates[i], unpitched[i] ? "(no pitch) " : "", static_cast<double>(worst[i]));
		table += s;
	}
	MESSAGE(std::string(info().name) << ": cents from 48 kHz, every preset's worst:" << table);
}
