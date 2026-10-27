// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// Shared helpers of the host renderers and tests: WAV output (at full scale 1), a
// hash of a render (for golden checksums) and parameter writes by the hosts' rule.
#ifndef AWVA_HARNESS_UTIL_H_
#define AWVA_HARNESS_UTIL_H_

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

#include "awva/params.h"

namespace harness {

// 16 bit WAV, hard clipped at full scale (the engines' output is nominal +-1: engine.h OUTPUT_SCALE)
[[nodiscard]] inline bool write_wav(const char* path, std::span<const float> x, int channels, int sample_rate) {
	FILE* f = std::fopen(path, "wb");
	if (!f) return false;
	const uint32_t n = static_cast<uint32_t>(x.size());
	const uint32_t sr = static_cast<uint32_t>(sample_rate), br = sr * 2 * static_cast<uint32_t>(channels), data = n * 2;
	const uint16_t ch = static_cast<uint16_t>(channels), ba = static_cast<uint16_t>(2 * channels), bits = 16, fmt = 1;
	const uint32_t riff = 36 + data, fmtlen = 16;
	std::fwrite("RIFF", 1, 4, f);
	std::fwrite(&riff, 4, 1, f);
	std::fwrite("WAVEfmt ", 1, 8, f);
	std::fwrite(&fmtlen, 4, 1, f);
	std::fwrite(&fmt, 2, 1, f);
	std::fwrite(&ch, 2, 1, f);
	std::fwrite(&sr, 4, 1, f);
	std::fwrite(&br, 4, 1, f);
	std::fwrite(&ba, 2, 1, f);
	std::fwrite(&bits, 2, 1, f);
	std::fwrite("data", 1, 4, f);
	std::fwrite(&data, 4, 1, f);
	std::vector<int16_t> s(n);
	for (uint32_t k = 0; k < n; k++) {
		float v = x[k];
		v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
		s[k] = static_cast<int16_t>(std::lrintf(v * 32767.0f));
	}
	std::fwrite(s.data(), 2, n, f);
	return std::fclose(f) == 0;
}

// FNV-1a 64 over the samples' bits: equal hashes mean bit-identical renders
[[nodiscard]] inline uint64_t hash_samples(std::span<const float> x) {
	uint64_t h = 1469598103934665603ull;
	for (float v : x) {
		uint32_t u;
		std::memcpy(&u, &v, 4);
		for (int k = 0; k < 4; k++) {
			h ^= (u >> (8 * k)) & 0xffu;
			h *= 1099511628211ull;
		}
	}
	return h;
}

// a parameter write as every host applies it (params.h, write_param): NaN ignored,
// clamped to [min, max], rounded unless continuous, then changed()
template <class E> inline void param_write(E& e, int i, float value) {
	using P = awva::EngineParams<E>;
	const awva::ParamDesc* d = P::desc(i);
	float* p = P::ptr(e, i);
	if (!d || !p || value != value) return;
	float v = value < d->min ? d->min : (value > d->max ? d->max : value);
	if (d->kind != awva::ParamKind::continuous) v = static_cast<float>(static_cast<int>(v + (v >= 0 ? 0.5f : -0.5f)));
	*p = v;
	P::changed(e, i);
}

} // namespace harness

#endif
