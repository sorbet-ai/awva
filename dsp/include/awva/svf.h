// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
//
// The state-variable filter of the engines, in its trapezoidal (topology-preserving transform)
// form: V. Zavalishin, "The Art of VA Filter Design" (2012), chapter 4 (the SVF), and A. Simper,
// "Linear Trapezoidal Integrated State Variable Filter" (Cytomic technical paper, 2013), whose
// update this follows.
#ifndef AWVA_SVF_H_
#define AWVA_SVF_H_

#include "dspmath.h"

namespace awva {

// With the prewarped integrator gain g = tan(pi F / SR) and the damping k = 1/Q, one sample of
// input v0 and integrator states ic1, ic2 is
//   v1 = (ic1 + g (v0 - ic2)) / (1 + g (g + k)),   v2 = ic2 + g v1,
//   ic1' = 2 v1 - ic1,   ic2' = 2 v2 - ic2.
// With a = 1 / (1 + g (g + k)) the two outputs need no division per sample:
//   v1 = a ic1 + g a (v0 - ic2),   v2 = ic2 + g a ic1 + g^2 a (v0 - ic2).
// v2 is the lowpass, v1 the bandpass (gain Q at the centre; k v1 has unity gain), and
// v0 - k v1 - v2 the highpass.

struct SvfCoef {
	real a, ga, g2a, k; // a, g a, g^2 a, k
};
struct SvfState {
	real ic1, ic2;
};

// the coefficients from g and Q
[[nodiscard]] static inline SvfCoef svf_from_g(real g, real Q) {
	real k = 1.0f / Q;
	real a = 1.0f / (1.0f + g * (g + k));
	return {a, g * a, g * g * a, k};
}

// the coefficients from x = pi F / SR, with tan(x) as its [7/6] Pade approximant
//   tan x ~ x (135135 - 17325 x^2 + 378 x^4 - x^6) / (135135 - 62370 x^2 + 3150 x^4 - 28 x^6),
// g = gN / D, and a, g a, g^2 a over one common denominator, so a modulated cutoff costs one
// division per sample. (k is left 0: the lowpass and the raw bandpass don't use it.)
[[nodiscard]] static inline SvfCoef svf_pade(real x, real Q) {
	real x2 = x * x;
	real gN = x * (135135.0f + x2 * (-17325.0f + x2 * (378.0f - x2)));
	real D = 135135.0f + x2 * (-62370.0f + x2 * (3150.0f - 28.0f * x2));
	real QD = Q * D;
	real r = 1.0f / (QD * D + gN * (gN * Q + D));
	return {QD * D * r, gN * QD * r, gN * gN * Q * r, 0.0f};
}

// one sample: input v0, states s1, s2 (updated), coefficients c; out1 = v1, out2 = v2
#define SVF_TICK(v0, s1, s2, c, out1, out2) \
	do { \
		::awva::real e_ = (v0) - (s2); \
		(out1) = (c).a * (s1) + (c).ga * e_; \
		(out2) = (s2) + (c).ga * (s1) + (c).g2a * e_; \
		(s1) = 2.0f * (out1) - (s1); \
		(s2) = 2.0f * (out2) - (s2); \
	} while (0)

} // namespace awva

#endif // AWVA_SVF_H_
