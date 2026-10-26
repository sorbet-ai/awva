// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The slur (docs/conformance.md): in mono a key pressed while another is held takes the voice
// over as the keys' pressures say, without a click. The keys go through the host's mono player
// (awva/mono.h, the engine's family) as a key path's do. A click: a 1 ms block whose largest
// sample-to-sample jump is over 3x every jump from 2 to 20 ms on both sides of it and over 1 % of
// the render's peak (a buzzing waveform's edges recur within 20 ms, so they don't count; an
// isolated pop does).
//   slur    every preset, on the middle of its tuning range: a line of slurs up and down by 1, 2,
//           3, 5, 7 and 12 semitones, soft (0.25) and firm (0.7), the next key pressed gently (over
//           25 ms) louder or softer than the one held and the last let go 30 ms later, each note
//           held 0.35 s. From each slur's press to 50 ms after the last key lets go, no click (a
//           struck engine's first 20 ms after the new key takes over are its attack).
//   rolls   every preset, by 2, 5 and 12 semitones up and down from the middle: the fingers rolling
//           from one key to the next over 300 ms (slow) and 20 ms (fast), both keys held at one
//           pressure for 400 ms, and the next key pressed hard (a strike: re-articulated) while the
//           last is held. From the press to 400 ms after it, no click (a hard press's first 20 ms
//           are its attack, as are a struck engine's 20 ms after the new key takes over).
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "awva/mono.h"
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
		float ref = 2.5e-8f;
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

// two keys pressed gently: each rises to its level over RISE_S and lets go over FALL_S, the
// pressure rate as the key path gives it (a slur's press, under the player's strike)
struct Gentle {
	static constexpr float RISE_S = 0.025f, FALL_S = 0.015f;
	NoteControls vc[2] = {dsp::note_at_rest, dsp::note_at_rest};
	float target[2] = {0.0f, 0.0f}, top[2] = {0.0f, 0.0f};
	InstrumentControls g = dsp::instrument_at_rest;
	void press(int v, float note, float pres) {
		vc[v].pitch = note;
		target[v] = top[v] = pres;
	}
	void release(int v) { target[v] = 0.0f; }
	void release_all() { target[0] = target[1] = 0.0f; }
	void step() {
		const float dt = static_cast<float>(dsp::BLOCK) / static_cast<float>(SR);
		for (int v = 0; v < 2; v++) {
			const float p = vc[v].pressure, t = target[v], up = top[v] * dt / RISE_S, down = top[v] * dt / FALL_S;
			const float np = p < t ? std::min(t, p + up) : std::max(t, p - down);
			vc[v].pressure_rate = std::clamp((np - p) / dt * 0.0125f, -1.0f, 1.0f);
			vc[v].pressure = np;
			gate_of(vc[v]);
		}
	}
};

// a line of slurs from note: the keys alternate (key 0, key 1, ...), each pressed gently to its
// level while the last is held
Line line(int p, float note, float level, float louder) {
	static constexpr int steps[] = {2, -2, 5, -5, 1, -1, 7, -7, 3, -3, 12, -12};
	constexpr int nsteps = static_cast<int>(sizeof steps / sizeof steps[0]);
	sdk::Engine e = contract::fresh(p);
	Gentle k;
	awva::voices::Mono<16> mono(awva::voices::MonoStyle::of(info().mono));
	std::vector<NoteControls> vc(static_cast<size_t>(e.voices()), dsp::note_at_rest);
	const int b_note = blocks(NOTE_S), b_over = blocks(OVERLAP_S);
	const int n = b_note * (nsteps + 1) + blocks(0.5);
	std::vector<int> press_b, lift_b, take_b; // each slur's: the press, the last key's lift, when the voice reached the note
	std::vector<float> to;                    // each slur's note
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
			if (i > 0) press_b.push_back(b), to.push_back(pitch), take_b.push_back(-1);
		}
		if (at == b_over && i > 0 && i <= nsteps) {
			k.release((i + 1) % 2);
			lift_b.push_back(b);
		}
		if (i == nsteps + 1 && at == 0) k.release_all();
		k.step();
		for (int key = 0; key < 2; key++) mono.note(key, k.vc[key]); // (the gate with the pressure)
		mono.step(static_cast<float>(dsp::BLOCK) / static_cast<float>(SR));
		vc[0] = mono.voice();
		if (!take_b.empty() && take_b.back() < 0 && vc[0].pitch == to.back()) take_b.back() = b;
		float buf[dsp::BLOCK];
		float* const o[1] = {buf};
		e.process(vc.data(), k.g, o);
		out.insert(out.end(), buf, buf + dsp::BLOCK);
	}
	Line r;
	r.slurs = static_cast<int>(press_b.size());
	const std::vector<int> ck = clicks(out);
	for (size_t j = 0; j < press_b.size(); j++) {
		int a = press_b[j] * dsp::BLOCK / MS;
		if (info().mono == dsp::MonoFamily::struck && take_b[j] >= 0) a = std::max(a, take_b[j] * dsp::BLOCK / MS + 20); // (a struck engine's takeover is its attack)
		const int z = (lift_b[j] * dsp::BLOCK + static_cast<int>(AFTER_S * SR)) / MS;
		for (int c : ck)
			if (c >= a && c <= z) {
				if (r.clicks++ == 0) r.where = "the slur by " + std::to_string(steps[j]) + " at " + std::to_string(press_b[j] * dsp::BLOCK / MS) + " ms, " + std::to_string(c - a) + " ms after its press";
			}
	}
	return r;
}

} // namespace

TEST_CASE("slur: mono slurs, up and down, soft and firm, without a click") {
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

namespace {

// two keys rolled, held or struck from note to note + by: the voice through the player; the clicks
// from B's press to 400 ms after it (after `skip_ms` of it)
int roll(int p, float note, float by, const char* how) {
	sdk::Engine e = contract::fresh(p);
	awva::voices::Mono<16> mono(awva::voices::MonoStyle::of(info().mono));
	std::vector<NoteControls> vc(static_cast<size_t>(e.voices()), dsp::note_at_rest);
	const std::string h = how;
	constexpr float L = 0.6f;
	const float dt = static_cast<float>(dsp::BLOCK) / static_cast<float>(SR);
	const double tb = 0.6; // B's press
	const int n = blocks(tb + 1.2);
	float pa = 0.0f, pb = 0.0f;
	int take_ms = -1; // (when the voice reached B's note)
	std::vector<float> out;
	out.reserve(static_cast<size_t>(n) * dsp::BLOCK);
	const auto ramp = [](double t, double t0, double t1, float a, float b) {
		if (t <= t0) return a;
		if (t >= t1) return b;
		return a + (b - a) * static_cast<float>((t - t0) / (t1 - t0));
	};
	for (int b = 0; b < n; b++) {
		const double t = static_cast<double>(b) * dt;
		float na = ramp(t, 0.0, 0.08, 0.0f, L), nb = 0.0f;
		if (h == "slow" || h == "fast") {
			const double d = h == "slow" ? 0.3 : 0.02;
			nb = ramp(t, tb, tb + d, 0.0f, L);
			if (t > tb) na = ramp(t, tb, tb + d, L, 0.0f);
		} else if (h == "both") {
			nb = ramp(t, tb, tb + 0.1, 0.0f, L);
			if (t > tb + 0.5) na = ramp(t, tb + 0.5, tb + 0.6, L, 0.0f);
		} else { // hard: a strike while A is held
			nb = ramp(t, tb, tb + 0.01, 0.0f, L);
			if (t > tb + 0.1) na = ramp(t, tb + 0.1, tb + 0.12, L, 0.0f);
		}
		if (t > tb + 0.8) nb = ramp(t, tb + 0.8, tb + 0.85, L, 0.0f);
		const float ra = (na - pa) / dt * 0.0125f, rb = (nb - pb) / dt * 0.0125f; // (the key path's rate)
		pa = na, pb = nb;
		mono.note(0, {note, pa, std::clamp(ra, -1.0f, 1.0f), 0.0f, 0.0f, 0.0f, static_cast<uint8_t>(pa > 0.0f)});
		mono.note(1, {note + by, pb, std::clamp(rb, -1.0f, 1.0f), 0.0f, 0.0f, 0.0f, static_cast<uint8_t>(pb > 0.0f)});
		mono.step(dt);
		vc[0] = mono.voice();
		if (take_ms < 0 && vc[0].pitch == note + by) take_ms = static_cast<int>(t * 1000);
		float buf[dsp::BLOCK];
		float* const o[1] = {buf};
		e.process(vc.data(), dsp::instrument_at_rest, o);
		out.insert(out.end(), buf, buf + dsp::BLOCK);
	}
	int a = static_cast<int>(tb * 1000) + (h == "hard" ? 20 : 0);
	if (info().mono == dsp::MonoFamily::struck) a = std::max(a, take_ms + 20); // (a struck engine's takeover is its attack)
	const int z = static_cast<int>(tb * 1000) + 400;
	int c = 0;
	for (int k : clicks(out)) c += k >= a && k <= z;
	return c;
}

} // namespace

TEST_CASE("slur: mono rolls slow and fast, both keys held, a hard press, without a click") {
	static constexpr const char* hows[] = {"slow", "fast", "both", "hard"};
	static constexpr float bys[] = {2.0f, -2.0f, 5.0f, -5.0f, 12.0f, -12.0f};
	for (int p : presets()) {
		const dsp::Tuning& t = dsp::tuning_of(info().conformance, p >= 0 ? entry().preset_name(p) : nullptr);
		const float mid = std::round(0.5f * (t.low + t.high));
		int clicks = 0, cases = 0;
		std::string first;
		for (const char* how : hows)
			for (float by : bys) {
				const float note = mid - 0.5f * by; // (the two notes about the middle)
				const int c = roll(p, note, by, how);
				cases++;
				if (c > 0 && first.empty()) first = std::string(how) + " by " + std::to_string(static_cast<int>(by));
				clicks += c;
			}
		CAPTURE(name());
		CAPTURE(preset_label(p));
		CAPTURE(first);
		MESSAGE(name() << " preset " << preset_label(p) << ": " << clicks << " clicks in " << cases << " rolls" << (first.empty() ? "" : "; the first: " + first));
		CONFORM("roll", p, clicks == 0);
	}
}
