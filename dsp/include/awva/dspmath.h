// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-FileCopyrightText: 2012 Paul Mineiro (the fastpow2 approximation, BSD-3-Clause; notice below)
// SPDX-FileCopyrightText: 2020 Piers Titus van der Torren (the tan0 coefficients, Apache-2.0)
// SPDX-License-Identifier: Apache-2.0
//
// Small numeric helpers for the engines: the sample type, 2^x and tan(x) approximations, and
// min/max that compile to single instructions. An MCU libm (e.g. on a Cortex-M7) may do much of
// expf/powf/tanf in double precision in software, so the hot paths use these instead. PROVENANCE.md has the sources.
#ifndef AWVA_DSPMATH_H_
#define AWVA_DSPMATH_H_

#include <math.h>
#include <stdint.h>

namespace strisy {

// the sample type: float; STRISY_REAL=double builds the engines in double (a host-side check of
// an algorithm apart from float rounding)
#ifdef STRISY_REAL
using real = STRISY_REAL;
#else
using real = float;
#endif

// 2^p, for p >= 0 (and small negative p): the exponent bits of a float built directly, with a
// rational correction of the fraction. The formula and its constants are Paul Mineiro's
// "fastpow2" (fastapprox, BSD-3-Clause):
//   Copyright (C) 2012 Paul Mineiro. Redistribution and use in source and binary forms, with or
//   without modification, are permitted provided that the conditions of the BSD 3-Clause licence
//   are met (the copyright notice, this list of conditions and the disclaimer are retained; the
//   name of the author is not used to endorse derived products). THE SOFTWARE IS PROVIDED "AS
//   IS", WITHOUT WARRANTY OF ANY KIND. (PROVENANCE.md has the licence text.)
[[nodiscard]] static inline float fastpow2(float p) {
	int whole = p;
	float frac = p - whole;
	union {
		uint32_t bits;
		float value;
	} out = {static_cast<uint32_t>((1 << 23) * (p + 121.2740575f + 27.7280233f / (4.84252568f - frac) - 1.49012907f * frac))};
	return out.value;
}

// tan(a) for a in [0, pi/4] as an odd cubic-in-a^2 polynomial, a ((c2 a^2 + c1) a^2 + 1). The
// coefficients are the Faust fast.lib's tan0 (Piers Titus van der Torren, Apache-2.0).
[[nodiscard]] static inline real tan0(real a) {
	real sq = a * a;
	return ((0.2033f * sq + 0.31755f) * sq + 1.0f) * a;
}

// max and min: fmaxf/fminf compile to one vmaxnm/vminnm each on Cortex-M7 (a compare and select
// needs vcmp and vmrs); the same results for the numbers used here (no NaNs)
[[nodiscard]] static inline real rmax(real a, real b) { return sizeof(real) == sizeof(float) ? (real)fmaxf((float)a, (float)b) : (real)fmax(a, b); }
[[nodiscard]] static inline real rmin(real a, real b) { return sizeof(real) == sizeof(float) ? (real)fminf((float)a, (float)b) : (real)fmin(a, b); }

} // namespace strisy

#endif // AWVA_DSPMATH_H_
