// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The slur (docs/conformance.md): in mono legato a note pressed while another is held takes the
// voice over without an attack, and without a click. The keys go through the host's legato
// (awva/legato.h) as a key path's do: the next key goes down while the last is held and the last
// lets go 30 ms later.
//   slur   every preset, on the middle of its tuning range: a line of slurs up and down by 1, 2, 3,
//          5, 7 and 12 semitones, soft (0.25) and firm (0.7), the next key pressed louder or softer
//          than the one held, each note held 0.35 s. From each slur's press to 50 ms after the last
//          key lets go, no click: no 1 ms block whose largest sample-to-sample jump is over 3x
//          every jump from 2 to 20 ms on both sides of it and over 1 % of the render's peak (a
//          buzzing waveform's edges recur within 20 ms, so they don't count; an isolated pop does).
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "awva/legato.h"
#include "contract.h"

using namespace contract; // NOLINT(google-build-using-namespace): the suite's own helpers

namespace {

constexpr double NOTE_S = 0.35;    // s, each note
constexpr double OVERLAP_S = 0.03; // s, both keys down
constexpr double AFTER_S = 0.05;   // s, a slur's scope after the last key lets go
constexpr int MS = SR / 1000;
constexpr float RATIO = 3.0f, FLOOR = 0.01f;
constexpr int NEAR_MS = 2, FAR_MS = 20;

// the clicks of x (ms from its start): 1 ms blocks whose largest jump is over RATIO x every block's
// from NEAR_MS to FAR_MS on both sides, and over FLOOR of the peak
std::vector<int> clicks(const std::vector<float>& x) {
	const int nb = static_cast<int>(x.size()) / MS - 1;
	std::vector<float> bm(static_cast<size_t>(std::max(nb, 0)), 0.0f);
	float peak = 0.0f;
	for (float v : x) peak = std::max(peak, std::fabs(v));
	for (int b = 0; b < nb; b++)
		for (int i = b * MS; i < (b + 1) * MS; i++) bm[static_cast<size_t>(b)] = std::max(bm[static_cast<size_t>(b)], std::fabs(x[i + 1] - x[i]));
	std::vector<int> out;
	for (int b = 0; b < nb; b++) {
		float ref = 1e-7f;
		for (int d = NEAR_MS; d <= FAR_MS; d++) {
			if (b - d >= 0) ref = std::max(ref, bm[static_cast<size_t>(b) - static_cast<size_t>(d)]);
			if (b + d < nb) ref = std::max(ref, bm[static_cast<size_t>(b) + static_cast<size_t>(d)]);
		}
		const float m = bm[static_cast<size_t>(b)];
		if (m > RATIO * ref && m > FLOOR * std::max(peak, 1e-9f) && (out.empty() || b - out.back() > 2)) out.push_back(b);
	}
	return out;
}

struct Line {
	int clicks = 0, slurs = 0;
	std::string where; // the first click: which slur, how long after its press
};

// a line of slurs from note: the keys alternate (key 0, key 1, ...), each pressed to its level
// while the last is held
Line line(int p, float note, float level, float louder) {
	static constexpr int steps[] = {2, -2, 5, -5, 1, -1, 7, -7, 3, -3, 12, -12};
	constexpr int nsteps = static_cast<int>(sizeof steps / sizeof steps[0]);
	sdk::Engine e = contract::fresh(p);
	Keys k(2);
	awva::voices::Legato<16> legato;
	std::vector<NoteControls> vc(static_cast<size_t>(e.voices()), dsp::note_at_rest);
	const int b_note = blocks(NOTE_S), b_over = blocks(OVERLAP_S);
	const int n = b_note * (nsteps + 1) + blocks(0.5);
	std::vector<int> press_b, lift_b; // each slur's
	float pitch = note;
	std::vector<float> out;
	out.reserve(static_cast<size_t>(n) * dsp::BLOCK);
	for (int b = 0; b < n; b++) {
		const int i = b / b_note, at = b % b_note;
		if (at == 0 && i <= nsteps) {
			const int key = i % 2;
			if (i > 0) pitch += static_cast<float>(steps[i - 1]);
			// the next key a little louder or softer than the held one, so its rise crosses it or not
			k.press(key, pitch, std::min(i > 0 && i % 2 == 1 ? level * louder : level, 1.0f));
			if (i > 0) press_b.push_back(b);
		}
		if (at == b_over && i > 0 && i <= nsteps) {
			k.release((i + 1) % 2);
			lift_b.push_back(b);
		}
		if (i == nsteps + 1 && at == 0) k.release_all();
		k.step();
		for (int key = 0; key < 2; key++) {
			legato.note(key, k.vc[static_cast<size_t>(key)]); // (Keys keep the gate with the pressure)
		}
		NoteControls& v = vc[0];
		if (legato.empty()) {
			v.pressure = 0.0f;
			v.pressure_rate = std::min(v.pressure_rate, 0.0f);
			v.gate = 0;
		} else {
			v = legato.signals();
			v.gate = 1;
		}
		float buf[dsp::BLOCK];
		float* const o[1] = {buf};
		e.process(vc.data(), k.g, o);
		out.insert(out.end(), buf, buf + dsp::BLOCK);
	}
	Line r;
	r.slurs = static_cast<int>(press_b.size());
	const std::vector<int> ck = clicks(out);
	for (size_t j = 0; j < press_b.size(); j++) {
		const int a = press_b[j] * dsp::BLOCK / MS, z = (lift_b[j] * dsp::BLOCK + static_cast<int>(AFTER_S * SR)) / MS;
		for (int c : ck)
			if (c >= a && c <= z) {
				if (r.clicks++ == 0) r.where = "the slur by " + std::to_string(steps[j]) + " at " + std::to_string(press_b[j] * dsp::BLOCK / MS) + " ms, " + std::to_string(c - a) + " ms after its press";
			}
	}
	return r;
}

} // namespace

TEST_CASE("slur: mono-legato slurs, up and down, soft and firm, without a click") {
	struct Case {
		const char* how;
		float level, louder;
	};
	static constexpr Case cases[] = {{"soft", 0.25f, 1.4f}, {"soft", 0.25f, 0.7f}, {"firm", 0.7f, 1.3f}, {"firm", 0.7f, 0.8f}};
	for (int p : presets()) {
		const dsp::Tuning& t = dsp::tuning_of(info().conformance, p >= 0 ? entry().preset_name(p) : nullptr);
		const float note = std::round(0.5f * (t.low + t.high)) - 6.0f; // (the line goes up to +6 and down to -6 of it)
		int clicks = 0, slurs = 0;
		std::string first;
		for (const Case& c : cases) {
			const Line l = line(p, note, c.level, c.louder);
			slurs += l.slurs;
			if (l.clicks > 0 && first.empty()) first = std::string(c.how) + (c.louder > 1.0f ? ", the next key louder: " : ", the next key softer: ") + l.where;
			clicks += l.clicks;
		}
		CAPTURE(name());
		CAPTURE(preset_label(p));
		CAPTURE(first);
		MESSAGE(name() << " preset " << preset_label(p) << ": " << clicks << " clicks in " << slurs << " slurs" << (first.empty() ? "" : "; the first: " + first));
		CONFORM("slur", p, clicks == 0);
	}
}
