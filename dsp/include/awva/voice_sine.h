// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
/**
 * Minimal example voice engine: one sine per voice, amplitude following pressure.
 * Traits: engines/sine.h. It shows the API every voice_<name>.h provides (engine.h, adapter.h)
 * and a couple of patterns that matter on the Cortex-M7:
 * - no sinf/expf/divisions per sample: block-rate coefficients, per-sample multiply-adds
 * - no libm at all (sinf, cosf, expf, powf, tanf...): newlib on this toolchain does parts of them
 *   in software double precision, which is slow and makes tools/build.sh reject the build. Use
 *   polynomial approximations (dspmath.h has fastpow2 and tan0; cos/sin below).
 * - per-voice state in locals during the sample loop, plain-data Engine (it lives in DTCM)
 */
#ifndef VOICE_SINE_H_
#define VOICE_SINE_H_

#include <math.h>
#include "controls.h"
#include "dspmath.h"

namespace sine {

using real = float;
static constexpr int BLOCK = awva::BLOCK;
static constexpr int MAX_VOICES = 16;

struct VoiceState {
	real c, s; // rotating phasor: output is s
	real amp;  // smoothed amplitude
};

struct Engine {
	awva::InstrumentControls g;
	awva::NoteControls vc[MAX_VOICES];
	int nvoices;
	real sr;
	real amp_coef; // one-pole smoothing per sample
	VoiceState vs[MAX_VOICES];
};

// cos and sin for |x| <= pi/2 (Taylor to x^8 / x^9, error below 4e-6)
[[nodiscard]] static inline real cos_poly(real x) {
	real x2 = x * x;
	return 1.0f + x2 * (-0.5f + x2 * (1.0f / 24 + x2 * (-1.0f / 720 + x2 * (1.0f / 40320))));
}
[[nodiscard]] static inline real sin_poly(real x) {
	real x2 = x * x;
	return x * (1.0f + x2 * (-1.0f / 6 + x2 * (1.0f / 120 + x2 * (-1.0f / 5040 + x2 * (1.0f / 362880)))));
}

static inline void init(Engine& e, int sample_rate, int nvoices) {
	e.sr = (real)sample_rate;
	e.amp_coef = 1.0f / (1.0f + 0.005f * e.sr); // one-pole, about 5 ms
	e.nvoices = nvoices < MAX_VOICES ? nvoices : MAX_VOICES;
	e.g = awva::instrument_at_rest;
	for (int v = 0; v < e.nvoices; v++) {
		e.vc[v] = awva::note_at_rest;
		e.vs[v] = {1.0f, 0.0f, 0.0f};
	}
}

static inline void process(Engine& e, real* output) {
	for (int n = 0; n < BLOCK; n++) output[n] = 0.0f;
	for (int v = 0; v < e.nvoices; v++) {
		const awva::NoteControls c = e.vc[v];
		VoiceState& st = e.vs[v];
		real target = c.pressure > 0.0f ? c.pressure : 0.0f;
		if (target == 0.0f && st.amp < 1e-6f) continue; // silent voice: skip
		// block-rate rotation coefficients for the note's frequency
		real w = 2.0f * 3.14159265f * 8.1757989f * awva::fastpow2(c.pitch * (1.0f / 12.0f)) / e.sr;
		w = w < 1.5707963f ? w : 1.5707963f; // up to SR/4
		real cw = cos_poly(w), sw = sin_poly(w);
		real cc = st.c, ss = st.s, amp = st.amp;
		const real k = e.amp_coef;
		for (int n = 0; n < BLOCK; n++) {
			real nc = cc * cw - ss * sw;
			ss = cc * sw + ss * cw;
			cc = nc;
			amp += k * (target - amp);
			output[n] += ss * amp;
		}
		// renormalize the phasor once per block against rounding drift
		real g = 1.5f - 0.5f * (cc * cc + ss * ss);
		st.c = cc * g;
		st.s = ss * g;
		st.amp = amp;
	}
}

[[nodiscard]] static inline int active_voices(const Engine& e) {
	int n = 0;
	for (int v = 0; v < e.nvoices; v++) n += e.vs[v].amp > 1e-6f || e.vc[v].pressure > 0.0f;
	return n;
}

} // namespace sine

#endif // VOICE_SINE_H_
