// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// internal signals of voice 0 on a held note: probe <preset> <note> <pres> <out prefix> [name=value ...]
#define CLARINET_TEST_HOOK 1
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "voice_clarinet.h"
static clarinet::BulkMemory mem;
static clarinet::Engine eng;
int main(int argc, char** argv) {
	const int preset = std::atoi(argv[1]);
	const float note = std::strtof(argv[2], nullptr), pres = std::strtof(argv[3], nullptr);
	eng.bulk = &mem;
	clarinet::init(eng, 48000, 1);
	clarinet::set_preset(eng, preset);
	using EP = awva::EngineParams<clarinet::Engine>;
	for (int a = 5; a < argc; a++) {
		const char* eq = std::strchr(argv[a], '=');
		const std::string nm(static_cast<const char*>(argv[a]), eq);
		for (int i = 0; i < EP::count; i++) if (nm == EP::desc(i)->name) *EP::ptr(eng, i) = std::strtof(eq + 1, nullptr);
	}
	eng.ps = eng.p;
	clarinet::update_derived(eng);
	const long N = 48000;
	std::vector<float> p(N), u(N), x(N), y(N);
	clarinet::clarinet_dbg_p = p.data(); clarinet::clarinet_dbg_u = u.data(); clarinet::clarinet_dbg_x = x.data();
	clarinet::clarinet_dbg_len = N;
	for (long b = 0; b < N / 32; b++) {
		const float t = b * 32.0f / 48000;
		eng.vc[0] = {note, pres * std::fmin(t / 0.035f, 1.0f), t < 0.035f ? 0.4f : 0.0f, 0.0f, 0.0f};
		clarinet::process(eng, y.data() + b * 32);
	}
	const std::string o = argv[4];
	for (auto [n, v] : {std::pair<const char*, std::vector<float>*>{".p", &p}, {".u", &u}, {".x", &x}, {".y", &y}}) {
		FILE* f = std::fopen((o + n).c_str(), "wb"); std::fwrite(v->data(), 4, N, f); std::fclose(f);
	}
}
