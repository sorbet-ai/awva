// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
/**
 * clarinet: a physical model of the clarinet family (B-flat clarinet, bass clarinet). Nothing sets
 * the pitch but the reed and the bore together. The presets' physics is fitted (CMA-ES) to recordings
 * of real instruments, the University of Iowa MIS scales (pp, mf and ff, note by note): per note and
 * dynamic the sustained harmonic spectrum, the level, the noise between the harmonics, the attack's
 * rise, centroid and harmonics, and the pitch. In the fit, pp, mf and ff are a pressure of 0.12,
 * 0.35 and 0.7 with strikes of 0.15, 0.4 and 0.7.
 *
 *   REED     a single reed on a mouthpiece: one mechanical mode (resonance reed_hz, damping reed_q
 *            by the lip), pushed closed by the pressure difference across it, beating on the lay
 *            when blown hard (a contact stiffness when shut). Flow through the slit: Bernoulli,
 *            u = zeta (1 + x)+ sgn(dp) sqrt|dp|, solved exactly with the bore's wave every sample
 *            (one square root; Guillemain, Kergomard & Voinier 2005). Pressures are in units of
 *            the closing pressure p_M, flows in p_M / Z0: the mouth pressure gamma speaks near 1/3,
 *            beats from 1/2 and chokes near 1 (Kergomard & Chaigne). The turbulence rides on the
 *            flow
 *   BORE     a cylinder: two waveguide sections (four delay lines) split at the register hole.
 *            Its length is the fingering's: a quarter wave of the note in the chalumeau, three
 *            quarters in the clarion (the register hole open: a shunt inertance that bleeds the
 *            first resonance away, so the reed plays the twelfth); above that a short tube with one
 *            strong resonance at the note (the altissimo's vented fingerings).
 *            Wall losses grow with sqrt(f) and the length (viscothermal, bore_r), fitted per
 *            note by a one-pole
 *   TONE HOLES  the open tone-hole lattice reflects the low frequencies (-1) and passes the high
 *            ones out (a two-pole cutoff): below it the bore has its odd resonances, above it
 *            none, so the internal spectrum falls there and the radiated one carries the rest
 *   RADIATION  the flow through the open end, differentiated (a monopole), then a tone low-pass
 *   TUNING   the bore length is solved per block (Newton) so that the input impedance, register
 *            hole and lattice included, peaks at the note together with the reed's own reactance:
 *            the note is in tune at the threshold. The same impedance gives the threshold of
 *            oscillation, which places the lightest touch
 *
 * Playing:
 *   pressure   breath: mouth pressure from just above the threshold (pp, round, nearly odd
 *              harmonics only) to a beating reed (ff, bright)
 *   strike     tonguing: the tongue holds the reed shut against the built-up breath and lets it
 *              go, faster for a harder strike, with an accent (tongue: how fast the reed speaks);
 *              none = a breath attack. Apart from it, the attack's noise: a "t" on the strike
 *              (t_noise, tongue_noise, t_len) and the turbulence before the reed speaks
 *              (breath_pre), each silenced as the reed speaks (t_gate; the measure: the mouthpiece
 *              pressure periodic and oscillating). The sustain's noise (breath: how much,
 *              breath_mod: how far it pulses with the flow, breath_hp: its colour) and the breath's
 *              fluctuations (wander) are the reed's once it speaks, whatever the attack's are
 *   release    the breath stops, the bore rings out (the pedal lengthens the release)
 *   tilt fwd/back  embouchure: forward firmer (a smaller reed opening: brighter, a little sharp),
 *              back looser (darker, flatter)
 *   tilt sideways  pitch bend (the host moves the note; the bore follows)
 *
 * Every element is passive or bounded: the jet only draws on the mouth pressure, the bore loop has
 * gain < 1, the register hole is a passive shunt; the reed's excursion is clamped; a per-voice guard
 * resets a voice whose state is non-finite or absurd; the master has a DC blocker and a soft ceiling.
 * Cost: about 41k QEMU instructions per block (10 voices, the default performance); per voice-sample
 * one square root and ~100 FP operations, per block the bore's impedance at the note (a few complex
 * evaluations). Memory: the bore delay lines, 10 KB per voice, as long as 192 kHz needs (BulkMemory,
 * 102.4 KB). Below 44.1 kHz the bass clarinet's reed squeals (engines/clarinet.h min_rate).
 */
#ifndef VOICE_CLARINET_H_
#define VOICE_CLARINET_H_

#include <math.h>
#include <stdint.h>

#include "controls.h"
#include "dspmath.h"
#include "params.h"
#include "preset.h"
#include "svf.h"
#ifdef CLARINET_TEST_HOOK
#include <stdio.h>
#endif

namespace clarinet {

using real = float;
using strisy::rmax;
using strisy::rmin;

static constexpr int BLOCK = strisy::BLOCK;
static constexpr int MAX_VOICES = 10;
static constexpr int CTRL = 8; // control rate divider (6 kHz)
// the reed speaks: the mouthpiece pressure's oscillation (peak, re gamma) from SPK_LO (not yet) to
// SPK_HI (fully; a steady pp is about 0.35, ff 0.8)
static constexpr float SPK_LO = 0.05f, SPK_HI = 0.3f;
static constexpr int NCTRL = BLOCK / CTRL;
static_assert(BLOCK % CTRL == 0, "CTRL must divide BLOCK");
// the delay lines are as long as 192 kHz needs (4x 48 kHz: the same bores at every rate)
static constexpr int ALEN = 256; // section A (mouthpiece -> register hole), each direction
static constexpr uint32_t AMASK = ALEN - 1;
static constexpr int BLEN = 1024; // section B (register hole -> lattice), each direction
static constexpr uint32_t BMASK = BLEN - 1;
static constexpr real C0 = 343.0f;
static constexpr real PI = 3.14159265f;

// the bore delay lines, placed by the host (engine SDK)
struct BulkMemory {
	alignas(32) float a_dn[MAX_VOICES][ALEN];
	alignas(32) float a_up[MAX_VOICES][ALEN];
	alignas(32) float b_dn[MAX_VOICES][BLEN];
	alignas(32) float b_up[MAX_VOICES][BLEN];
};

// ---------------------------------------------------------------------------------------------
// Parameters (all floats: the live parameters)

struct Params {
	// level / breath
	real gain;         // output level
	real pc_a, pc_b;   // breath from key pressure: pres * (pc_a + pc_b * pres)
	real lv_hz;        // breath smoothing (critically damped 2-pole)
	real rel, rel_ped; // release time constant (s): rel + rel_ped * pedal^2
	real p_soft;       // mouth pressure at the lightest touch, re the oscillation threshold
	real g_loud;       // mouth pressure at full breath (gamma, units of the closing pressure)
	real g_reg;        // its change per octave up
	real p_curve;      // breath -> mouth pressure exponent
	// reed
	real zeta;     // the slit's opening (Z0 w H sqrt(2 / (rho p_M))) an octave above the lowest note
	real zeta_reg; // octaves of zeta per octave up
	real reed_hz;  // reed resonance
	real reed_q;   // reed damping (the lip)
	real lay;      // contact stiffness when shut, re the reed's
	real emb;      // forward tilt firms the embouchure: octaves of reed opening per unit tilt
	real emb_bend; // and moves the pitch (cents per unit tilt)
	// bore
	real bore_r;  // bore radius (mm): the wall losses
	real wall;    // wall losses re a smooth cylinder's
	real lat_hz;  // tone-hole lattice cutoff
	real lat_q;   // its sharpness
	real reg_pos; // register hole: distance from the reed (m)
	real reg_tau; // its inertance re the bore's (ms): (t_eff / c) (S_bore / S_hole)
	real reg_r;   // its resistance re Z0
	real alt_rel; // the altissimo (a short tube again) from this many semitones above the lowest note
	// radiation
	real tone_hz; // tone low-pass (the room, the microphone)
	real tone_q;  //
	// air
	real breath;     // turbulence (riding on the flow) once the reed speaks: the sustain's noise
	real breath_mod; // how far it follows the flow's pulses within a period (1) or the mean flow (0)
	real breath_hp;  // its colour: a high-pass (Hz; 0 none) on the turbulence and the "t"
	real wander;     // the player's breath is never quite steady: slow and fast fluctuations (re gamma)
	real t_noise;    // the tongue's "t": a noise burst on a strike
	real t_len;      // its decay (s)
	real t_gate;     // how far the reed's speaking silences it (0: not at all, 1: only before it speaks)
	real breath_pre; // the turbulence before the reed speaks, re breath (an air attack's hiss)
	// attack
	real tongue;       // strike sensitivity of the attack: the tongue's hold and release, the accent
	real tongue_noise; // strike sensitivity of the "t" (on its own: the attack keeps its speed)
	real acc_amp;      // the breath built up behind the tongue: overshoot
	real acc_t;        // its decay (s)
	real tongue_t;     // tongue release time of a soft "d" (s); a hard strike is ~16x faster
	real tongue_f;     // the tongue's push on the reed (re p_M)
	real slur;         // articulation on a legato note change
	real tune;         // cents
	real pitch_t;      // the player's ear: pitch correction time constant (s)
	real instrument;   // which instrument (instruments[]): its lowest note, where the registers start
};
static constexpr int NPARAMS = sizeof(Params) / sizeof(real);
static_assert(sizeof(Params) == NPARAMS * sizeof(real), "Params must hold only floats");

// the instruments: the lowest note of each (all holes closed: the registers start from it); the
// parameter instrument picks one
static constexpr int NUM_INSTRUMENTS = 2;
static constexpr real instrument_low[NUM_INSTRUMENTS] = {
    50.0f, // B-flat clarinet, sounding D3
    34.0f, // the low-C bass clarinet, sounding Bb1
};

// the factory presets: presets/clarinet/NN-<name>.preset.json, compiled in (the end of this file)
static constexpr int NUM_PRESETS = 2;
[[nodiscard]] static constexpr const char* preset_name(int i);
[[nodiscard]] static inline Params preset_params(int i);

#ifdef CLARINET_PRESET
static constexpr int DEFAULT_PRESET = CLARINET_PRESET;
#else
static constexpr int DEFAULT_PRESET = 0;
#endif

struct VoiceState {
	// reed: displacement (units of the rest opening) now and before, the force of the last sample
	real x1, x2, pe;
	real gam; // mouth pressure at the end of the last block
	// register hole: its flow, the last incoming sum, its openness (0 shut, 1 open) and target
	real q, aprev, hole, hole_t;
	// lattice and wall-loss filter states, the last radiated flow
	real l1, l2, ls, rprev;
	real DB, DBp; // section B round trip (samples): this block's target, the last block's
	int nbu;      // section B's upward delay (whole samples, fixed while a note is held)
	int dA;       // section A, each way (whole samples)
	int mode;     // 1 chalumeau and altissimo, 3 clarion
	uint32_t w;
	// player
	real lv1, lv2;   // breath smoother
	real amp;        // breath level after the release logic
	real bgate;      // the breath (0 .. 1)
	real gt;         // the tongue: 0 on the reed, 1 off
	real gt_rate;    // its release rate (per control step)
	real acc, tn;    // accent and tongue noise envelopes
	real spk, spk_t; // the reed speaks (0 .. 1): smoothed, and as measured from the last block
	real pk, tcl;    // the mouthpiece pressure's oscillation (peak, held over a period), its last clean crossing
	real um, nl;     // the flow's mean (its positive part), the noise high-pass's low-pass state
	real accf;       // the accent's pressure
	real ty1, ty2;   // tilt smoother
	real gth;        // threshold mouth pressure (gamma)
	real vneg;       // release speed latch
	real note_prev;  //
	real level;      // smoothed peak (activity)
	// the player's ear: the mouthpiece pressure's period from its zero crossings, the bore trimmed
	// onto the note (cents)
	real xlp, xhp, xprev, tlast, pprev, trim;
	real jn, dr; // breath fluctuations: fast (~25 Hz) and slow (~3 Hz)
	uint32_t rng, nrng;
	int held_prev;
	int active;
};

struct Engine {
	awva::InstrumentControls g;
	awva::NoteControls vc[MAX_VOICES];
	int nvoices;
	int preset;
	Params p;           // as set (preset, live edits)
	Params ps;          // smoothed copy used by the DSP
	BulkMemory* bulk; // the host's (init): the bore delay lines
	real sr, dt, pi_sr, ctrl_dt;
	real r48;            // 48 kHz / the rate: a per-sample or per-block rate made for 48 kHz, scaled (exactly 1 there)
	real kj, kd, sj, sd; // the breath fluctuations' per-block rates and their noise scales (any rate)
	real low_note;
	strisy::SvfCoef lv_c, ty_c, tone_c;
	real t1, t2; // tone filter (shared: every voice plays the same preset)
	real dc, dc_c;
	VoiceState vs[MAX_VOICES];
	real vout[BLOCK];
	real mix[BLOCK];
	real gmc[NCTRL]; // mouth pressure at the control points
	real gtc[NCTRL]; // tongue gate at the control points
	real tnc[NCTRL]; // tongue noise at the control points
	real brc[NCTRL]; // turbulence at the control points
};

// ---------------------------------------------------------------------------------------------
// helpers (no libm)

[[nodiscard]] static inline real exp2s(real p) {
	p = rmin(rmax(p, -60.0f), 60.0f);
	const int wi = (int)(p + 64.0f) - 64; // floor
	const real z = p - (real)wi;
	union {
		uint32_t i;
		float f;
	} v = {static_cast<uint32_t>((1 << 23) * (p + 121.2740575f + 27.7280233f / (4.84252568f - z) - 1.49012907f * z))};
	return v.f;
}

// log2 for x > 0 (error ~1e-4)
[[nodiscard]] static inline real log2s(real x) {
	union {
		float f;
		uint32_t i;
	} u = {x};
	const real ex = (real)(int)((u.i >> 23) & 255) - 127.0f;
	u.i = (u.i & 0x007FFFFFu) | 0x3F800000u; // mantissa in [1, 2)
	const real m = u.f;
	return ex + 1.44269504f * (-1.7417939f + (2.8212026f + (-1.4699568f + (0.44717955f - 0.056570851f * m) * m) * m) * m);
}

[[nodiscard]] static inline real note_freq(real note) { return 8.1757989f * exp2s(note * (1.0f / 12)); }

[[nodiscard]] static inline real clampr(real x, real lo, real hi) { return rmin(rmax(x, lo), hi); }

// tan(x) for 0 <= x < ~1.4 ([7/6] Pade)
[[nodiscard]] static inline real tanp(real x) {
	const real x2 = x * x;
	return x * (135135.0f + x2 * (-17325.0f + x2 * (378.0f - x2))) / (135135.0f + x2 * (-62370.0f + x2 * (3150.0f - 28.0f * x2)));
}

// atan(x), any sign (max error ~1e-5)
[[nodiscard]] static inline real atans(real x) {
	const real ax = fabsf(x);
	const bool inv = ax > 1.0f;
	const real t = inv ? 1.0f / ax : ax;
	const real t2 = t * t;
	const real a = t * (0.9998660f + t2 * (-0.3302995f + t2 * (0.1801410f + t2 * (-0.0851330f + t2 * 0.0208351f))));
	const real r = inv ? 1.5707963f - a : a;
	return x < 0.0f ? -r : r;
}

[[nodiscard]] static inline real atan2s(real y, real x) {
	if (x > 0.0f) return atans(y / x);
	if (x < 0.0f) return y >= 0.0f ? atans(y / x) + PI : atans(y / x) - PI;
	return y > 0.0f ? 0.5f * PI : (y < 0.0f ? -0.5f * PI : 0.0f);
}

// an angle wrapped into [-pi, pi]
[[nodiscard]] static inline real wrapp(real a) {
	real t = a * (0.5f / PI);
	t -= (real)(int)(t + (t >= 0.0f ? 0.5f : -0.5f));
	return t * 2.0f * PI;
}

// sin and cos of any angle (error ~1e-6)
static inline void sincosr(real a, real& s, real& c) {
	const real x = wrapp(a);          // [-pi, pi]
	auto sinh_ = [](real y) -> real { // y in [-pi/2, pi/2]
		const real y2 = y * y;
		return y * (1.0f + y2 * (-0.16666667f + y2 * (0.0083333310f + y2 * (-0.00019840874f + y2 * 2.7525562e-6f))));
	};
	real ys = x;
	if (ys > 0.5f * PI) ys = PI - ys;
	else if (ys < -0.5f * PI) ys = -PI - ys;
	s = sinh_(ys);
	real yc = x + 0.5f * PI; // cos x = sin(x + pi/2)
	if (yc > PI) yc -= 2.0f * PI;
	if (yc > 0.5f * PI) yc = PI - yc;
	else if (yc < -0.5f * PI) yc = -PI - yc;
	c = sinh_(yc);
}

[[nodiscard]] static inline float fsqrt(float x) {
#if defined(__arm__) && defined(__ARM_FP)
	float r;
	__asm__("vsqrt.f32 %0, %1"
	        : "=t"(r)
	        : "t"(x));
	return r;
#else
	return __builtin_sqrtf(x);
#endif
}

[[nodiscard]] static inline real block_noise(uint32_t& r) {
	r = r * 1664525u + 1013904223u;
	return (real)(int32_t)r * 4.656613e-10f;
}

[[nodiscard]] static inline real sat(real x) {
	x = clampr(x, -1.5f, 1.5f);
	return x * (1.0f - 0.148148148f * x * x);
}

struct Cx {
	real re, im;
};
[[nodiscard]] static inline Cx cmul(Cx a, Cx b) { return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re}; }
[[nodiscard]] static inline Cx cinv(Cx a) {
	const real d = 1.0f / rmax(a.re * a.re + a.im * a.im, 1e-30f);
	return {a.re * d, -a.im * d};
}
[[nodiscard]] static inline Cx cexpi(real a) {
	Cx r;
	sincosr(a, r.im, r.re);
	return r;
}

// ---------------------------------------------------------------------------------------------
// setup

static inline void clear_lines(Engine& e, int v) {
	for (int i = 0; i < ALEN; i++) e.bulk->a_dn[v][i] = e.bulk->a_up[v][i] = 0.0f;
	for (int i = 0; i < BLEN; i++) e.bulk->b_dn[v][i] = e.bulk->b_up[v][i] = 0.0f;
}

static inline void reset_voice(Engine& e, VoiceState& s, int v) {
	s.x1 = s.x2 = s.pe = s.gam = 0.0f;
	s.q = s.aprev = s.hole = s.hole_t = 0.0f;
	s.l1 = s.l2 = s.ls = s.rprev = 0.0f;
	s.DB = s.DBp = 100.0f;
	s.nbu = 50;
	s.dA = 20;
	s.mode = 1;
	s.w = 0;
	s.lv1 = s.lv2 = s.amp = s.bgate = 0.0f;
	s.gt = 1.0f;
	s.gt_rate = 0.0f;
	s.acc = s.tn = s.accf = 0.0f;
	s.ty1 = s.ty2 = 0.0f;
	s.gth = 0.34f;
	s.vneg = 0.0f;
	s.level = 0.0f;
	s.jn = s.dr = 0.0f;
	s.spk = s.spk_t = s.pk = s.um = s.nl = 0.0f;
	s.tcl = -1e4f;
	s.xlp = s.xhp = s.xprev = 0.0f;
	s.tlast = -1e4f;
	s.pprev = 0.0f;
	s.trim = 0.0f;
	s.held_prev = 0;
	if (v >= 0 && v < MAX_VOICES) clear_lines(e, v);
}

static inline void update_derived(Engine& e) {
	e.lv_c = strisy::svf_from_g(strisy::tan0(e.pi_sr * CTRL * clampr(e.p.lv_hz, 1.0f, 400.0f)), 0.5f);
}

// the instrument of e.p: its lowest note
static inline void instrument_changed(Engine& e) {
	e.low_note = instrument_low[(int)clampr(e.p.instrument, 0.0f, (real)(NUM_INSTRUMENTS - 1))];
}

// e.p, as a preset leaves it, takes effect (index: the factory preset, -1 a preset by name)
static inline void params_loaded(Engine& e, int index) {
	e.preset = index;
	e.ps = e.p;
	instrument_changed(e);
	update_derived(e);
}

static inline void set_preset(Engine& e, int i) {
	if (i < 0 || i >= NUM_PRESETS) i = 0;
	e.p = preset_params(i);
	params_loaded(e, i);
}

static inline void init(Engine& e, int sample_rate, int nvoices) {
	const real sr = (real)sample_rate;
	e.sr = sr;
	e.dt = 1.0f / sr;
	e.pi_sr = PI / sr;
	e.ctrl_dt = (real)CTRL / sr;
	e.r48 = 48000.0f / sr;
	// the fluctuations were made at 48 kHz (blocks of 32): the same bandwidth and variance at any
	// rate (a one-pole's variance is a / (2 - a) of its input's)
	e.kj = rmin(0.15f * e.r48, 1.0f);
	e.kd = rmin(0.004f * e.r48, 1.0f);
	e.sj = fsqrt((0.15f * (2.0f - e.kj)) / (e.kj * (2.0f - 0.15f)));
	e.sd = fsqrt((0.004f * (2.0f - e.kd)) / (e.kd * (2.0f - 0.004f)));
	e.ty_c = strisy::svf_from_g(strisy::tan0(e.pi_sr * BLOCK * 12.0f), 0.5f);
	e.t1 = e.t2 = 0.0f;
	e.dc = 0.0f;
	e.dc_c = 2.0f * PI * 12.0f / sr;
	e.nvoices = nvoices < MAX_VOICES ? nvoices : MAX_VOICES;
	e.g = awva::instrument_at_rest;
	for (int v = 0; v < MAX_VOICES; v++) {
		VoiceState& s = e.vs[v];
		s = VoiceState();
		reset_voice(e, s, v);
		s.nrng = 0x85EBCA6Bu * (uint32_t)(v + 3) + 999u;
		s.rng = 0x9E3779B9u * (uint32_t)(v + 1) + 12345u;
		s.note_prev = 69.0f;
		s.active = 0;
	}
	for (int v = 0; v < e.nvoices; v++) e.vc[v] = awva::note_at_rest;
	set_preset(e, DEFAULT_PRESET);
}

[[nodiscard]] static inline int active_voices(const Engine& e) {
	int n = 0;
	for (int v = 0; v < e.nvoices; v++) n += e.vs[v].active;
	return n;
}

// ---------------------------------------------------------------------------------------------
// the bore seen from the reed at one frequency

struct Bore {
	Cx rend;  // the lattice's (and walls') reflection at the note
	Cx yh;    // the register hole's admittance re the bore's, x its openness
	real w0;  // the note (radians per sample)
	real dA2; // section A's round trip (samples)
};

// the reflection at the reed for a section B round trip DB
[[nodiscard]] static inline Cx gamma_in(const Bore& b, real DB) {
	const Cx gb = cmul(b.rend, cexpi(-b.w0 * DB));
	Cx gj = gb;
	if (b.yh.re != 0.0f || b.yh.im != 0.0f) {
		// the shunt: pJ = 1 / (1 / (1 + gb) + y / 2), the reflection pJ - 1
		const Cx one_gb = {1.0f + gb.re, gb.im};
		const Cx inv = cinv(one_gb);
		const Cx pj = cinv({inv.re + 0.5f * b.yh.re, inv.im + 0.5f * b.yh.im});
		gj = {pj.re - 1.0f, pj.im};
	}
	return cmul(gj, cexpi(-b.w0 * b.dA2));
}

#ifdef CLARINET_TEST_HOOK
// host analysis: voice 0's reed shut and a unit flow impulse at the first sample of a note (the input
// impedance), its mouthpiece pressure recorded per sample
static int clarinet_dbg_imp = 0;
static long clarinet_dbg_n = 0, clarinet_dbg_len = 0;
static float* clarinet_dbg_p = nullptr;
#endif

// ---------------------------------------------------------------------------------------------
// one voice, one block

static inline void process_voice(Engine& e, int vi) {
	VoiceState& s = e.vs[vi];
	const Params& P = e.ps;
	const awva::NoteControls cc = e.vc[vi];
	const real pres = clampr(cc.pressure, 0.0f, 1.5f);
	// after a release a host may leave the last pressure_rate in place: a positive leftover must not
	// strike or hold a released voice; negative values still set the release speed
	const real vpres = clampr(cc.pressure_rate, -1.0f, pres > 0.0f ? 1.0f : 0.0f);
	const real vp1 = rmax(vpres - 0.02f, 0.0f);
	const real vplev = rmin(rmax(P.tongue, 0.0f) * vp1 / (0.4f + vp1), 1.0f);
	const real vnlev = rmin(rmax(P.tongue_noise, 0.0f) * vp1 / (0.4f + vp1), 1.0f);
	const real low = e.low_note;
	real note = clampr(cc.pitch, 0.0f, 132.0f);
	while (note > low + 46.0f) note -= 12.0f;
	while (note < 30.0f) note += 12.0f; // (the delay lines' longest bore)
	const bool held = pres > 0.0f;
	bool start = false;
	if (!s.active) {
		if (!held && vplev <= 0.0f) {
			s.note_prev = note;
			s.held_prev = 0;
			return;
		}
		s.active = 1;
		start = true;
	}
	const bool onset = held && !s.held_prev; // a new breath (from silence or a ringing tail)
	s.held_prev = held;

	// ---- block rate: the fingering, the reed, the bore for this note ---------------------------
	const bool jump = start || fabsf(note - s.note_prev) >= 1.0f;
	if (jump && !start && held && !onset) s.acc = rmax(s.acc, P.slur); // legato change: a small articulation
	if (jump) s.vneg = 0.0f;
	s.note_prev = note;
	if (start) {
		s.x1 = s.x2 = s.pe = 0.0f;
		s.lv1 = s.lv2 = s.amp = s.bgate = 0.0f;
		s.gam = 0.0f;
		s.acc = s.accf = s.tn = 0.0f;
		s.spk = s.spk_t = s.pk = s.um = s.nl = 0.0f;
		s.tcl = -1e4f;
	}
	// the tongue: a strike holds the reed and lets it go (faster for a harder one); a press
	// without one is a breath attack
	if (onset || (held && vplev > s.acc + 0.05f)) {
		if (vplev > 0.05f) {
			s.gt = 0.0f;
			s.gt_rate = rmin(e.ctrl_dt / rmax(P.tongue_t * exp2s(-4.0f * vplev), 0.0005f), 1.0f);
		} else if (onset) {
			s.gt = 1.0f;
		}
	}

	const real by = clampr(cc.timbre, -1.0f, 1.0f);
	if (start) {
		s.ty1 = 0.0f;
		s.ty2 = by;
	}
	real tilt;
	{
		real ty1 = s.ty1, ty2 = s.ty2, t1;
		SVF_TICK(by, ty1, ty2, e.ty_c, t1, tilt);
		s.ty1 = ty1;
		s.ty2 = ty2;
	}
	tilt = clampr(tilt, -1.0f, 1.0f);
	// the breath's life: a fast flutter and a slow wander (each about unit variance)
	s.jn += e.kj * (4.5f * e.sj * block_noise(s.rng) - s.jn);
	s.jn = clampr(s.jn, -4.0f, 4.0f);
	s.dr += e.kd * (40.0f * e.sd * block_noise(s.rng) - s.dr);
	s.dr = clampr(s.dr, -4.0f, 4.0f);
	const real wob = 1.0f + clampr(P.wander, 0.0f, 0.2f) * (0.4f * s.jn + 0.8f * s.dr);

	// the fingering: register by the note re the lowest (the register is kept through bends)
	if (jump) {
		const real rel = note - low;
		// above the clarion's top a short quarter-wave tube (the altissimo's vented fingerings
		// leave one strong resonance at the note)
		s.mode = (rel < 18.5f || rel >= clampr(P.alt_rel, 19.0f, 40.0f)) ? 1 : 3;
	}
	if (jump) {
		s.trim = 0.0f;
		s.tlast = -1e4f;
	}
	const real cents = P.tune + P.emb_bend * tilt;
	const real fn = rmin(note_freq(note) * exp2s(cents * (1.0f / 1200)), 0.2f * e.sr); // the note played
	const real f0 = rmin(fn * exp2s(s.trim * (1.0f / 1200)), 0.2f * e.sr);             // the bore's aim
	const real w0 = 2.0f * PI * f0 * e.dt;
	const real oct = (note - low - 12.0f) * (1.0f / 12);
	// the embouchure: forward firms the lip, the opening H x e (zeta ~ sqrt(H), p_M ~ H)
	const real eo = exp2s(-clampr(P.emb, -2.0f, 2.0f) * tilt);
	const real zeta = clampr(P.zeta * exp2s(P.zeta_reg * oct), 0.02f, 2.0f) * fsqrt(eo);
	// reed: a two-pole resonator of unit static gain, driven by the last sample's pressure
	real ra1, ra2, rb0;
	Cx hr;
	{
		const real wr = 2.0f * PI * clampr(P.reed_hz, 300.0f, 0.3f * e.sr) * e.dt;
		const real qr = clampr(P.reed_q, 0.02f, 1.5f);
		const real rr = exp2s(-1.442695f * 0.5f * qr * wr);
		real sn, cs;
		sincosr(wr * fsqrt(1.0f - 0.25f * qr * qr), sn, cs);
		ra1 = 2.0f * rr * cs;
		ra2 = rr * rr;
		rb0 = 1.0f - ra1 + ra2;
		// its response at the note: b0 z^-1 / (1 - a1 z^-1 + a2 z^-2)
		const Cx z1 = cexpi(-w0), z2 = cexpi(-2.0f * w0);
		hr = cmul({rb0 * z1.re, rb0 * z1.im}, cinv({1.0f - ra1 * z1.re + ra2 * z2.re, -ra1 * z1.im + ra2 * z2.im}));
	}
	// the lattice cutoff (a two-pole) and the walls (a one-pole fitted to exp(-alpha(f) L) at f0
	// and 6 f0, alpha ~ sqrt(f) / r): the round trip D of the fingering
	const real t0 = tanp(rmin(0.5f * w0, 1.3f));
	const real gs = tanp(e.pi_sr * clampr(P.lat_hz, 100.0f, 0.4f * e.sr));
	const real lq = clampr(P.lat_q, 0.3f, 0.7071f);
	const strisy::SvfCoef lc = strisy::svf_from_g(gs, lq);
	const real Dfing = (real)s.mode * 0.5f * e.sr / f0; // the fingering's round trip (samples)
	real g0, Gl;
	{
		const real Lrt = Dfing * e.dt * C0; // metres
		const real kw = rmax(P.wall, 0.0f) * 2.96e-5f / (clampr(P.bore_r, 2.0f, 30.0f) * 0.001f) * Lrt;
		const real f1 = f0, f2 = rmin(6.0f * f0, 0.4f * e.sr);
		const real A1 = exp2s(-1.442695f * kw * fsqrt(f1)), A2 = exp2s(-1.442695f * kw * fsqrt(f2));
		const real rho = (A1 * A1) / rmax(A2 * A2, 1e-12f);
		real fc = 0.45f * e.sr;
		if (rho > 1.0001f) {
			const real fc2 = (f2 * f2 - rho * f1 * f1) / (rho - 1.0f);
			fc = fc2 > 1.0f ? rmin(fsqrt(fc2), 0.45f * e.sr) : f2;
		}
		g0 = rmin(A1 * fsqrt(1.0f + (f1 / fc) * (f1 / fc)), 0.999f);
		const real gl = tanp(e.pi_sr * rmin(fc, 0.42f * e.sr));
		Gl = gl / (1.0f + gl);
		// the end's reflection at the note: -g0 / (1 + i W_l) / (1 - W_s^2 + i W_s / Q)
		Bore b;
		const real Wl = t0 / gl, Ws = t0 / gs;
		b.rend = cmul(cinv({1.0f, Wl}), cinv({1.0f - Ws * Ws, Ws / lq}));
		b.rend = {-g0 * b.rend.re, -g0 * b.rend.im};
		b.w0 = w0;
		// register hole: open in the clarion and above
		s.hole_t = s.mode > 1 ? 1.0f : 0.0f;
		if (jump) s.hole = s.hole_t;
		const real tau = clampr(P.reg_tau, 0.05f, 20.0f) * 0.001f;
		const real ww = 2.0f * e.sr * t0; // warped
		const real rh = clampr(P.reg_r, 0.0f, 5.0f);
		b.yh = s.hole > 0.0f ? cinv({rh, ww * tau}) : Cx{0.0f, 0.0f};
		b.yh = {b.yh.re * s.hole, b.yh.im * s.hole};
		// the fingering's round trip less the end's phase delay
		const real Deff = Dfing + atan2s(-b.rend.im, -b.rend.re) / w0; // (arg of the positive reflection)
		if (jump) {
			s.dA = (int)clampr(P.reg_pos / C0 * e.sr + 0.5f, 2.0f, (real)(ALEN - 2));
			// a short fingering: section A as long as it can be (the hole's place matters only open)
			const int amax = (int)(0.5f * (Deff - 4.0f));
			if (s.dA > amax) s.dA = amax < 1 ? 1 : amax;
		}
		b.dA2 = 2.0f * (real)s.dA;
		// the oscillation condition at the note: Im(Y_in) = Im(Y_reed), Y = (1 - G) / (1 + G);
		// Y_reed = zeta sqrt(gamma) H_r - zeta (1 - gamma) / (2 sqrt(gamma)): the reed's reactance
		const real gm = clampr(s.gam > 0.05f ? s.gam : s.gth, 0.05f, 1.5f);
		const real yri = zeta * fsqrt(gm) * hr.im;
		const real lim_lo = 3.0f, lim_hi = (real)(BLEN - 4);
		if (jump) s.DB = clampr(Deff - b.dA2, lim_lo, 2.0f * lim_hi); // the first guess
		const int iters = jump ? 4 : 1;
		real DB = s.DB;
		Cx gin;
		auto solve = [&](int n) {
			gin = gamma_in(b, DB);
			for (int it = 0; it < n; it++) {
				const real mag = fsqrt(gin.re * gin.re + gin.im * gin.im);
				const real opm = 1.0f + mag;
				const real phi_t = clampr(-yri * opm * opm / (2.0f * rmax(mag, 0.05f)), -0.5f, 0.5f);
				const real f = wrapp(atan2s(gin.im, gin.re) - phi_t);
				const Cx g2 = gamma_in(b, DB + 0.5f);
				real der = wrapp(atan2s(g2.im, g2.re) - atan2s(gin.im, gin.re)) * 2.0f;
				if (der > -0.2f * w0) der = -w0;
				DB = clampr(DB - clampr(f / der, -0.1f * Dfing, 0.1f * Dfing), lim_lo, 2.0f * lim_hi);
				gin = gamma_in(b, DB);
			}
		};
		solve(iters);
		// a short tube (the register hole shut, so where the bore is split doesn't matter): when
		// section B can't get any shorter, section A gives it samples back (else the bore stays too
		// long, flat, with the ear's trim at its end)
		for (int g = 0; g < 16 && s.mode == 1 && DB <= lim_lo + 1e-3f && s.dA > 1; g++) {
			s.dA--;
			b.dA2 = 2.0f * (real)s.dA;
			DB += 2.0f;
			if (!jump) s.DBp += 2.0f; // (the block's ramp starts where the bore was)
			solve(2);
		}
		s.DB = DB;
		if (jump) {
			s.DBp = DB;
			s.nbu = (int)(0.5f * DB);
		}
		// the threshold: zeta (3 gamma - 1) / (2 sqrt(gamma)) = 1 / Zp (the quasi-static reed)
		const real mag = rmin(fsqrt(gin.re * gin.re + gin.im * gin.im), 0.995f);
		const real zp = (1.0f + mag) / (1.0f - mag);
		const real bq = 2.0f / (zeta * zp);
		const real sq = (bq + fsqrt(bq * bq + 12.0f)) * (1.0f / 6);
		s.gth = clampr(sq * sq, 0.2f, 0.9f);
	}
	// the register hole's shunt (trapezoidal): q' = (a - (r + 1/2) q) / tau
	real cq, ca;
	{
		const real tau = clampr(P.reg_tau, 0.05f, 20.0f) * 0.001f;
		const real G = 0.5f * e.dt / tau;
		const real k = clampr(P.reg_r, 0.0f, 5.0f) + 0.5f;
		cq = (1.0f - G * k) / (1.0f + G * k);
		ca = G / (1.0f + G * k);
	}

	// ---- control rate: breath, tongue, accent ----------------------------------------------------
	if (vpres > 0.1f) s.vneg = 0.0f;
	if (held && vpres < 0.0f) s.vneg = rmax(s.vneg, -vpres);
	const real ped = clampr(e.g.sustain, 0.0f, 1.0f);
	const real rel_tc = rmax(P.rel + P.rel_ped * ped * ped, 0.004f) / (1.0f + 3.0f * s.vneg);
	const real fac = 1.0f - e.ctrl_dt / (held ? 0.03f : rel_tc);
	const real fac_acc = 1.0f - e.ctrl_dt / rmax(P.acc_t, 0.005f);
	const real fac_tn = 1.0f - e.ctrl_dt / clampr(P.t_len, 0.001f, 0.05f);
	const real fac_sp = rmin(e.ctrl_dt / 0.002f, 1.0f);
	const real tg = clampr(P.t_gate, 0.0f, 1.0f), bpre = clampr(P.breath_pre, 0.0f, 4.0f);
	const real br = rmax(P.breath, 0.0f);
	const real fac_bg = rmin(e.ctrl_dt / (held ? 0.004f : rel_tc), 1.0f);
	const real pcl = pres * (P.pc_a + P.pc_b * pres);
	const real g_lo = s.gth * clampr(P.p_soft, 0.5f, 3.0f);
	const real g_hi = rmax(clampr(P.g_loud * exp2s(P.g_reg * oct), 0.1f, 1.5f), g_lo + 0.02f);
	const real pcv = clampr(P.p_curve, 0.3f, 3.0f);
	{
		real lv1 = s.lv1, lv2 = s.lv2, amp = s.amp, acc = s.acc, accf = s.accf, tn = s.tn, bg = s.bgate, gt = s.gt, sp = s.spk;
		const real fac_af = rmin(e.ctrl_dt / rmax(rmax(P.tongue_t, 0.0005f) * exp2s(-4.0f * acc), 0.001f), 1.0f);
		const strisy::SvfCoef lcv = e.lv_c;
		for (int k = 0; k < NCTRL; k++) {
			acc = rmax(acc * fac_acc, vplev);
			tn = rmax(tn * fac_tn, vnlev);
			sp += fac_sp * (s.spk_t - sp);
			real t1, ls;
			SVF_TICK(rmin(pcl, 2.0f), lv1, lv2, lcv, t1, ls);
			amp = rmax(rmax(ls, 0.0f), amp * fac);
			accf += fac_af * (P.acc_amp * acc - accf);
			bg += fac_bg * ((held ? 1.0f : 0.0f) - bg);
			gt += s.gt_rate * (1.0f - gt);
			// breath -> mouth pressure (amp^p_curve: exp2s(p_curve log2 amp) without libm)
			const real a = rmin(amp + accf, 1.5f);
			const real ap = (pcv != 1.0f && a > 1e-6f) ? exp2s(pcv * log2s(a)) : a;
			e.gmc[k] = clampr(bg * (g_lo + (g_hi - g_lo) * ap) * wob / eo, 0.0f, 1.6f);
			e.gtc[k] = gt;
			// the "t" and the turbulence before and after the reed speaks
			e.tnc[k] = P.t_noise * tn * tn * (1.0f - tg * sp);
			e.brc[k] = br * (bpre + (1.0f - bpre) * sp);
		}
		s.lv1 = lv1;
		s.lv2 = lv2;
		s.amp = amp;
		s.acc = acc;
		s.accf = accf;
		s.tn = tn;
		s.spk = sp;
		s.bgate = bg;
		s.gt = gt;
	}

	// ---- audio rate: reed, bore, register hole, lattice, radiation -------------------------------
	float* __restrict__ Ad = e.bulk->a_dn[vi];
	float* __restrict__ Au = e.bulk->a_up[vi];
	float* __restrict__ Bd = e.bulk->b_dn[vi];
	float* __restrict__ Bu = e.bulk->b_up[vi];
	real x1 = s.x1, x2 = s.x2, pe = s.pe, q = s.q, aprev = s.aprev, l1 = s.l1, l2 = s.l2, lsv = s.ls, rprev = s.rprev;
	real gam = s.gam, pk = s.pk, tcl = s.tcl - (real)BLOCK, um = s.um, nl = s.nl;
	const real k_xhp = 0.004f * e.r48, k_um = 0.002f * e.r48;
	const real bmod = clampr(P.breath_mod, 0.0f, 1.0f);
	const real a_nhp = rmin(2.0f * PI * clampr(P.breath_hp, 0.0f, 0.25f * e.sr) * e.dt, 1.0f);
	const real k_pk = 1.0f - 0.5f * fn * e.dt; // (the peak held over about two periods) // (time constants made at 48 kHz)
	uint32_t w = s.w, nr = s.nrng;
	const uint32_t dA = (uint32_t)s.dA, nbu = (uint32_t)s.nbu;
	// section B's downward (fractional) delay ramps over the block
	const real dd0 = clampr(s.DBp - (real)s.nbu, 2.0f, (real)(BLEN - 4));
	const real dd1 = clampr(s.DB - (real)s.nbu, 2.0f, (real)(BLEN - 4));
	const real ddd = (dd1 - dd0) * (1.0f / BLOCK);
	real dd = dd0;
	const real hole = s.hole;
	const real ztz = zeta;
	const real lay = clampr(P.lay, 0.0f, 20.0f);
	const real tf = clampr(P.tongue_f, 0.0f, 5.0f);
	const real gain = clampr(P.gain, 0.0f, 100.0f) * eo;
	real* __restrict__ out = e.vout;
	real peak = 0.0f;
	// pitch tracker: mouthpiece pressure low-passed (~1.3 f), DC removed, upward zero crossings
	const real a_lp = clampr(2.0f * PI * 1.3f * fn * e.dt, 0.001f, 0.9f);
	real xlp = s.xlp, xhp = s.xhp, xprev = s.xprev, tlast = s.tlast - (real)BLOCK, pprev = s.pprev;
	int ncross = 0;
	real per_sum = 0.0f;
	for (int k = 0; k < NCTRL; k++) {
		const real dg = (e.gmc[k] - gam) * (1.0f / CTRL);
		const real gtk = e.gtc[k];
		const real tnl = e.tnc[k];
		const real brk = e.brc[k];
		const real tpush = tf * (1.0f - gtk);
		for (int j = 0; j < CTRL; j++) {
			gam += dg;
			// the reed: driven by the last sample's pressure difference, the lay when shut
			real x = ra1 * x1 - ra2 * x2 + rb0 * pe;
			x = clampr(x, -1.6f, 2.0f);
			x2 = x1;
			x1 = x;
			// the wave from the bore at the reed, and the flow through the slit (exact)
			const real pin = Au[(w - dA) & AMASK];
			const real W = ztz * rmax(1.0f + x, 0.0f) * gtk;
			const real Pd = gam - 2.0f * pin;
			const real sq = fsqrt(W * W + 4.0f * fabsf(Pd));
			real u = 0.5f * W * (sq - W);
			u = Pd < 0.0f ? -u : u;
			nr = nr * 1664525u + 1013904223u;
			real nz = (real)(int32_t)nr * 4.656613e-10f;
			nl += a_nhp * (nz - nl);
			nz -= nl;
			const real up = rmax(u, 0.0f);
			um += k_um * (up - um);
			u += nz * (brk * (bmod * up + (1.0f - bmod) * um) + tnl * gtk);
#ifdef CLARINET_TEST_HOOK
			if (clarinet_dbg_imp && vi == 0) u = clarinet_dbg_n == 0 ? 1.0f : 0.0f;
#endif
			const real p = 2.0f * pin + u;
#ifdef CLARINET_TEST_HOOK
			if (vi == 0 && clarinet_dbg_p && clarinet_dbg_n < clarinet_dbg_len) clarinet_dbg_p[clarinet_dbg_n] = p;
			if (vi == 0) clarinet_dbg_n++;
#endif
			pe = p - gam - tpush + lay * rmax(-1.0f - x, 0.0f);
			xlp += a_lp * (p - xlp);
			xhp += k_xhp * (xlp - xhp);
			{
				const real xc = xlp - xhp;
				if (xprev < 0.0f && xc >= 0.0f) {
					const real tc = (real)(k * CTRL + j) - xc / (xc - xprev);
					if (tlast > -4000.0f) {
						const real per = tc - tlast;
						if (fabsf(per - pprev) < 0.03f * per) { // a clean, periodic regime
							per_sum += per;
							ncross++;
							tcl = tc;
						}
						pprev = per;
					}
					tlast = tc;
				}
				xprev = xc;
				pk = rmax(fabsf(xc), pk * k_pk);
			}
			Ad[w & AMASK] = pin + u;
			// the register hole (a shunt; shut: a plain junction)
			const real a1v = Ad[(w - dA) & AMASK];
			const real b2v = Bu[(w - nbu) & BMASK];
			const real av = a1v + b2v;
			q = cq * q + ca * hole * (av + aprev);
			aprev = av;
			const real pj = av - 0.5f * q;
			Au[w & AMASK] = pj - a1v;
			Bd[w & BMASK] = pj - b2v;
			// the lattice end: the wave arriving (Lagrange, 3rd order), its reflection
			dd += ddd;
			const int di = (int)dd;
			const real fr = dd - (real)di;
			const uint32_t i0 = w - (uint32_t)di + 1u;
			const real y0 = Bd[i0 & BMASK], y1 = Bd[(i0 - 1u) & BMASK], y2 = Bd[(i0 - 2u) & BMASK], y3 = Bd[(i0 - 3u) & BMASK];
			const real fm1 = fr - 1.0f, fm2 = fr - 2.0f, fp1 = fr + 1.0f;
			const real h0 = -fr * fm1 * fm2 * (1.0f / 6), h1 = fp1 * fm1 * fm2 * 0.5f, h2 = -fp1 * fr * fm2 * 0.5f, h3 = fp1 * fr * fm1 * (1.0f / 6);
			const real inc = h0 * y0 + h1 * y1 + h2 * y2 + h3 * y3;
			real v1, lp;
			SVF_TICK(inc, l1, l2, lc, v1, lp);
			const real vl = (lp - lsv) * Gl;
			const real lo = vl + lsv;
			lsv = lo + vl;
			const real refl = -g0 * lo;
			Bu[w & BMASK] = refl;
			w = (w + 1u) & (BMASK);
			// radiation: the flow through the open end, differentiated
			const real rad = inc - refl;
			const real y = (rad - rprev) * gain;
			rprev = rad;
			out[k * CTRL + j] = y;
			peak = rmax(peak, fabsf(y));
		}
	}
	s.x1 = x1;
	s.x2 = x2;
	s.pe = pe;
	s.q = q;
	s.aprev = aprev;
	s.l1 = l1;
	s.l2 = l2;
	s.ls = lsv;
	s.rprev = rprev;
	s.gam = gam;
	s.w = w;
	s.nrng = nr;
	s.pk = pk;
	s.um = um;
	s.nl = nl;
	s.tcl = rmax(tcl, -1e5f);
	// the reed speaks: the mouthpiece pressure is periodic (a clean crossing within 1.5 periods)
	// and oscillates (re the mouth pressure); not while the tongue is on the reed
	{
		const bool periodic = (real)BLOCK - tcl < 1.5f * e.sr / fn;
		s.spk_t = (s.gt < 0.5f || !periodic) ? 0.0f : clampr((pk / rmax(gam, 0.05f) - SPK_LO) * (1.0f / (SPK_HI - SPK_LO)), 0.0f, 1.0f);
	}
	s.DBp = s.DB;
	s.hole += rmin(0.2f * e.r48, 1.0f) * (s.hole_t - s.hole);
	s.xlp = xlp;
	s.xhp = xhp;
	s.xprev = xprev;
	s.tlast = tlast;
	s.pprev = pprev;
	// the player's ear: the bore trimmed slowly onto the note (the reed's pull grows with the level)
	if (ncross > 0 && held) {
		const real err = 1200.0f * log2s(e.sr * (real)ncross / (rmax(per_sum, 1.0f) * fn));
		if (fabsf(err) < 150.0f) s.trim = clampr(s.trim - clampr(err * rmin(per_sum * e.dt / clampr(P.pitch_t, 0.002f, 1.0f), 0.5f), -6.0f, 6.0f), -100.0f, 100.0f);
	}

	// guard: non-finite or absurd state -> reset this voice and drop its block
	const real st = fabsf(s.x1) + fabsf(s.pe) + fabsf(s.q) + fabsf(s.l1) + fabsf(s.l2) + fabsf(s.ls) + fabsf(s.rprev) + fabsf(s.amp) + fabsf(s.gam);
	if (!(peak < 1e3f) || !(st < 1e5f)) {
		reset_voice(e, s, vi);
		s.active = 0;
		return;
	}
	real* __restrict__ mix = e.mix;
	for (int n = 0; n < BLOCK; n++) mix[n] += out[n];
	s.level += 0.05f * (peak - s.level);
	// idle once released and faded out; the state is kept
	if (!held && vplev <= 0.0f && s.bgate < 1e-4f && s.amp < 1e-5f && s.acc < 1e-4f && peak < 1e-5f) s.active = 0;
}

static inline void process(Engine& e, real* __restrict__ output) {
	// smooth the parameters towards the set values (live edits glide in ~13 ms instead of stepping)
	{
		const real* __restrict__ pt = reinterpret_cast<const real*>(&e.p);
		real* __restrict__ ps = reinterpret_cast<real*>(&e.ps);
		for (int i = 0; i < NPARAMS; i++) {
			const real d = pt[i] - ps[i];
			// (the snap is relative: a large value's last steps would be below its float resolution)
			ps[i] = fabsf(d) <= 1e-5f * rmax(fabsf(pt[i]), 1.0f) ? pt[i] : ps[i] + rmin(0.05f * e.r48, 1.0f) * d;
		}
	}
	e.tone_c = strisy::svf_pade(e.pi_sr * clampr(e.ps.tone_hz, 500.0f, 0.45f * e.sr), clampr(e.ps.tone_q, 0.5f, 2.0f));
	for (int n = 0; n < BLOCK; n++) e.mix[n] = 0.0f;
	for (int v = 0; v < e.nvoices; v++) process_voice(e, v);
	// master: the tone low-pass, a DC blocker, then a gentle soft clip with a ceiling of 4.2
	real dc = e.dc, t1 = e.t1, t2 = e.t2;
	const real dcc = e.dc_c;
	const strisy::SvfCoef tc = e.tone_c;
	for (int n = 0; n < BLOCK; n++) {
		real tb, x;
		SVF_TICK(e.mix[n], t1, t2, tc, tb, x);
		dc += dcc * (x - dc);
		output[n] = 4.2f * sat((x - dc) * (1.0f / 4.2f));
	}
	const bool ok = fabsf(dc) < 1e3f && fabsf(t1) < 1e4f && fabsf(t2) < 1e4f;
	e.dc = ok ? dc : 0.0f;
	e.t1 = ok ? t1 : 0.0f;
	e.t2 = ok ? t2 : 0.0f;
}

// ---------------------------------------------------------------------------------------------
// live parameters

struct ParamEntry {
	strisy::ParamDesc d;
	real Params::*m;
};

static constexpr strisy::ParamKind C_ = strisy::ParamKind::continuous;

static constexpr ParamEntry param_table[] = {
    {{"gain", "level", 0.1f, 100.0f, C_}, &Params::gain},
    {{"pres_lin", "level", 0.0f, 1.0f, C_}, &Params::pc_a},
    {{"pres_sq", "level", 0.0f, 1.5f, C_}, &Params::pc_b},
    {{"pres_hz", "level", 2.0f, 200.0f, C_}, &Params::lv_hz},
    {{"release", "level", 0.005f, 1.0f, C_}, &Params::rel},
    {{"rel_pedal", "level", 0.0f, 3.0f, C_}, &Params::rel_ped},
    {{"p_soft", "breath", 0.8f, 2.0f, C_}, &Params::p_soft}, // lightest touch re the threshold
    {{"g_loud", "breath", 0.3f, 1.4f, C_}, &Params::g_loud}, // ff mouth pressure (gamma)
    {{"g_reg", "breath", -0.5f, 0.5f, C_}, &Params::g_reg},
    {{"p_curve", "breath", 0.3f, 3.0f, C_}, &Params::p_curve},
    {{"zeta", "reed", 0.05f, 1.2f, C_}, &Params::zeta},
    {{"zeta_reg", "reed", -1.0f, 1.0f, C_}, &Params::zeta_reg},
    {{"reed_hz", "reed", 500.0f, 6000.0f, C_}, &Params::reed_hz},
    {{"reed_q", "reed", 0.05f, 1.2f, C_}, &Params::reed_q},
    {{"lay", "reed", 0.0f, 12.0f, C_}, &Params::lay},
    {{"emb", "reed", -1.5f, 1.5f, C_}, &Params::emb},
    {{"emb_bend", "reed", -100.0f, 100.0f, C_}, &Params::emb_bend},
    {{"bore_r", "bore", 3.0f, 20.0f, C_}, &Params::bore_r},
    {{"wall", "bore", 0.0f, 5.0f, C_}, &Params::wall},
    {{"lat_hz", "bore", 300.0f, 5000.0f, C_}, &Params::lat_hz},
    {{"lat_q", "bore", 0.3f, 0.7f, C_}, &Params::lat_q}, // (passive: |R| <= 1)
    {{"reg_pos", "bore", 0.03f, 0.42f, C_}, &Params::reg_pos},
    {{"reg_tau", "bore", 0.1f, 10.0f, C_}, &Params::reg_tau},
    {{"reg_r", "bore", 0.0f, 2.0f, C_}, &Params::reg_r},
    {{"alt_rel", "bore", 19.0f, 40.0f, C_}, &Params::alt_rel},
    {{"tone_hz", "radiation", 1000.0f, 20000.0f, C_}, &Params::tone_hz},
    {{"tone_q", "radiation", 0.5f, 2.0f, C_}, &Params::tone_q},
    {{"breath", "air", 0.0f, 1.0f, C_}, &Params::breath},
    {{"breath_mod", "air", 0.0f, 1.0f, C_}, &Params::breath_mod},
    {{"breath_hp", "air", 0.0f, 4000.0f, C_}, &Params::breath_hp},
    {{"wander", "air", 0.0f, 0.2f, C_}, &Params::wander},
    {{"t_noise", "air", 0.0f, 1.0f, C_}, &Params::t_noise},
    {{"t_len", "air", 0.001f, 0.05f, C_}, &Params::t_len},
    {{"t_gate", "air", 0.0f, 1.0f, C_}, &Params::t_gate},
    {{"breath_pre", "air", 0.0f, 4.0f, C_}, &Params::breath_pre},
    {{"tongue", "attack", 0.0f, 2.0f, C_}, &Params::tongue},
    {{"tongue_noise", "attack", 0.0f, 2.0f, C_}, &Params::tongue_noise},
    {{"acc_level", "attack", 0.0f, 1.0f, C_}, &Params::acc_amp},
    {{"acc_decay", "attack", 0.01f, 0.3f, C_}, &Params::acc_t},
    {{"tongue_t", "attack", 0.001f, 0.05f, C_}, &Params::tongue_t},
    {{"tongue_f", "attack", 0.0f, 4.0f, C_}, &Params::tongue_f},
    {{"slur", "attack", 0.0f, 1.0f, C_}, &Params::slur},
    {{"tune", "attack", -100.0f, 100.0f, C_}, &Params::tune},
    {{"pitch_t", "attack", 0.003f, 0.3f, C_}, &Params::pitch_t},
    {{"instrument", "bore", 0.0f, 1.0f, strisy::ParamKind::steps}, &Params::instrument}, // instruments[]: the lowest note
};
static constexpr int NUM_LIVE_PARAMS = sizeof(param_table) / sizeof(param_table[0]);
static_assert(NUM_LIVE_PARAMS == NPARAMS, "every Params field is exposed once");

} // namespace clarinet

namespace strisy {
template <> struct EngineParams<clarinet::Engine> {
	static constexpr int count = clarinet::NUM_LIVE_PARAMS;
	[[nodiscard]] static constexpr const ParamDesc* desc(int i) { return (i >= 0 && i < count) ? &clarinet::param_table[i].d : nullptr; }
	[[nodiscard]] static float* ptr(clarinet::Engine& e, int i) { return (i >= 0 && i < count) ? &(e.p.*(clarinet::param_table[i].m)) : nullptr; }
	static void changed(clarinet::Engine& e, int i) {
		if (i >= 0 && i < count && clarinet::param_table[i].m == &clarinet::Params::instrument) clarinet::instrument_changed(e);
		clarinet::update_derived(e);
	}
};
} // namespace strisy

// ---------------------------------------------------------------------------------------------
// The factory presets: presets/clarinet/NN-<name>.preset.json, the files themselves
// (presets/clarinet.inc, tools/presets_inc.cmake), each read at compile time; a file that isn't
// every parameter in range doesn't compile (awva/preset.h factory).

namespace clarinet {
#include "presets/clarinet.inc"
static_assert(NUM_FACTORY_PRESETS == NUM_PRESETS, "presets/clarinet/: a file per preset");

[[nodiscard]] static constexpr const awva::preset::Factory<NPARAMS>& factory_preset(int i) {
	return awva::preset::factories<strisy::EngineParams<Engine>, factory_json, NUM_PRESETS>[(i >= 0 && i < NUM_PRESETS) ? i : 0];
}

[[nodiscard]] static constexpr const char* preset_name(int i) { return factory_preset(i).name.c_str(); }

[[nodiscard]] static inline Params preset_params(int i) {
	const awva::preset::Factory<NPARAMS>& f = factory_preset(i);
	Params p;
	for (int k = 0; k < NPARAMS; k++) p.*(param_table[k].m) = f.values[k];
	return p;
}
} // namespace clarinet

#endif // VOICE_CLARINET_H_
