// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// Renders single clarinet notes (one voice) for the fit: synthetic key controls, the engine
// header compiled in directly (no SDK).
//
//   clar_render --preset N [--set name=value ...] --job out.f32 note pres strike rise hold tail [tilt]
//               [--job ...] [--legato out.f32 note1 note2 pres strike rise hold1 hold2 tail]
//
// A job: the key pressure rises to `pres` over `rise` s (smoothstep) with vpres a pulse peaking
// at `strike`, holds for `hold` s, falls in 20 ms (vpres -0.3) and the tail is rendered. Writes
// mono float32 at 48 kHz. Prints "job <out> peak <x> guard <n>" per job.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "voice_clarinet.h"

namespace {
constexpr int SR = 48000;
clarinet::BulkMemory mem;
clarinet::Engine eng;

bool set_param(const char* nv) {
	const char* eq = std::strchr(nv, '=');
	if (!eq) return false;
	const std::string name(nv, eq);
	const float v = std::strtof(eq + 1, nullptr);
	using EP = strisy::EngineParams<clarinet::Engine>;
	for (int i = 0; i < EP::count; i++)
		if (name == EP::desc(i)->name) {
			const strisy::ParamDesc* d = EP::desc(i);
			*EP::ptr(eng, i) = std::fmin(std::fmax(v, d->min), d->max);
			EP::changed(eng, i);
			return true;
		}
	std::fprintf(stderr, "unknown parameter %s\n", name.c_str());
	return false;
}

struct Job {
	std::string out;
	float n1, n2, pres, strike, rise, hold1, hold2, tail, tilt;
};

float smooth(float s) { return s <= 0 ? 0 : s >= 1 ? 1 : s * s * (3 - 2 * s); }

void render(const Job& j, int preset, const std::vector<std::string>& sets) {
	eng = clarinet::Engine{};
	eng.bulk = &mem;
	clarinet::init(eng, SR, 1);
	clarinet::set_preset(eng, preset);
	for (const auto& s : sets) set_param(s.c_str());
	eng.ps = eng.p;
	clarinet::update_derived(eng);
	const double T = 0.05 + j.rise + j.hold1 + j.hold2 + 0.02 + j.tail;
	const int nb = static_cast<int>(T * SR / 32);
	std::vector<float> y(static_cast<size_t>(nb) * 32);
	float peak = 0;
	float prevp = 0;
	for (int b = 0; b < nb; b++) {
		const double t = b * 32.0 / SR - 0.05;
		float p = 0, vp = 0;
		const double t_off = j.rise + j.hold1 + j.hold2;
		if (t >= 0 && t < j.rise) {
			p = j.pres * smooth(static_cast<float>(t / j.rise));
			const float s = static_cast<float>(t / j.rise);
			vp = j.strike * 4.0f * s * (1.0f - s) * 1.0f;
		} else if (t >= j.rise && t < t_off) {
			p = j.pres;
		} else if (t >= t_off && t < t_off + 0.02) {
			p = j.pres * (1.0f - static_cast<float>((t - t_off) / 0.02));
			vp = -0.3f;
		}
		if (p <= 0 && t > 0 && t < t_off) p = 1e-3f;
		(void)prevp;
		prevp = p;
		eng.vc[0].note = (t < j.rise + j.hold1) ? j.n1 : j.n2;
		eng.vc[0].pres = p;
		eng.vc[0].vpres = vp;
		eng.vc[0].but_x = 0;
		eng.vc[0].but_y = j.tilt;
		clarinet::process(eng, y.data() + static_cast<size_t>(b) * 32);
	}
	int bad = 0;
	for (float v : y) {
		if (!std::isfinite(v)) bad++;
		else peak = std::fmax(peak, std::fabs(v));
	}
	if (FILE* f = std::fopen(j.out.c_str(), "wb")) {
		std::fwrite(y.data(), 4, y.size(), f);
		std::fclose(f);
	}
	std::printf("job %s peak %.4f bad %d\n", j.out.c_str(), peak, bad);
}
} // namespace

int main(int argc, char** argv) {
	int preset = 0;
	std::vector<std::string> sets;
	std::vector<Job> jobs;
	for (int a = 1; a < argc; a++) {
		const std::string s = argv[a];
		if (s == "--preset" && a + 1 < argc) preset = std::atoi(argv[++a]);
		else if (s == "--set" && a + 1 < argc) sets.emplace_back(argv[++a]);
		else if (s == "--job" && a + 7 < argc) {
			Job j{};
			j.out = argv[++a];
			j.n1 = j.n2 = std::strtof(argv[++a], nullptr);
			j.pres = std::strtof(argv[++a], nullptr);
			j.strike = std::strtof(argv[++a], nullptr);
			j.rise = std::strtof(argv[++a], nullptr);
			j.hold1 = std::strtof(argv[++a], nullptr);
			j.hold2 = 0;
			j.tail = std::strtof(argv[++a], nullptr);
			if (a + 1 < argc && argv[a + 1][0] != '-') j.tilt = std::strtof(argv[++a], nullptr);
			jobs.push_back(j);
		} else if (s == "--legato" && a + 9 < argc) {
			Job j{};
			j.out = argv[++a];
			j.n1 = std::strtof(argv[++a], nullptr);
			j.n2 = std::strtof(argv[++a], nullptr);
			j.pres = std::strtof(argv[++a], nullptr);
			j.strike = std::strtof(argv[++a], nullptr);
			j.rise = std::strtof(argv[++a], nullptr);
			j.hold1 = std::strtof(argv[++a], nullptr);
			j.hold2 = std::strtof(argv[++a], nullptr);
			j.tail = std::strtof(argv[++a], nullptr);
			jobs.push_back(j);
		} else {
			std::fprintf(stderr, "bad argument %s\n", s.c_str());
			return 2;
		}
	}
	for (const auto& j : jobs) render(j, preset, sets);
	return 0;
}
