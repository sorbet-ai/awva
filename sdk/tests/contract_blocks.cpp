// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// blocks: through the SDK's Shell (shell.h, a plugin's host side) the size of the host's calls
// makes no difference: MIDI 2.0 and MPE events at their frames, played in calls of 1, 7, 32, 256
// frames and of random sizes, give the same samples bit for bit (the Shell promises it: an event
// is taken at the first control step at or after its frame, timed_midi.h). The first and last preset.
// (The component's own host side, late events included, is awva_conformance's "blocks".)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "awva_sdk/shell.h"
#include "contract.h"

using namespace contract; // NOLINT(google-build-using-namespace): the suite's own helpers

namespace {

struct Msg {
	int64_t frame;
	uint32_t w[2];
	int len; // MIDI 1.0 bytes in w[0] (1..3), or 0: a UMP packet
};

// MPE notes in MIDI 1.0 with pressure, bends and CC74, MIDI 2.0 notes with theirs, the pedal: off
// the step grid and off block starts
std::vector<Msg> messages() {
	std::vector<Msg> m;
	const auto at = [](double s) { return static_cast<int64_t>(std::llround(s * SR)); };
	const auto b1 = [](uint32_t s, uint32_t d1, uint32_t d2) { return s | d1 << 8 | d2 << 16; };
	for (uint32_t n = 0; n < 4; n++) {
		const uint32_t ch = 1 + n;
		const double on = 0.0071 + 0.31 * n, off = on + 1.1;
		m.push_back({at(on), {b1(0x90 | ch, 50 + 5 * n, 70 + 12 * n), 0}, 3});
		for (int k = 1; on + 0.0137 * k < off; k++) {
			const double t = on + 0.0137 * k;
			m.push_back({at(t), {b1(0xd0 | ch, static_cast<uint32_t>(40 + (k * 3) % 80), 0), 0}, 2});
			const auto bend = static_cast<uint32_t>(8192 + static_cast<int>(700 * std::sin(0.2 * k)));
			m.push_back({at(t + 0.0043), {b1(0xe0 | ch, bend & 0x7f, (bend >> 7) & 0x7f), 0}, 3});
			if (k % 4 == 0) m.push_back({at(t + 0.0091), {b1(0xb0 | ch, 74, static_cast<uint32_t>(64 + 3 * k) & 0x7f), 0}, 3});
		}
		m.push_back({at(off), {b1(0x80 | ch, 50 + 5 * n, 40), 0}, 3});
	}
	for (uint32_t n = 0; n < 3; n++) { // MIDI 2.0: velocity, channel pressure, a release
		const uint32_t ch = 8 + n, key = 60 + 4 * n;
		m.push_back({at(1.6 + 0.013 * n), {0x40900000u | ch << 16 | key << 8, (0x8000u + 0x1000u * n) << 16}, 0});
		for (int k = 0; k < 30; k++) m.push_back({at(1.65 + 0.021 * k), {0x40d00000u | ch << 16, 0x20000000u + 0x03000000u * static_cast<uint32_t>(k)}, 0});
		m.push_back({at(2.4 + 0.017 * n), {0x40800000u | ch << 16 | key << 8, 0x40000000u}, 0});
	}
	m.push_back({at(1.9), {b1(0xb0, 64, 127), 0}, 3});
	m.push_back({at(2.7), {b1(0xb0, 64, 0), 0}, 3});
	std::stable_sort(m.begin(), m.end(), [](const Msg& a, const Msg& b) { return a.frame < b.frame; });
	return m;
}

// the Shell in calls of `call` frames (0: random 1..1024), each message given in the call its
// frame falls in, at its offset there
std::vector<float> through_shell(int preset, const std::vector<Msg>& msgs, int frames, int call) {
	sdk::Shell s(entry(), sdk::HostConfig{SR, 0, nvoices()}, false);
	if (preset >= 0) s.set_preset(preset);
	std::vector<float> l(static_cast<size_t>(frames)), r(static_cast<size_t>(frames));
	size_t k = 0;
	uint32_t rng = 777u;
	for (int at = 0; at < frames;) {
		rng = rng * 1664525u + 1013904223u;
		const int n = std::min(call > 0 ? call : 1 + static_cast<int>((rng >> 8) % 1024), frames - at);
		for (; k < msgs.size() && msgs[k].frame < at + n; k++) {
			const int off = static_cast<int>(msgs[k].frame - at);
			if (msgs[k].len > 0) {
				uint8_t b[3];
				for (int i = 0; i < 3; i++) b[i] = static_cast<uint8_t>(msgs[k].w[0] >> (8 * i));
				s.midi(b, msgs[k].len, off);
			} else {
				s.ump(msgs[k].w, off);
			}
		}
		s.process(&l[static_cast<size_t>(at)], &r[static_cast<size_t>(at)], n);
		at += n;
	}
	return l;
}

} // namespace

TEST_CASE("blocks: through the Shell, any call size gives the same samples bit for bit") {
	const std::vector<Msg> msgs = messages();
	const int frames = static_cast<int>(3.2 * SR);
	for (int p : {presets().front(), presets().back()}) {
		const std::vector<float> ref = through_shell(p, msgs, frames, 32);
		float peak = 0.0f;
		for (float x : ref) peak = std::max(peak, std::fabs(x));
		CAPTURE(name());
		CAPTURE(preset_label(p));
		CHECK(peak > 1e-3f);
		for (int call : {1, 7, 256, 0}) {
			CAPTURE(call);
			CHECK(same_bits(through_shell(p, msgs, frames, call), ref));
		}
	}
}
