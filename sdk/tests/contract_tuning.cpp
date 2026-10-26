// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// tuning: a held note's sustained pitch is its note's (12-TET, A4 = 440 Hz, or an octave of it)
// within the engine's declared tolerance, on the notes of its declared range; for a preset
// declared inharmonic (Tuning::partial: a bell, a glass) the declared partial's frequency, a
// spectral peak, instead of the period (EngineInfo::
// conformance: tuning, preset_tuning; docs/conformance.md). Every preset; the notes from the
// range's low end every 6 semitones and its high end; each pressed to 0.6 and held 1.5 s, its pitch
// measured over the second half.
#include <cmath>
#include <string>
#include <vector>

#include "contract.h"

using namespace contract; // NOLINT(google-build-using-namespace): the suite's own helpers

namespace {
[[nodiscard]] std::vector<float> tuning_notes(const dsp::Tuning& t) {
	std::vector<float> n;
	for (float x = t.low; x < t.high; x += 6.0f) n.push_back(x);
	n.push_back(t.high);
	return n;
}
} // namespace

TEST_CASE("tuning: the measures read a tone's detune (a harmonic tone's period, an inharmonic tone's partial)") {
	// a tone 7 cents sharp of A3 (220 Hz): harmonics 1..4, and with an inharmonic partial at 2.32
	const double f = 220.0 * std::pow(2.0, 7.0 / 1200.0);
	std::vector<float> harmonic(SR * 3 / 4), inharmonic(harmonic.size());
	for (size_t i = 0; i < harmonic.size(); i++) {
		const double t = static_cast<double>(i) / SR;
		double h = 0.0;
		for (int k = 1; k <= 4; k++) h += std::sin(2.0 * M_PI * k * f * t) / k;
		harmonic[i] = static_cast<float>(0.3 * h);
		inharmonic[i] = static_cast<float>(0.3 * std::sin(2.0 * M_PI * f * t) + 0.4 * std::sin(2.0 * M_PI * 2.32 * f * t));
	}
	double corr = 0.0;
	bool found = false;
	CHECK(std::fabs(cents_from(harmonic, SR, 220.0, &corr) - 7.0) < 0.2);
	CHECK(corr > 0.9);
	CHECK(std::fabs(partial_cents(inharmonic, SR, 220.0, &found) - 7.0) < 0.2);
	CHECK(found);
}

TEST_CASE("tuning: a held note sounds its pitch within the declared cents (12-TET, A4 = 440 Hz)") {
	for (int p : presets()) {
		const dsp::Tuning& t = dsp::tuning_of(info().conformance, p >= 0 ? entry().preset_name(p) : nullptr);
		float worst = 0.0f, worst_note = 0.0f;
		double least_corr = 1.0;
		for (float note : tuning_notes(t)) {
			sdk::Engine e = fresh(p);
			const std::vector<float> x = held_note(e, note, 0.6f, 1.5);
			double corr = 0.0;
			const double f = 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
			float cents = 0.0f;
			if (t.partial > 0.0f) { // an inharmonic tone: its partial at t.partial of the note, a spectral peak
				bool found = false;
				cents = static_cast<float>(partial_cents(x, SR, f * t.partial, &found));
				corr = found ? 1.0 : 0.0;
			} else { // a harmonic tone: its period
				cents = static_cast<float>(cents_from(x, SR, f, &corr));
			}
			least_corr = std::min(least_corr, corr);
			if (corr < 0.5 || std::fabs(cents) > std::fabs(worst)) worst = corr < 0.5 ? INFINITY : cents, worst_note = note;
		}
		// one verdict a preset: its worst note (a pitch to measure, within the declared cents)
		CAPTURE(name());
		CAPTURE(preset_label(p));
		CAPTURE(worst_note);
		CAPTURE(worst);
		CAPTURE(least_corr);
		const bool in_tune = least_corr >= 0.5 && std::fabs(worst) <= t.cents;
		CONFORM("tuning", p, in_tune);
		MESSAGE(name() << " preset " << preset_label(p) << ": worst " << worst << " cents at note " << worst_note << " (declared " << t.cents << " c, notes " << t.low << " .. "
		               << t.high << (t.partial > 0.0f ? ", the partial at " + std::to_string(t.partial) + " of the note" : std::string()) << ")");
	}
}
