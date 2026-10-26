// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// Renders the flute engine on engine-level control files (per 32-sample block 10 voices x (note,
// pres, vpres, x, y) + 8 globals, float32) as an embedded host's synth thread drives it: a zeroed Engine,
// init, set_preset, then per block the controls and process. --set name=value overrides live
// parameters (EngineParams) from the first block on, without their glide. The fitting tools' renderer.
//
// Usage: replay --engine flute --preset N [--set name=v ...] [--check ref.f32] --job in.ctl out.f32 [--job ...]
//        replay --engine flute --list      (the live parameters and the preset's values)
// Build: c++ -O2 -ffp-contract=off -std=gnu++20 -I<awva>/dsp/include -I<awva>/dsp/include/awva replay.cpp -o replay
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#define FLUTE_TEST_HOOK
#include "awva/voice_flute.h"

namespace {
constexpr int NV = 10, BLOCK = 32, SR = 48000;

std::vector<float> read_f32(const std::string& path) {
	std::vector<float> v;
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f) return v;
	std::fseek(f, 0, SEEK_END);
	const long n = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	v.resize(static_cast<size_t>(n) / 4);
	if (std::fread(v.data(), 4, v.size(), f) != v.size()) v.clear();
	std::fclose(f);
	return v;
}

struct Set {
	std::string name;
	float v;
};

template <class E, class InitF, class ProcF>
int run(const char* ename, InitF initf, ProcF procf, const std::vector<Set>& sets, const std::vector<std::pair<std::string, std::string>>& jobs,
    const std::string& check, bool list) {
	using EP = strisy::EngineParams<E>;
	static E e;
	if (list) {
		std::memset(static_cast<void*>(&e), 0, sizeof e);
		initf(e);
		for (int i = 0; i < EP::count; i++) {
			const strisy::ParamDesc* d = EP::desc(i);
			std::printf("%s %s %g %g %g\n", d->name, d->group, (double)d->min, (double)d->max, (double)*EP::ptr(e, i));
		}
		return 0;
	}
	int rc = 0;
	for (const auto& job : jobs) {
		const std::vector<float> ctl = read_f32(job.first);
		const size_t nb = ctl.size() / 58;
		if (nb == 0) {
			std::fprintf(stderr, "empty controls %s\n", job.first.c_str());
			return 2;
		}
		std::memset(static_cast<void*>(&e), 0, sizeof e);
		if constexpr (requires { e.fast; }) {
			static std::remove_pointer_t<decltype(e.fast)> fm;
			e.fast = &fm;
		}
		if constexpr (requires { e.large; }) {
			static std::remove_pointer_t<decltype(e.large)> lm;
			e.large = &lm;
		}
		initf(e);
		for (const Set& s : sets) {
			int found = -1;
			for (int i = 0; i < EP::count; i++)
				if (s.name == EP::desc(i)->name) found = i;
			if (found < 0) {
				std::fprintf(stderr, "%s: no parameter %s\n", ename, s.name.c_str());
				return 2;
			}
			*EP::ptr(e, found) = s.v;
			EP::changed(e, found);
		}
		if constexpr (requires { e.ps = e.p; }) e.ps = e.p; // no glide: the overrides from the start
		std::vector<float> out(nb * BLOCK);
		for (size_t b = 0; b < nb; b++) {
			const float* c = &ctl[b * 58];
			for (int v = 0; v < NV; v++) e.vc[v] = {c[5 * v], c[5 * v + 1], c[5 * v + 2], c[5 * v + 3], c[5 * v + 4]};
			const float* g = c + 50;
			e.g = {g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7]};
			procf(e, out.data() + b * BLOCK);
		}
		size_t bad = 0;
		float peak = 0;
		for (float x : out) {
			if (!std::isfinite(x)) bad++;
			else peak = std::fmax(peak, std::fabs(x));
		}
		if (FILE* f = std::fopen(job.second.c_str(), "wb")) {
			std::fwrite(out.data(), 4, out.size(), f);
			std::fclose(f);
		}
		if (!check.empty()) {
			const std::vector<float> ref = read_f32(check);
			size_t diff = ref.size() == out.size() ? 0 : out.size();
			for (size_t i = 0; i < ref.size() && i < out.size(); i++) diff += std::memcmp(&ref[i], &out[i], 4) != 0;
			std::printf("check %s: %zu samples, %zu differ from %s\n", job.second.c_str(), out.size(), diff, check.c_str());
			if (diff) rc = 1;
		}
		if (bad) {
			std::fprintf(stderr, "NON-FINITE output %s\n", job.second.c_str());
			rc = 1;
		}
		if (flute::flute_guard_resets) std::fprintf(stderr, "GUARD %d", flute::flute_guard_resets);
		std::printf("render %s %s -> %s: %.2f s, peak %.3f\n", ename, job.first.c_str(), job.second.c_str(), (double)out.size() / SR, (double)peak);
	}
	return rc;
}
} // namespace

int main(int argc, char** argv) {
	std::string engine, check;
	int preset = 0;
	bool list = false;
	std::vector<Set> sets;
	std::vector<std::pair<std::string, std::string>> jobs;
	for (int a = 1; a < argc; a++) {
		const std::string s = argv[a];
		auto next = [&]() -> std::string { return a + 1 < argc ? argv[++a] : ""; };
		if (s == "--engine") engine = next();
		else if (s == "--preset") preset = std::atoi(next().c_str());
		else if (s == "--check") check = next();
		else if (s == "--list") list = true;

		else if (s == "--set") {
			const std::string kv = next();
			const size_t eq = kv.find('=');
			sets.push_back({kv.substr(0, eq), static_cast<float>(std::atof(kv.c_str() + eq + 1))});
		} else if (s == "--job") {
			const std::string in = next();
			jobs.push_back({in, next()});
		} else {
			std::fprintf(stderr, "unknown option %s\n", s.c_str());
			return 2;
		}
	}
	if (engine == "flute")
		return run<flute::Engine>(
		    "flute", [&](flute::Engine& e) { flute::init(e, SR, NV); flute::set_preset(e, preset); },
		    [](flute::Engine& e, float* o) { flute::process(e, o); }, sets, jobs, check, list);
	std::fprintf(stderr, "unknown engine %s\n", engine.c_str());
	return 2;
}
