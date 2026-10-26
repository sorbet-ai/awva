// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// A factor per control step from its time constant, without the C library's pow(): x^y for the
// two factors the control rate scales (the voices' level decay, 0.998 per 2 ms, and the strike's
// fade, 0.6 per 2 ms), as the nearest double to the exact value. In double-double arithmetic
// (about 100 bits) with ln(x) a constant, so the result is the correctly rounded power: on every
// host the same (glibc's pow() is not correctly rounded for 67 of 376001 control rates' level
// decays, by 1 ulp; the strike's fade, a float, is the same for all of them), and no libm on an
// embedded host (newlib's pow brings errno, stdio and the heap into a module). At 2 ms (y = 1)
// it is the factor itself.
#ifndef AWVA_STEP_POW_H_
#define AWVA_STEP_POW_H_

#include <stdint.h>

namespace awva::step_pow {

struct DD {
	double hi, lo;
};

// a + b exactly (Knuth)
[[nodiscard]] constexpr DD two_sum(double a, double b) {
	const double s = a + b, bb = s - a, e = (a - (s - bb)) + (b - bb);
	return {s, e};
}
[[nodiscard]] constexpr DD quick_two_sum(double a, double b) {
	const double s = a + b;
	return {s, b - (s - a)};
}
// a * b exactly (Dekker: Veltkamp's split, no fused multiply-add needed)
[[nodiscard]] constexpr DD two_prod(double a, double b) {
	constexpr double SPLIT = 134217729.0; // 2^27 + 1
	const double p = a * b;
	const double ta = SPLIT * a, ah = ta - (ta - a), al = a - ah;
	const double tb = SPLIT * b, bh = tb - (tb - b), bl = b - bh;
	const double e = ((ah * bh - p) + ah * bl + al * bh) + al * bl;
	return {p, e};
}
[[nodiscard]] constexpr DD add(DD a, DD b) {
	DD s = two_sum(a.hi, b.hi);
	const DD t = two_sum(a.lo, b.lo);
	s.lo += t.hi;
	s = quick_two_sum(s.hi, s.lo);
	s.lo += t.lo;
	return quick_two_sum(s.hi, s.lo);
}
[[nodiscard]] constexpr DD mul(DD a, DD b) {
	DD p = two_prod(a.hi, b.hi);
	p.lo += a.hi * b.lo + a.lo * b.hi;
	return quick_two_sum(p.hi, p.lo);
}
[[nodiscard]] constexpr DD mul(DD a, double b) {
	DD p = two_prod(a.hi, b);
	p.lo += a.lo * b;
	return quick_two_sum(p.hi, p.lo);
}

inline constexpr DD LN2 = {0x1.62e42fefa39efp-1, 0x1.abc9e3b39803fp-56};
// ln of the factors' bases, each the nearest double-double to the exact logarithm of its double
inline constexpr DD LN_0_998 = {-0x1.06680ffe7cc09p-9, 0x1.307f216c47922p-63};
inline constexpr DD LN_0_6 = {-0x1.058aefa811452p-1, 0x1.c19f73d945334p-60};

// 2^k as a double (|k| < 1000)
[[nodiscard]] constexpr double pow2(int k) {
	double r = 1.0;
	for (; k > 0; k--) r *= 2.0;
	for (; k < 0; k++) r *= 0.5;
	return r;
}

// e^z, z a double-double (|z| < 700): z = k ln2 + r, |r| <= ln2 / 2, e^r by its Taylor series
[[nodiscard]] constexpr DD exp_dd(DD z) {
	const double kf = z.hi / LN2.hi;
	const int k = static_cast<int>(kf < 0 ? kf - 0.5 : kf + 0.5);
	const DD r = add(z, mul(LN2, static_cast<double>(-k)));
	// sum of r^n / n!, n = 0 .. 30, by Horner (each term's 1/n a division in double-double)
	DD s = {1.0, 0.0};
	for (int n = 30; n >= 1; n--) {
		// s = 1 + r s / n
		DD t = mul(r, s);
		const double q1 = t.hi / n;
		const DD back = two_prod(q1, static_cast<double>(n));
		const double q2 = ((t.hi - back.hi) - back.lo + t.lo) / n;
		t = quick_two_sum(q1, q2);
		s = add({1.0, 0.0}, t);
	}
	const double m = pow2(k);
	return {s.hi * m, s.lo * m};
}

// x^y for x one of the bases above (by its log), y >= 0: the nearest double
[[nodiscard]] constexpr double pow_of(double x, DD ln_x, double y) {
	if (y == 1.0) return x;
	if (y == 0.0) return 1.0;
	const DD e = exp_dd(mul(ln_x, y));
	return e.hi + e.lo;
}

} // namespace awva::step_pow

#endif // AWVA_STEP_POW_H_
