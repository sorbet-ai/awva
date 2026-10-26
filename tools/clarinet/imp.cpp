// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// input impedance of the model's bore at a note: imp <preset> <note> <out.f32> [name=value ...]
#define CLARINET_TEST_HOOK 1
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
	const float note = std::strtof(argv[2], nullptr);
	eng.bulk = &mem;
	clarinet::init(eng, 48000, 1);
	clarinet::set_preset(eng, preset);
	using EP = awva::EngineParams<clarinet::Engine>;
	for (int a = 4; a < argc; a++) {
		const char* eq = std::strchr(argv[a], '=');
		const std::string nm(static_cast<const char*>(argv[a]), eq);
		for (int i = 0; i < EP::count; i++) if (nm == EP::desc(i)->name) *EP::ptr(eng, i) = std::strtof(eq + 1, nullptr);
	}
	eng.ps = eng.p;
	clarinet::update_derived(eng);
	const long N = 1 << 16;
	std::vector<float> p(N);
	clarinet::clarinet_dbg_imp = 1;
	clarinet::clarinet_dbg_p = p.data();
	clarinet::clarinet_dbg_len = N;
	float out[32];
	for (long b = 0; b < N / 32; b++) {
		eng.vc[0] = {note, 0.3f, 0.0f, 0.0f, 0.0f};
		clarinet::process(eng, out);
	}
	const auto& s = eng.vs[0];
	std::printf("DB %.3f nbu %d dA %d mode %d gth %.4f\n", s.DB, s.nbu, s.dA, s.mode, s.gth);
	FILE* f = std::fopen(argv[3], "wb");
	std::fwrite(p.data(), 4, N, f);
	std::fclose(f);
}
