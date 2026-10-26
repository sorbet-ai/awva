// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
//
// nsc::ScopedNoDenormals: subnormal floats flushed to zero for a scope, the mode before restored
// when it ends (as JUCE's ScopedNoDenormals). The component's process() and process_voices() run
// in one, so an engine's decaying states never reach the subnormal range on a PC, where each
// operation on them costs tens of cycles (an engine left idle can otherwise spend 10-30x its
// silent cost on a tail of subnormals that no one hears).
//   x86-64     MXCSR FTZ (flush results) and DAZ (subnormal inputs read as 0)
//   AArch64    FPCR.FZ (both)
//   elsewhere  nothing: 32-bit Arm (e.g. Cortex-M7) keeps its IEEE mode, so its samples and the target
//              goldens are unchanged
// What it changes: only samples that would have been subnormal (|x| < 2^-126, about 1.2e-38)
// come out as 0, and nothing that depends on a subnormal.
#ifndef NSC_NO_DENORMALS_H_
#define NSC_NO_DENORMALS_H_

#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace nsc {

class ScopedNoDenormals {
public:
#if defined(__x86_64__) || defined(_M_X64)
	ScopedNoDenormals() :
	    saved_(_mm_getcsr()) {
		_mm_setcsr(saved_ | FTZ | DAZ);
	}
	~ScopedNoDenormals() { _mm_setcsr(saved_); }

private:
	static constexpr unsigned FTZ = 0x8000u, DAZ = 0x0040u;
	unsigned saved_;
#elif defined(__aarch64__)
	ScopedNoDenormals() {
		__asm__ __volatile__("mrs %0, fpcr"
		                     : "=r"(saved_));
		const uint64_t fz = saved_ | FZ;
		__asm__ __volatile__("msr fpcr, %0"
		                     :
		                     : "r"(fz));
	}
	~ScopedNoDenormals() { __asm__ __volatile__("msr fpcr, %0"
		                                        :
		                                        : "r"(saved_)); }

private:
	static constexpr uint64_t FZ = uint64_t{1} << 24;
	uint64_t saved_ = 0;
#else
	ScopedNoDenormals() = default;
#endif

public:
	ScopedNoDenormals(const ScopedNoDenormals&) = delete;
	ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;
};

} // namespace nsc

#endif // NSC_NO_DENORMALS_H_
