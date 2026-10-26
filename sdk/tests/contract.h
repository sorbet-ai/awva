// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The contract suite's shared parts (docs/conformance.md): the engine under test, the keys and
// the phrases that play it, the measurements, and the known failures. Every contract_*.cpp and
// contract.cpp is compiled once per engine into awva_engine_<name>_contract, linked to that
// engine's library (AWVA_ENGINE_ENTRY names its entry; sdk/CMakeLists.txt).
#ifndef AWVA_SDK_TESTS_CONTRACT_H_
#define AWVA_SDK_TESTS_CONTRACT_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "doctest/doctest.h"
#include "awva_sdk/engines.h"

const awva::sdk::EngineEntry& AWVA_ENGINE_ENTRY();

namespace contract {

namespace dsp = awva;
namespace sdk = awva::sdk;
using dsp::GlobalControls;
using dsp::VoiceControls;

constexpr int SR = 48000;
constexpr float SILENT = 1e-4f;       // the render checks' silence
constexpr float PEAK_PLAYING = 32.0f; // every voice at full pressure (a host at x0.25 volume clips at 4)
constexpr float PEAK_FUZZ = 64.0f;    // extreme but legal parameters may be loud: only blow-ups fail

[[nodiscard]] inline const sdk::EngineEntry& entry() { return AWVA_ENGINE_ENTRY(); }
[[nodiscard]] inline const dsp::EngineInfo& info() { return *entry().info; }
[[nodiscard]] inline std::string name() { return info().name; }
[[nodiscard]] inline int nvoices() { return std::min(10, info().budget.max_voices); }
[[nodiscard]] inline int blocks(double s, int rate = SR) { return static_cast<int>(s * rate / dsp::BLOCK); }

// every preset, or -1 for an engine without presets
[[nodiscard]] inline std::vector<int> presets() {
	std::vector<int> p;
	p.reserve(static_cast<size_t>(info().num_presets) + 1);
	for (int i = 0; i < info().num_presets; i++) p.push_back(i);
	if (p.empty()) p.push_back(-1);
	return p;
}
[[nodiscard]] inline std::string preset_label(int p) { return p >= 0 ? std::to_string(p) + " " + entry().preset_name(p) : std::string("(none)"); }

[[nodiscard]] inline sdk::Engine fresh(int preset, int rate = SR) {
	sdk::Engine e(entry());
	e.init(rate, nvoices());
	if (preset >= 0) e.set_preset(preset);
	return e;
}

// --- known failures (sdk/tests/known_failures.cmake) --------------------------------------------
// A check that an engine is known to fail is reported, not failed, until its owner fixes it. Its
// tag is the check's ("tuning") or the check's on one preset ("tuning:flute"); the list comes from
// AWVA_KNOWN_FAILURES, the engine's tags separated by commas.
#ifndef AWVA_KNOWN_FAILURES
#define AWVA_KNOWN_FAILURES ""
#endif
[[nodiscard]] inline bool known_failure(std::string_view check, int preset = -1) {
	const std::string_view all = AWVA_KNOWN_FAILURES;
	const std::string with_preset = preset >= 0 ? std::string(check) + ":" + entry().preset_name(preset) : std::string();
	for (size_t at = 0; at <= all.size();) {
		const size_t end = std::min(all.find(',', at), all.size());
		const std::string_view tag = all.substr(at, end - at);
		if (!tag.empty() && (tag == check || tag == with_preset)) return true;
		at = end + 1;
	}
	return false;
}
// CONFORM(check, preset, expression): CHECK, or for a known failure a warning that says so (and
// one that says the failure is gone when the expression holds: then drop it from the list)
#define CONFORM(check, preset, ...) \
	do { \
		if (::contract::known_failure(check, preset)) { \
			const bool conform_ok_ = static_cast<bool>(__VA_ARGS__); \
			WARN_MESSAGE(conform_ok_, "known failure [" << check << "] (sdk/tests/known_failures.cmake)"); \
			if (conform_ok_) MESSAGE("known failure [" << check << "] passes here"); \
		} else { \
			CHECK(__VA_ARGS__); \
		} \
	} while (0)

// --- the keys and the phrases ---------------------------------------------------------------------

// The keys: pressure glides to its target at 1 per 20 ms, vpres is its speed as the harness
// performances compute it, gate/strike/release follow (track_key): every engine sees a real
// strike and release.
struct Keys {
	std::vector<VoiceControls> vc;
	std::vector<float> target;
	GlobalControls g = dsp::globals_at_rest;
	int rate = SR;
	explicit Keys(int nv, int sample_rate = SR) :
	    vc(static_cast<size_t>(nv), dsp::voice_at_rest), target(static_cast<size_t>(nv), 0.0f), rate(sample_rate) {}
	void press(int v, float note, float pres) {
		vc[static_cast<size_t>(v)].note = note;
		target[static_cast<size_t>(v)] = pres;
	}
	void release(int v) { target[static_cast<size_t>(v)] = 0.0f; }
	void release_all() { std::fill(target.begin(), target.end(), 0.0f); }
	void step() {
		// 1 per 20 ms (at 48 kHz 1/30 a block), vpres in units per 80 s (as the performances)
		const auto speed = static_cast<float>(dsp::BLOCK / (0.02 * rate));
		const float blocks_per_s = static_cast<float>(rate) / dsp::BLOCK;
		for (size_t v = 0; v < vc.size(); v++) {
			const float p = vc[v].pres, t = target[v];
			const float np = p < t ? std::min(t, p + speed) : std::max(t, p - speed);
			vc[v].vpres = std::clamp((np - p) * blocks_per_s * 0.0125f, -1.0f, 1.0f);
			vc[v].pres = np;
			dsp::track_key(vc[v]);
		}
	}
};

struct Stats {
	float peak = 0.0f;
	long nonfinite = 0;
	int last_loud = -1; // last block with a sample above SILENT
	int blocks = 0;
};

// n blocks of the keys (each block: keys.step(), then before(b) may change anything)
inline Stats play(sdk::Engine& e, Keys& k, int n, std::vector<float>* keep = nullptr, const std::function<void(int)>& before = {}) {
	Stats s;
	for (int b = 0; b < n; b++) {
		k.step();
		if (before) before(b);
		float out[dsp::BLOCK];
		float* const o[1] = {out};
		e.process(k.vc.data(), k.g, o);
		for (float x : out) {
			if (!std::isfinite(x)) s.nonfinite++;
			else {
				s.peak = std::max(s.peak, std::fabs(x));
				if (std::fabs(x) > SILENT) s.last_loud = s.blocks;
			}
		}
		if (keep) keep->insert(keep->end(), out, out + dsp::BLOCK);
		s.blocks++;
	}
	return s;
}

// a phrase: a three note chord with vibrato and tilt, a melody on top, all released at 1.5 s
inline void phrase(Keys& k, int b, float transpose = 0.0f) {
	const double t = static_cast<double>(b) * dsp::BLOCK / k.rate;
	const int nv = static_cast<int>(k.vc.size());
	if (b == 0) {
		k.press(0, 48 + transpose, 0.6f);
		if (nv > 1) k.press(1, 55 + transpose, 0.7f);
		if (nv > 2) k.press(2, 64 + transpose, 0.5f);
	}
	if (nv > 3) {
		const int step = static_cast<int>(t / 0.25);
		if (t < 1.5 && b == blocks(step * 0.25, k.rate)) k.press(3, static_cast<float>(67 + 2 * (step % 4)) + transpose, 0.8f);
		if (t < 1.5 && b == blocks(step * 0.25 + 0.18, k.rate)) k.release(3);
	}
	for (int v = 0; v < std::min(nv, 3); v++) {
		VoiceControls& c = k.vc[static_cast<size_t>(v)];
		c.but_x = static_cast<float>(0.3 * std::sin(2 * M_PI * 5 * t + v));
		c.but_y = static_cast<float>(0.6 * std::sin(2 * M_PI * 0.7 * t + v));
	}
	if (b == blocks(1.5, k.rate)) k.release_all();
}

struct Rng {
	uint32_t s;
	float next() {
		s = s * 1664525u + 1013904223u;
		return static_cast<float>(s >> 8) * (1.0f / 16777216.0f);
	}
	float range(float lo, float hi) { return lo + next() * (hi - lo); }
};

// random playing: presses and releases, glides, tilt, strikes, pedal and motion, extremes included
inline void fuzz_controls(Rng& r, Keys& k) {
	for (size_t v = 0; v < k.vc.size(); v++) {
		VoiceControls& c = k.vc[v];
		const float u = r.next();
		if (u < 0.03f) { // press / release (instantly: the pressure jumps)
			const bool on = c.pres <= 0.0f;
			c.pres = k.target[v] = on ? r.range(0.05f, 1.3f) : 0.0f;
			c.vpres = on ? r.range(0.0f, 1.0f) : -r.range(0.0f, 1.0f);
			if (on) c.note = r.range(10.0f, 120.0f);
		} else if (c.pres > 0.0f) {
			c.pres = k.target[v] = std::clamp(c.pres + r.range(-0.05f, 0.05f), 0.01f, 1.5f);
			c.vpres = r.range(-0.2f, 0.2f);
			c.note += r.range(-0.1f, 0.1f);
			c.but_x = r.range(-1.0f, 1.0f);
			c.but_y = r.range(-1.0f, 1.0f);
		} else {
			c.vpres = 0.0f;
		}
		dsp::track_key(c);
	}
	if (r.next() < 0.02f) {
		k.g.acc_abs = r.range(0.0f, 3.5f);
		k.g.acc_x = r.range(-1.0f, 1.0f);
		k.g.acc_y = r.range(-1.0f, 1.0f);
		k.g.acc_z = r.range(-1.0f, 1.0f);
		k.g.rot_x = r.range(-1.0f, 1.0f);
		k.g.rot_y = r.range(-1.0f, 1.0f);
		k.g.rot_z = r.range(-1.0f, 1.0f);
		k.g.pedal = r.range(0.0f, 1.0f);
	}
}

// one random parameter value: mostly in range, often the extremes, sometimes far outside or NaN/Inf
[[nodiscard]] inline float fuzz_value(Rng& r, const dsp::ParamDesc& d) {
	const float u = r.next(), span = d.max - d.min;
	if (u < 0.55f) return r.range(d.min, d.max);
	if (u < 0.70f) return d.min;
	if (u < 0.85f) return d.max;
	if (u < 0.92f) return r.next() < 0.5f ? d.min - 10.0f * span - 1.0f : d.max + 10.0f * span + 1.0f;
	if (u < 0.96f) return NAN;
	return r.next() < 0.5f ? INFINITY : -INFINITY;
}

// a random performance with parameter writes, n blocks; the same seed plays the same
inline std::vector<float> fuzz_render(sdk::Engine& e, uint32_t seed, int n, Stats* st = nullptr) {
	Keys k(e.voices());
	Rng r{seed};
	std::vector<float> out;
	const int np = e.num_params();
	const Stats s = play(e, k, n, &out, [&](int) {
		fuzz_controls(r, k);
		if (np > 0)
			for (int w = 1 + static_cast<int>(r.next() * 4); w > 0; w--) {
				const int i = static_cast<int>(r.next() * np) % np;
				e.set_param(i, fuzz_value(r, *e.param_desc(i)));
			}
	});
	if (st) *st = s;
	return out;
}

[[nodiscard]] inline bool same_bits(const std::vector<float>& a, const std::vector<float>& b) {
	return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

} // namespace contract

#endif // AWVA_SDK_TESTS_CONTRACT_H_
