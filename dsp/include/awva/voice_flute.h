// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
/**
 * flute: a physical model of the flute family (concert flute, alto flute, piccolo as presets), in the
 * style of the physical trumpet: nothing sets the pitch but the jet and the bore together. Traits:
 * engines/flute.h.
 *
 * Every voice is a player blowing across the embouchure hole of an open-open pipe:
 *
 *   breath   key pressure -> breath level (pres (a + b pres), smoothed) -> blowing pressure p_m (Pa),
 *            from p_soft to p_loud, rising p_reg octaves per octave of pitch (high notes need more).
 *            The jet leaves the lips at the Bernoulli velocity U = sqrt(2 p_m / rho)
 *   JET      (the jet-drive model: Coltman; Verge, Fabre, Hirschberg et al. 1997; Fletcher & Rossing
 *            ch. 16; the time-domain form of Auvray, Ernoult, Fabre & Lagree, JASA 2014) the acoustic
 *            flow through the embouchure window v deflects the jet at the lips; the deflection is
 *            convected to the edge at about 0.4 U and grows on the way (the jet's instability): at
 *            the edge eta(t) = (h / U) e^{alpha W} v(t - tau), tau = W / (c_p U). The edge splits the
 *            jet: the flow into the pipe Q = b H U (1 + tanh((eta - y0) / b)), b = 2/5 h (a Bickley
 *            profile); y0 the edge's offset from the jet's centre line (even harmonics). The flow
 *            injected on both sides of the edge is a pressure dipole across the window:
 *            dp = -(rho delta_d / S_m) dQ/dt, delta_d = (4 / pi) sqrt(2 h W)
 *   PLAYER   the lip-to-edge distance W is the player's: set per note so that the jet's travel time is
 *            a part theta of the period at the middle breath; a louder breath shortens the travel
 *            time unless the player follows it (w_track: 1 = theta held, 0 = W held). theta too small
 *            for the note: the jet favours the next mode (overblowing); too large: it drops
 *   LOSSES   vortex shedding at the edge: dp_los = -(rho / 2) (v / alpha_vc)^2 sgn(v), solved
 *            implicitly with the pipe's flow (one square root per sample)
 *   NOISE    the jet's turbulence: fluctuations of its deflection (they ride the same instability,
 *            so they are heard through the resonances: breathiness), growing with U; the jet's
 *            own edge noise radiated directly (a band at a Strouhal frequency of U / h, level ~U^3);
 *            the tongue's "t"
 *   BORE     an open-open cylinder: a waveguide (two delay lines, mouth -> open end -> mouth). At the
 *            open end (the first open tone hole) the low frequencies reflect inverted (-g); above the
 *            tone-hole lattice's cutoff they leave (a one-pole low-pass in the reflection); a first-
 *            order allpass is the frequency-dependent end corrections (upper resonances flat). The
 *            mouth end is open too: the source dp is the pressure there
 *   FINGERING  as on the flute: the first octave from the instrument's lowest note on the fundamental,
 *            the second octave overblown (the bore an octave down, the jet set for mode 2), the third
 *            on modes 3 and 4. A soft breath on an overblown note may fall to the lower mode; a hard
 *            one on a low note may jump up: the jet decides
 *   RADIATION  the flow out of the embouchure hole and out of the open end, differentiated (monopole
 *            radiation), with a low-pass for the room/directivity
 * The player's ear trims the bore slowly onto the note (zero crossings of the window flow), as on
 * the trumpet; the trim is clamped to +-150 cents.
 *
 * Playing:
 *   pressure   breath: jet velocity -> dynamics, brightness (the edge saturates), breathiness, and
 *              at the extremes overblowing / falling back
 *   strike     tonguing: the tongue releases the breath (faster for a harder strike), the pressure
 *              built behind it (an accent), a "t" of turbulence: the chiff, and the attack's jet:
 *              released fast and thin, it grows att_gain times more for the first att_t, so the
 *              tone speaks in tens of ms; none: a breath attack (att_soft of it)
 *   release    the breath stops, the pipe rings out (pedal lengthens the release)
 *   timbre     the jet angle and the cover of the embouchure hole: above 0 rolls in (darker,
 *              a little flatter, the edge offset moves), below 0 rolls out
 *   pitch      bends (the host moves the note; the pipe follows)
 *   shake      breath vibrato
 *   ob_amt     (0 by default) overblowing: above ob_thr of the breath the player stops shortening the
 *              lip-to-edge distance as the jet speeds up, its travel time falls and the next mode of
 *              the pipe takes over (an octave from the first register), with the jet's hysteresis
 *
 * The range (v3): inside [range_lo, range_hi] the player is the fitted one, unchanged. Outside it
 * the player keeps the pressure, jet phase and bore losses of the range's edge; above it the note
 * plays on the fundamental of a short pipe, the jet's growth set for the loop's real losses (the
 * delay lines' interpolations) with g_out of margin, its level out_db to meet the edge. Past the
 * pipe's longest (G2) and the jet's highest (6 kHz) the tone fades into the breath over 3 semitones.
 *
 * Every element is passive or bounded: the jet flow is bounded by 2 b H U, the pipe loop gain is
 * < 0.98, the window flow is solved implicitly; a per-voice guard resets a voice whose state is
 * non-finite or absurd; the master has a DC blocker and a soft ceiling at 4.2.
 *
 * Fitted (CMA-ES) to the University of Iowa MIS recordings (flute and alto flute, non-vibrato,
 * pp/mf/ff, every note): per note the steady harmonics, the level across notes and dynamics, the
 * noise between the harmonics, the attack and its rise, the timbre's change with the dynamic, in
 * tune and in its register. The piccolo (v4): the flute's player on the piccolo's geometry, its jet,
 * bore losses, radiation, noise and attack fitted to the VSCO-2 Community Edition piccolo (CC0).
 * The attack (v2) refitted alone, the sustain's parameters fixed: the tone's rise (harmonics 1-3
 * within 3 dB of their sustain) from the key on played phrases (hard strikes <= 50 ms,
 * soft <= 120 ms) and against the recordings' attacks.
 *
 * Cost (QEMU Cortex-M7 instruction counts, 10 voices): the default performance 34.7k
 * (flute) to 36.7k (alto) per block; a stress (10 voices ff, notes jumping every 0.25 s over every
 * register, timbres, shaking) 92k in the worst block. Per voice-sample: two divisions, one square
 * root, four interpolated delay reads, four SVF steps.
 * Sample rates: from 11025 to 192000 Hz the pitch holds 48 kHz's within 0.2 c (the cutoffs at most
 * 0.4 of the rate, the delay lines long enough at 192 kHz); up to 48828 Hz the samples are v4's.
 * Memory: per voice two bore half-lines and a jet line of 1024 floats (12 KB, for 192 kHz), 120 KB
 * in BulkMemory (the wind engine's size).
 */
#ifndef VOICE_FLUTE_H_
#define VOICE_FLUTE_H_

#include <math.h>
#include <stdint.h>

#include "controls.h"
#include "dspmath.h"
#include "params.h"
#include "param_text/flute.inc" // the parameters' text (AWVA_TEXT)
#include "preset.h"
#include "svf.h"
#ifdef FLUTE_TEST_HOOK
#include <stdio.h>
#endif

namespace flute {

using real = float;
using awva::rmax;
using awva::rmin;

static constexpr int BLOCK = awva::BLOCK;
// the voices (arrays sized by it): 10 unless the build says another (AWVA_VOICES_FLUTE)
#ifndef AWVA_VOICES_FLUTE
#define AWVA_VOICES_FLUTE 10
#endif
static constexpr int MAX_VOICES = AWVA_VOICES_FLUTE;
static constexpr int CTRL = 8; // control rate divider (6 kHz)
static constexpr int NCTRL = BLOCK / CTRL;
static_assert(BLOCK % CTRL == 0, "CTRL must divide BLOCK");
// each half of the bore loop: a round trip of up to ~2040 samples, the alto's lowest bore (an octave
// below its second register) at 192 kHz; at 48 kHz the loop uses a quarter of it
static constexpr int BLEN = 1024;
static constexpr uint32_t BMASK = BLEN - 1;
static constexpr real DMAX = (real)(BLEN - 3);
static constexpr int JLEN = 1024; // the jet's travel (window flow history), up to ~1020 samples (192 kHz)
static constexpr uint32_t JMASK = JLEN - 1;
static constexpr real JMAX = (real)(JLEN - 3);
// the delays' reach of v1-v4 (256-sample lines: 253 samples), kept up to 48828 Hz, the rates the
// engine was made and fitted at (it binds on notes far below a range, where the ear trims); above
// that the same reach in time, the longer lines holding it
static constexpr real REACH = 253.0f;
static constexpr real REACH_RATE = 48828.0f;
static constexpr real RHO = 1.2f;
static constexpr real C0 = 343.0f;
static constexpr real PI = 3.14159265f;
static constexpr real CP = 0.4f;         // jet convection velocity re U
static constexpr real SM = 1.0e-4f;      // embouchure window area (m^2)
static constexpr real NOTE_LO = 43.0f;   // the longest pipe the delay lines hold
static constexpr real NOTE_HI = 114.4f;  // the highest tone the jet and the pipe make (6 kHz)
static constexpr real LIMIT_ST = 3.0f;   // beyond either, the tone fades into the breath over this
static constexpr real RESTRIKE_V = 0.3f; // a re-strike within a held note: pressure_rate at least this (v5)
static constexpr real RESTRIKE_P = 0.1f; // and a pressure of at least this

struct BulkMemory {
	alignas(32) float bore[MAX_VOICES][2][BLEN];
	alignas(32) float jet[MAX_VOICES][JLEN];
};

// ---------------------------------------------------------------------------------------------
// Parameters (all floats: the live parameters)

struct Params {
	// level / breath
	real gain;         // output level
	real pc_a, pc_b;   // breath from key pressure: pres * (pc_a + pc_b * pres)
	real lv_hz;        // breath smoothing (critically damped 2-pole)
	real rel, rel_ped; // release time constant (s): rel + rel_ped * pedal^2
	real p_soft;       // blowing pressure at the lightest touch (Pa, at the reference note C5)
	real p_loud;       // at full breath (Pa)
	real p_reg;        // octaves of pressure per octave of pitch
	real p_curve;      // breath -> pressure exponent
	// jet
	real theta;     // the jet's phase at the edge re the period at the middle breath (0.5: the optimum, the
	                // jet's own response's delay included)
	real theta_reg; // its change per octave up (ratio, octaves)
	real w_track;   // the player follows the breath with the lip distance (1: theta held)
	real jet_h;     // lip opening (jet thickness, mm)
	real h_loud;    // the lips open with the breath: octaves at full breath
	real jet_w;     // jet width (mm)
	real g_soft;    // the player's embouchure: the jet's small-signal loop gain at the lightest touch
	                // (1: the oscillation threshold); the player sets the jet's growth e^{mu W} per note
	                // for it, louder breath raises the gain with the jet's velocity
	real amp_loud;  // the embouchure's growth at full breath (octaves)
	real y0;        // edge offset re h
	real y_tilt;    // timbre above 0 moves it (re h per unit)
	real jet_st;    // the jet's response falls above this Strouhal number (f h / U): its growth is
	                // band-limited (a critically damped 2-pole low-pass on the deflection)
	real vortex;    // vena contracta of the edge losses (alpha_vc; larger: less loss)
	// bore
	real bore_mm;  // bore radius
	real bore_t;   // resonance decay time (s) at C5
	real bore_reg; // octaves shorter per octave up
	real cut_hz;   // tone-hole lattice cutoff
	real wall;     // wall losses: a one-pole low-pass in the loop at this many harmonics of the bore
	real disp;     // end-correction allpass (0: none)
	real cover;    // timbre above 0 flattens (cents per unit)
	real cover_hz; // and darkens: cutoff octaves per unit
	// fingering
	real low_note; // the instrument's lowest note (first register from here)
	real overblow; // 1: second and third octaves overblown, 0: every note on the fundamental
	real range_lo; // the instrument's range: inside it the player as fitted; outside it the player
	real range_hi; // keeps the pressure, jet phase and bore losses of the range's edge, the notes
	               // above play on the fundamental of a short pipe, and the jet's growth is set for
	               // the loop's real losses (the delay lines' interpolation) with g_out of margin
	real g_out;    // the loop gain at the lightest touch above the range (below it: g_soft)
	real out_db;   // the level above the range re the fitted player's (dB), to meet the range's edge
	real ob_amt;   // overblowing: above ob_thr of the breath the player stops shortening the lip
	real ob_thr;   // distance with the jet's speed, the jet's travel time falls and the next mode
	               // takes over (0: the player always follows, as fitted)
	// radiation
	real mouth;   // radiation from the embouchure hole
	real end;     // from the open end / tone holes
	real dipole;  // the jet's own dipole at the edge (the source pressure)
	real tone_hz; // directivity / room low-pass
	real tone_q;
	// air
	real turb;    // jet turbulence (deflection noise re h, at U = 25 m/s)
	real turb_hz; // its bandwidth
	real hiss;    // the jet's edge noise radiated directly
	real hiss_st; // its Strouhal number (centre = st U / h)
	real t_noise; // tongue "t"
	// attack
	real tongue;   // strike sensitivity
	real acc_amp;  // the pressure built up behind the tongue: overshoot
	real acc_t;    // its decay (s)
	real tongue_t; // tongue release time of a soft "d" (s); a hard strike is ~16x faster
	real att_gain; // the tongue's release: the jet starts fast and thin, its growth up by this factor
	real att_t;    // for this long (s, decay)
	real att_soft; // the share of it for a start without a strike (a breath attack) or a slur
	real slur;     // articulation on a legato note change
	real pitch_t;  // the player's ear: pitch correction time constant (s)
	// modulation
	real vib, vib_hz; // shake -> breath vibrato
	// the tongue split (v5): tongue sets the strike that opens the attack's jet and the accent; these
	// set the same strike's share of the attack's noise ("t"), of the tongue's opening speed, and the
	// accent and noise a swell within a held note gives (the sustain's texture). At tongue's value
	// each is v4's tongue
	real tongue_nz, tongue_rt, tongue_sus;
};
static constexpr int NPARAMS = sizeof(Params) / sizeof(real);
static_assert(sizeof(Params) == NPARAMS * sizeof(real), "Params must hold only floats");

// The factory presets: presets/flute/NN-<name>.preset.json, compiled in (the end of this file);
// their values fitted to recordings:
//   0 flute       the concert flute, fitted to the University of Iowa MIS flute recordings
//   1 alto flute  in G: a fourth lower, a wider bore (13 mm); its player fitted to the alto's
//                 recordings
//   2 piccolo     the flute's player on the piccolo's geometry (an octave higher, a bore of 5.4 mm
//                 instead of 9.5), its jet, bore losses, radiation, noise and attack fitted to the
//                 VSCO-2 Community Edition piccolo (CC0: five sustained notes G5-G7 at one dynamic;
//                 the rise from the key on played phrases); its top notes, to C8, outside its range (they speak
//                 at the lightest touch so), its output level so that its fitted top meets them
static constexpr int NUM_PRESETS = 3;
[[nodiscard]] static constexpr const char* preset_name(int i);
[[nodiscard]] static inline Params preset_params(int i);

#ifdef FLUTE_PRESET
static constexpr int DEFAULT_PRESET = FLUTE_PRESET;
#else
static constexpr int DEFAULT_PRESET = 0;
#endif

struct VoiceState {
	real v;                   // window flow velocity (m/s, into the pipe)
	real xi;                  // the air's displacement at the lips (m)
	real q;                   // the jet's flow into the pipe (last sample)
	real mprev, eprev, dprev; // radiation differentiators
	real lps, lpw, aps;       // end reflection low-pass, wall-loss low-pass, allpass state
	real tn1, tn2;            // turbulence noise filter
	real jr1, jr2;            // the jet's response
	real hz1, hz2;            // edge noise band-pass
	real D;                   // loop delay (samples)
	real jd;                  // jet delay (samples, smoothed)
	uint32_t w, wj;
	// body
	real t1, t2;
	// player
	real lv1, lv2;  // breath smoother
	real amp;       // breath level after the release logic
	real gate;      // tongue / breath gate
	real gate_rate; // its opening rate (per control step)
	real acc, tn;   // accent and tongue noise envelopes
	real att;       // the attack's jet (the tongue's release): 1 at a hard strike, decays
	real accf;      // the accent's pressure
	real ty1, ty2;  // timbre smoother
	real pm_prev;   // blowing pressure at the end of the last block
	real note_prev; //
	real vneg;      // release speed latch
	real vib_ph;
	uint32_t rng, nrng;
	real level; // smoothed peak (activity)
	// the player's ear
	real xlp, xhp, xprev, tlast, pprev, trim, xamp;
	int active;
	// the key's strike (v5): the rise from silence until its pressure stops rising (or 0.25 s)
	int rise, rose;
	real rise_t;
	int was_held; // the key was held in the last block (a press on a voice still sounding its release)
};

struct Engine {
	awva::InstrumentControls g;
	awva::NoteControls vc[MAX_VOICES];
	int nvoices;
	int preset;
	Params p;         // as set (preset, live edits)
	Params ps;        // smoothed copy used by the DSP
	BulkMemory* bulk; // the host's (init): delay lines
	real sr, inv_sr, pi_sr, dt, ctrl_dt, block_dt;
	awva::SvfCoef lv_c, ty_c;
	real shake_pk, shake;
	real dc, dc_c;
	VoiceState vs[MAX_VOICES];
	real vout[BLOCK];
	real mix[BLOCK];
	real pmc[NCTRL]; // blowing pressure at the control points
	real tnc[NCTRL]; // tongue noise at the control points
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

[[nodiscard]] static inline real psin(real ph) { // sine of a phase in [0, 1)
	const real q = ph - 0.5f;
	return -8.0f * q * (1.0f - 2.0f * fabsf(q));
}

[[nodiscard]] static inline real sat(real x) {
	x = clampr(x, -1.5f, 1.5f);
	return x * (1.0f - 0.148148148f * x * x);
}

[[nodiscard]] static inline real block_noise(uint32_t& r) {
	r = r * 1664525u + 1013904223u;
	return (real)(int32_t)r * 4.656613e-10f;
}

// a voice's delay lines
[[nodiscard]] static inline float* bore_line(Engine& e, int v, int h) { return e.bulk->bore[v][h]; }
[[nodiscard]] static inline float* jet_line(Engine& e, int v) { return e.bulk->jet[v]; }

// the fingering: the register (mode) a note is played on, and the bore's fundamental
static inline void fingering(real note, real low, bool overblow, int& mode, real& bore_note) {
	mode = 1;
	bore_note = note;
	if (!overblow) return;
	const real r = note - low;
	if (r >= 33.0f) {
		mode = 4;
		bore_note = note - 24.0f;
	} else if (r >= 26.0f) {
		mode = 3;
		bore_note = note - 19.0196f;
	} else if (r >= 14.0f) {
		mode = 2;
		bore_note = note - 12.0f;
	}
}

// ---------------------------------------------------------------------------------------------
// setup

static inline void reset_voice(Engine& e, VoiceState& s, int v) {
	s.v = s.xi = s.q = s.mprev = s.eprev = s.dprev = 0.0f;
	s.lps = s.lpw = s.aps = s.tn1 = s.tn2 = s.hz1 = s.hz2 = s.jr1 = s.jr2 = 0.0f;
	s.D = 100.0f;
	s.jd = 10.0f;
	s.w = s.wj = 0;
	s.t1 = s.t2 = 0.0f;
	s.lv1 = s.lv2 = s.amp = s.gate = 0.0f;
	s.gate_rate = 0.0f;
	s.acc = s.tn = s.accf = s.att = 0.0f;
	s.ty1 = s.ty2 = 0.0f;
	s.pm_prev = 0.0f;
	s.vneg = 0.0f;
	s.level = 0.0f;
	s.xlp = s.xhp = s.xprev = 0.0f;
	s.tlast = -1e4f;
	s.pprev = 0.0f;
	s.was_held = 0;
	s.trim = s.xamp = 0.0f;
	if (v >= 0 && v < MAX_VOICES) {
		for (int h = 0; h < 2; h++)
			for (int i = 0; i < BLEN; i++) bore_line(e, v, h)[i] = 0.0f;
		for (int i = 0; i < JLEN; i++) jet_line(e, v)[i] = 0.0f;
	}
}

static inline void update_derived(Engine& e) {
	e.lv_c = awva::svf_from_g(awva::tan0(e.pi_sr * CTRL * rmin(e.p.lv_hz, 400.0f)), 0.5f);
}

// after the parameters of preset index were set (set_preset, or a preset loaded by name)
static inline void params_loaded(Engine& e, int index) {
	e.preset = index;
	e.ps = e.p;
	update_derived(e);
}

static inline void set_preset(Engine& e, int i) {
	if (i < 0 || i >= NUM_PRESETS) i = 0;
	e.p = preset_params(i);
#ifdef FLUTE_RANGE_ALL
	// (proof builds: every note inside the range, the v2 engine's behaviour everywhere)
	e.p.range_lo = 0.0f;
	e.p.range_hi = 132.0f;
#endif
	params_loaded(e, i);
}

static inline void init(Engine& e, int sample_rate, int nvoices) {
	const real sr = (real)sample_rate;
	e.sr = sr;
	e.inv_sr = 1.0f / sr;
	e.pi_sr = PI / sr;
	e.dt = 1.0f / sr;
	e.ctrl_dt = (real)CTRL / sr;
	e.block_dt = (real)BLOCK / sr;
	e.ty_c = awva::svf_from_g(awva::tan0(e.pi_sr * BLOCK * 12.0f), 0.5f);
	e.shake_pk = e.shake = 0.0f;
	e.dc = 0.0f;
	e.dc_c = 2.0f * PI * 12.0f / sr;
	e.nvoices = nvoices < MAX_VOICES ? nvoices : MAX_VOICES;
	e.g = awva::instrument_at_rest;
	for (int v = 0; v < MAX_VOICES; v++) {
		VoiceState& s = e.vs[v];
		s = VoiceState();
		reset_voice(e, s, v);
		s.rng = 0x9E3779B9u * (uint32_t)(v + 1) + 12345u;
		s.nrng = 0x85EBCA6Bu * (uint32_t)(v + 3) + 999u;
		s.vib_ph = 0.23f * (real)v;
		s.vib_ph -= (real)(int)s.vib_ph;
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

#ifdef FLUTE_TEST_HOOK
static int flute_guard_resets = 0;
#endif

// ---------------------------------------------------------------------------------------------
// one voice, one block

static inline void process_voice(Engine& e, int vi) {
	VoiceState& s = e.vs[vi];
	const Params& P = e.ps;
	const awva::NoteControls cc = e.vc[vi];
	const real pres = clampr(cc.pressure, 0.0f, 1.5f);
	// (a released note's rate is never positive: NoteControls; negative values set the release speed)
	const real pressure_rate = clampr(cc.pressure_rate, -1.0f, 1.0f);
	const real vp1 = rmax(pressure_rate - 0.02f, 0.0f);
	const real vplev = rmin(rmax(P.tongue, 0.0f) * vp1 / (0.4f + vp1), 1.0f);
	const real vpl_nz = rmin(rmax(P.tongue_nz, 0.0f) * vp1 / (0.4f + vp1), 1.0f);   // the attack's noise
	const real vpl_rt = rmin(rmax(P.tongue_rt, 0.0f) * vp1 / (0.4f + vp1), 1.0f);   // the tongue's speed
	const real vpl_sus = rmin(rmax(P.tongue_sus, 0.0f) * vp1 / (0.4f + vp1), 1.0f); // a swell's texture
	real note = clampr(cc.pitch, 0.0f, 132.0f);
	// outside the range, past the pipe's and the jet's limits the tone fades into the breath (the
	// pitch kept); a range wider than the pipe plays an octave up below it, as v2 did
	real lim = 1.0f;
	if (note < P.range_lo || note > P.range_hi) {
		lim = clampr((note - NOTE_LO) * (1.0f / LIMIT_ST), 0.0f, 1.0f) * clampr((NOTE_HI - note) * (1.0f / LIMIT_ST), 0.0f, 1.0f);
		note = clampr(note, NOTE_LO, NOTE_HI + LIMIT_ST);
	}
	while (note < NOTE_LO) note += 12.0f;
	const bool held = pres > 0.0f;
	bool start = false;
	if (!s.active) {
		if (!held && vplev <= 0.0f) {
			s.note_prev = note;
			return;
		}
		s.active = 1;
		start = true;
	}
	// a press: a start, or the key pressed again on a voice still sounding its release (the same
	// note played again): its articulation as a start's, over the tail (the state kept)
	const bool press = start || (held && !s.was_held);
	s.was_held = held;

	// ---- block rate: the player and the pipe for this note -------------------------------------
	const bool jump = start || fabsf(note - s.note_prev) >= 1.0f;
	if (jump && !start && held) s.acc = rmax(s.acc, P.slur); // legato change: a small articulation
	if (jump) s.vneg = 0.0f;
	s.note_prev = note;
	if (start) {
		s.v = s.xi = s.q = 0.0f;
		s.gate = 0.0f;
		s.lv1 = s.lv2 = 0.0f;
		s.amp = 0.0f;
		s.pm_prev = 0.0f;
	}
	// What strikes (v5): the key's own rise from silence, until its pressure stops rising; within a
	// held note only a real re-strike, a fast rise (pressure_rate at least RESTRIKE_V) at a pressure of at
	// least RESTRIKE_P. A swell, a pressure that bounces as the key lifts, a falling or low pressure
	// strike nothing (v4 re-tongued on any pressure_rate over ~0.07 once the accent had decayed).
	if (press) {
		s.rise = 1;
		s.rose = 0;
		s.rise_t = 0.0f;
	} else if (s.rise) {
		s.rise_t += e.block_dt;
		if (!held || s.rise_t > 0.25f || (s.rose && pressure_rate <= 0.05f)) s.rise = 0;
	}
	if (pressure_rate > 0.05f) s.rose = 1;
	const bool strikes = s.rise || (held && pressure_rate >= RESTRIKE_V && pres >= RESTRIKE_P);
	const real vstrike = strikes ? vplev : 0.0f;
	const real vstrike_rt = strikes ? vpl_rt : 0.0f;
	// the accent and the tongue's noise: during the key's rise the strike's, after it a swell's
	const real acc_in = s.rise ? vplev : vpl_sus;
	const real tn_in = s.rise ? vpl_nz : vpl_sus;
	// a strike opens the tongue: fast for a hard one
	if (vstrike > 0.05f && (press || vstrike > s.acc + 0.05f)) s.gate_rate = rmin(e.ctrl_dt / rmax(P.tongue_t * exp2s(-4.0f * vstrike_rt), 0.0005f), 1.0f);
	if (press && vplev <= 0.05f) s.gate_rate = rmin(e.ctrl_dt / rmax(4.0f * P.tongue_t, 0.0005f), 1.0f);
	// the attack's jet: a strike (from silence or a re-strike) releases the pressure built behind the
	// tongue, the jet starts fast and thin and grows more on its way to the edge; a breath attack or
	// a slur gets att_soft of it. It decays in att_t: the sustained jet is the player's as before
	{
		const real asoft = clampr(P.att_soft, 0.0f, 1.0f);
		const real sl = rmin(4.0f * vp1, 1.0f);
		const real at = asoft + (1.0f - asoft) * sl;
		if (press || (held && vstrike > s.acc + 0.05f)) s.att = rmax(s.att, at);
		else if (jump && held) s.att = rmax(s.att, asoft);
		s.att *= exp2s(-1.442695f * e.block_dt / clampr(P.att_t, 0.005f, 0.15f));
		if (s.att < 1e-4f) s.att = 0.0f;
	}

	const real by = clampr(cc.timbre, -1.0f, 1.0f);
	if (start) {
		s.ty1 = 0.0f;
		s.ty2 = by;
	}
	real tmb;
	{
		real ty1 = s.ty1, ty2 = s.ty2, t1;
		SVF_TICK(by, ty1, ty2, e.ty_c, t1, tmb);
		s.ty1 = ty1;
		s.ty2 = ty2;
	}
	tmb = clampr(tmb, -1.0f, 1.0f);

	int mode;
	real bnote;
	fingering(note, P.low_note, P.overblow >= 0.5f, mode, bnote);
	// outside the instrument's range: the edge's player; above it the fundamental of a short pipe
	const bool outr = note < P.range_lo || note > P.range_hi;
	if (note > P.range_hi) bnote = note;
	const real fs = rmin(note_freq(note), 0.3f * e.sr);                                            // sounding
	const real f1 = rmin(note_freq(bnote), 0.3f * e.sr);                                           // the bore's fundamental
	const real oct = ((outr ? clampr(note, P.range_lo, P.range_hi) : note) - 72.0f) * (1.0f / 12); // octaves re C5
	const real P1 = e.sr / f1;
	// bore: the open end's reflection (low-pass at the lattice cutoff, the round-trip loss) and the
	// end corrections' allpass
	const real fc = clampr(P.cut_hz * exp2s(P.cover_hz * tmb), 200.0f, rmin(12000.0f, 0.4f * e.sr)); // (below Nyquist at any rate)
	const real gc = tanp(e.pi_sr * fc);
	const real Gc = gc / (1.0f + gc);
	const real fw = clampr(P.wall * f1, 50.0f, 0.45f * e.sr);
	const real gw = tanp(rmin(e.pi_sr * fw, 1.4f));
	const real Gw = gw / (1.0f + gw);
	const real ap = clampr(P.disp, -0.9f, 0.9f);
	// phase delays at f1 (samples): the one-poles (bilinear: phase -atan(tan(w / 2) / g), magnitude
	// 1 / sqrt(1 + (tan(w / 2) / g)^2)) and the allpass
	const real w1 = 2.0f * PI * f1 * e.inv_sr;
	const real t0 = tanp(rmin(0.5f * w1, 1.3f));
	const real rc = t0 / gc, rw = t0 / gw;
	const real lp_d = (atans(rc) + atans(rw)) / w1;
	// the round-trip loss at f1 from the decay time, the low-passes' share taken out
	real g = exp2s(-1.442695f * P1 / (clampr(P.bore_t * exp2s(-P.bore_reg * oct), 0.002f, 0.5f) * e.sr));
	g = rmin(g * fsqrt((1.0f + rc * rc) * (1.0f + rw * rw)), 0.98f);
	// allpass (a + z^-1) / (1 + a z^-1): phase delay = 2 atan(tan(w/2) (1 - a) / (1 + a)) / w
	const real ap_d = 2.0f * atans(t0 * (1.0f - ap) / (1.0f + ap)) / w1;
	const real cover_c = P.cover * tmb;
	if (jump) {
		s.trim = 0.0f;
		s.tlast = -1e4f;
	}
	const real reach = rmin(REACH * rmax(e.sr * (1.0f / REACH_RATE), 1.0f), DMAX); // (REACH up to 48828 Hz)
	const real Dtot = clampr(P1 * exp2s((s.trim - cover_c) * (1.0f / 1200)) - lp_d - ap_d, 4.0f, 2.0f * reach);
	s.D = Dtot;
	const real d1t = clampr(0.5f * Dtot, 1.0f, reach);
	const real d2t = clampr(Dtot - d1t, 1.0f, reach);
	// pipe impedance at the window: Zc S_m (Pa per m/s of window flow)
	const real rb = clampr(P.bore_mm, 2.0f, 20.0f) * 0.001f;
	const real zw = RHO * C0 / (PI * rb * rb) * SM;
	const real kl = 0.5f * RHO / (clampr(P.vortex, 0.1f, 5.0f) * clampr(P.vortex, 0.1f, 5.0f));

	// radiation
	const awva::SvfCoef tcf = awva::svf_pade(e.pi_sr * clampr(P.tone_hz, 500.0f, rmin(16000.0f, 0.4f * e.sr)), clampr(P.tone_q, 0.5f, 4.0f));
	real gain = clampr(P.gain, 0.0f, 100.0f) * 1e-3f;
	if (note > P.range_hi) gain *= exp2s(clampr(P.out_db, -24.0f, 6.0f) * (1.0f / 6.0206f));

	// ---- control rate: breath, tongue, accent ----------------------------------------------------
	if (pressure_rate > 0.1f) s.vneg = 0.0f;
	if (held && pressure_rate < 0.0f) s.vneg = rmax(s.vneg, -pressure_rate);
	const real ped = clampr(e.g.sustain, 0.0f, 1.0f);
	const real rel_tc = rmax(P.rel + P.rel_ped * ped * ped, 0.004f) / (1.0f + 3.0f * s.vneg);
	const real fac = 1.0f - e.ctrl_dt / (held ? 0.03f : rel_tc);
	const real fac_acc = 1.0f - e.ctrl_dt / rmax(P.acc_t, 0.005f);
	const real fac_tn = 1.0f - e.ctrl_dt / 0.008f;
	const real fac_gate = rmin(e.ctrl_dt / rel_tc, 1.0f);
	const real pcl = pres * (P.pc_a + P.pc_b * pres);
	const real preg = exp2s(P.p_reg * oct);
	const real p_lo = clampr(P.p_soft, 10.0f, 5000.0f) * preg;
	const real p_hi = rmax(clampr(P.p_loud, 20.0f, 8000.0f) * preg, p_lo * 1.05f);
	const real pcv = clampr(P.p_curve, 0.3f, 3.0f);
	const real vib = e.shake * rmax(P.vib, 0.0f) * 0.2f;
	const real vib_inc = P.vib_hz * e.ctrl_dt;
	real amp_end;
	{
		real lv1 = s.lv1, lv2 = s.lv2, amp = s.amp, acc = s.acc, accf = s.accf, tn = s.tn, gate = s.gate, vph = s.vib_ph;
		const real fac_af = rmin(e.ctrl_dt / rmax(rmax(P.tongue_t, 0.0005f) * exp2s(-4.0f * acc), 0.001f), 1.0f);
		const awva::SvfCoef lc = e.lv_c;
		for (int k = 0; k < NCTRL; k++) {
			acc = rmax(acc * fac_acc, acc_in);
			tn = rmax(tn * fac_tn, tn_in);
			real t1, ls;
			SVF_TICK(rmin(pcl, 2.0f), lv1, lv2, lc, t1, ls);
			amp = rmax(rmax(ls, 0.0f), amp * fac);
			accf += fac_af * (P.acc_amp * acc - accf);
			if (held) gate += s.gate_rate * (1.0f - gate);
			else gate -= fac_gate * gate;
			vph += vib_inc;
			if (vph >= 1.0f) vph -= 1.0f;
			const real am = 1.0f + vib * psin(vph);
			const real a = rmin(amp + accf * amp + 0.5f * accf, 1.6f);
			const real apw = (pcv != 1.0f && a > 1e-6f) ? exp2s(pcv * log2s(a)) : a;
			e.pmc[k] = gate * am * (p_lo + (p_hi - p_lo) * apw) * (a > 0.0f ? 1.0f : 0.0f);
			e.tnc[k] = P.t_noise * tn * tn;
		}
		s.lv1 = lv1;
		s.lv2 = lv2;
		s.amp = amp;
		s.acc = acc;
		s.accf = accf;
		s.tn = tn;
		s.gate = gate;
		s.vib_ph = vph;
		amp_end = amp;
	}

	// ---- the jet for this block (at the block's mean blowing pressure) ---------------------------
	const real pm_blk = rmax(0.5f * (s.pm_prev + e.pmc[NCTRL - 1]), 0.0f);
	const real U = fsqrt(2.0f * pm_blk / RHO) + 0.05f;
	const real p_mid = fsqrt(p_lo * p_hi);
	const real Umid = fsqrt(2.0f * p_mid / RHO);
	const real breath = clampr((pm_blk - p_lo) / (p_hi - p_lo), 0.0f, 1.5f);
	const real h = clampr(P.jet_h, 0.2f, 3.0f) * 0.001f * exp2s(rmax(P.h_loud, -1.0f) * (breath - 0.5f));
	const real bj = 0.4f * h;
	const real Hj = clampr(P.jet_w, 2.0f, 20.0f) * 0.001f;
	// the player sets the lip-to-edge distance W for this note at the middle breath: the jet's phase
	// at the edge theta periods, its own response's delay included (a critically damped 2-pole:
	// phase -2 atan(f / fj)); a louder breath shortens the travel unless the player follows it
	const real hmid = clampr(P.jet_h, 0.2f, 3.0f) * 0.001f;
	const real gjm = tanp(rmin(e.pi_sr * clampr(P.jet_st * Umid / hmid, 100.0f, 0.4f * e.sr), 1.4f));
	const real ts = tanp(rmin(e.pi_sr * fs, 1.3f));
	const real ph_j = 2.0f * atans(ts / gjm) * (1.0f / (2.0f * PI));
	const real th0 = clampr(P.theta * exp2s(P.theta_reg * oct) - ph_j, 0.02f, 2.0f);
	const real Wj = clampr(th0 / fs * CP * Umid, 0.0005f, 0.03f); // the lip-to-edge distance
	const real uratio = Umid / U;
	real th = clampr(th0 * exp2s((1.0f - clampr(P.w_track, 0.0f, 1.0f)) * log2s(rmax(uratio, 1e-3f))), 0.02f, 3.0f);
	if (P.ob_amt > 0.0f) {
		// overblowing: above ob_thr the lip distance stays, the faster jet's travel shortens
		const real pth = p_lo + (p_hi - p_lo) * clampr(P.ob_thr, 0.0f, 1.5f);
		const real Uth = fsqrt(2.0f * pth / RHO) + 0.05f;
		if (U > Uth) th = clampr(th * exp2s(-2.0f * clampr(P.ob_amt, 0.0f, 1.0f) * log2s(U / Uth)), 0.02f, 3.0f);
	}
	const real tau = th / fs; // seconds
	const real jd_t = clampr(tau * e.sr, 1.0f, rmin(reach, JMAX));
	// the deflection at the edge follows the acoustic displacement at the lips (Fletcher), grown
	// on the way: eta = xi e^{mu W} / 2. The player sets the growth so that the small-signal loop
	// gain at the note, at the lightest touch, is g_soft: G = (rho delta_d / S_m) b H U sech^2(y0 / b)
	// (e^{mu W} / 2 b) |H_jet| |Y| (the displacement's 1 / w and the source's d/dt cancel), |Y| the
	// window admittance at the resonance (1 + ge) / ((1 - ge) zw), ge the loop's loss at the note
	const real iy0 = clampr(P.y0 + P.y_tilt * tmb, -6.0f, 6.0f) * 2.5f; // offset re b (b = 0.4 h)
	real growth;
	{
		const real Ulo = fsqrt(2.0f * p_lo / RHO) + 0.05f;
		const real hlo = clampr(P.jet_h, 0.2f, 3.0f) * 0.001f * exp2s(-0.5f * rmax(P.h_loud, -1.0f));
		const real gjl = tanp(rmin(e.pi_sr * clampr(P.jet_st * Ulo / hlo, 100.0f, 0.4f * e.sr), 1.4f));
		const real rj = ts / gjl;
		const real hjet = 1.0f / (1.0f + rj * rj);
		const real ey = exp2s(1.442695f * clampr(iy0, -6.0f, 6.0f));
		const real sech2 = 4.0f / ((ey + 1.0f / ey) * (ey + 1.0f / ey));
		const real rcs = ts / gc, rws = ts / gw;
		const real ge = rmin(g / fsqrt((1.0f + rcs * rcs) * (1.0f + rws * rws)), 0.98f);
		const real yres = (1.0f + ge) / ((1.0f - ge) * zw);
		const real unit = RHO * 1.2732395f * fsqrt(2.0f * hlo * Wj) / SM * (0.4f * hlo * Hj * Ulo) * sech2 * (0.5f / (0.4f * hlo)) * hjet * yres;
		growth = clampr(P.g_soft, 0.1f, 50.0f) / rmax(unit, 1e-9f) * exp2s(clampr(P.amp_loud, -3.0f, 3.0f) * rmin(breath, 1.0f));
		if (outr) {
			// the delay lines' linear interpolations lose |1 - f + f e^{-iw}| at the note; the margin g_out
			const real cw = (1.0f - ts * ts) / (1.0f + ts * ts);
			auto il = [cw](real d) {
				const real fr = d - (real)(int)d;
				return fsqrt(rmax(1.0f - 2.0f * fr * (1.0f - fr) * (1.0f - cw), 1e-4f));
			};
			const real loss = il(jd_t) * il(d1t) * il(d2t);
			const real gt = note > P.range_hi ? rmax(P.g_out, P.g_soft) : P.g_soft;
			growth *= gt / clampr(P.g_soft, 0.1f, 50.0f) / loss * lim;
		}
		growth *= 1.0f + clampr(P.att_gain, 0.0f, 40.0f) * s.att;
		growth = rmin(growth, 1e6f);
	}
	const real ke = 0.5f * growth / bj; // eta / b per m of displacement
	const real xleak = 1.0f - 2.0f * PI * 8.0f * e.inv_sr;
	const real deltad = 1.2732395f * fsqrt(2.0f * h * Wj);
	const real ksrc = RHO * deltad / SM * e.sr; // Pa per (m^3/s) per sample
	const real qmax = bj * Hj * U;
	const awva::SvfCoef jrc = awva::svf_pade(e.pi_sr * clampr(P.jet_st * U / h, 100.0f, 0.4f * e.sr), 0.5f);
	// turbulence: deflection noise re b, growing with U (re 25 m/s)
	const real ur = U * (1.0f / 25.0f);
	const real tz = rmax(P.turb, 0.0f) * ur * h / bj;
	const awva::SvfCoef tzc = awva::svf_pade(e.pi_sr * clampr(P.turb_hz, 100.0f, rmin(12000.0f, 0.4f * e.sr)), 0.7f);
	// edge noise radiated directly: a band at st U / h, level ~ U^3
	const real fh = clampr(P.hiss_st * U / h, 300.0f, 0.4f * e.sr);
	const awva::SvfCoef hzc = awva::svf_pade(e.pi_sr * fh, 0.8f);
	const real hz_amp = rmax(P.hiss, 0.0f) * ur * ur * ur * 1000.0f;

	// ---- audio rate: jet, window, bore ---------------------------------------------------------
	float* __restrict__ B1 = bore_line(e, vi, 0);
	float* __restrict__ B2 = bore_line(e, vi, 1);
	float* __restrict__ J = jet_line(e, vi);
	real v = s.v, xi = s.xi, q = s.q, mprev = s.mprev, eprev = s.eprev, dprev = s.dprev, lps = s.lps, lpw = s.lpw, aps = s.aps;
	real tn1 = s.tn1, tn2 = s.tn2, hz1 = s.hz1, hz2 = s.hz2, jd = s.jd, jr1 = s.jr1, jr2 = s.jr2;
	uint32_t w = s.w, wj = s.wj, nr = s.nrng;
	if (start) jd = jd_t;
	const real djd = (jd_t - jd) * (1.0f / BLOCK);
	real* __restrict__ out = e.vout;
	const uint32_t i2 = (uint32_t)(int)d2t, i1 = (uint32_t)(int)d1t;
	const real f2 = d2t - (real)(int)i2, fn1 = d1t - (real)(int)i1;
	const real mouth = P.mouth, endm = P.end, dip = P.dipole;
	real pm = s.pm_prev;
	real peak = 0.0f;
	const real a_lp = clampr(2.0f * PI * 1.3f * fs * e.dt, 0.001f, 0.9f);
	real xlp = s.xlp, xhp = s.xhp, xprev = s.xprev, tlast = s.tlast - (real)BLOCK, pprev = s.pprev, xamp = 0.0f;
	int ncross = 0;
	real per_sum = 0.0f;
	for (int k = 0; k < NCTRL; k++) {
		const real tnl = e.tnc[k];
		for (int j = 0; j < CTRL; j++) {
			jd += djd;
			// the wave returning from the open end, at the mouth
			const real r0 = B2[(w - i2) & BMASK], r1 = B2[(w - i2 - 1u) & BMASK];
			const real pin = r0 + f2 * (r1 - r0);
			// the window flow a jet's travel ago
			const int ij = (int)jd;
			const real fj = jd - (real)ij;
			const real j0 = J[(wj - (uint32_t)ij) & JMASK], j1 = J[(wj - (uint32_t)ij - 1u) & JMASK];
			const real vd = j0 + fj * (j1 - j0);
			// turbulence on the deflection
			nr = nr * 1664525u + 1013904223u;
			const real nz = (real)(int32_t)nr * 4.656613e-10f;
			real tb, tlo;
			SVF_TICK(nz, tn1, tn2, tzc, tb, tlo);
			(void)tb;
			// the deflection at the edge (re b) and the jet's split
			real jb, jl;
			SVF_TICK(ke * vd + (tz + tnl) * tlo, jr1, jr2, jrc, jb, jl);
			(void)jb;
			real x = jl - iy0;
			x = clampr(x, -3.0f, 3.0f); // (tanh at +-3: +-0.995)
			const real x2 = x * x;
			const real th_ = x * (27.0f + x2) / (27.0f + 9.0f * x2);
			const real qn = qmax * (1.0f + th_);
			const real dps = -ksrc * (qn - q);
			q = qn;
			// the window: zw v + kl v |v| = dps - 2 pin
			const real A = dps - 2.0f * pin;
			const real vn = 2.0f * A / (zw + fsqrt(zw * zw + 4.0f * kl * fabsf(A)));
			v = vn;
			const real pmouth = dps - kl * v * fabsf(v);
			B1[w] = pmouth - pin;
			xi = xi * xleak + v * e.dt;
			J[wj] = xi;
			wj = (wj + 1u) & JMASK;
			// the open end: reflection (inverted, low-passed, allpass) and the radiated rest
			const real a0 = B1[(w - i1) & BMASK], a1 = B1[(w - i1 - 1u) & BMASK];
			const real be = a0 + fn1 * (a1 - a0);
			const real vb = (be - lps) * Gc;
			const real lp = vb + lps;
			lps = lp + vb;
			const real vw = (lp - lpw) * Gw;
			const real lw = vw + lpw;
			lpw = lw + vw;
			const real apo = ap * lw + aps; // allpass, direct form: y = a x + s, s' = x - a y
			aps = lw - ap * apo;
			const real refl = -g * apo;
			B2[w] = refl;
			w = (w + 1u) & BMASK;
			// radiation: the flow out of the window (-v zw) and out of the open end (be - refl)
			const real mo = -zw * v, eo = be - refl;
			real hb, hlo;
			SVF_TICK(nz, hz1, hz2, hzc, hb, hlo);
			(void)hlo;
			const real o = mouth * (mo - mprev) + endm * (eo - eprev) + dip * (dps - dprev) + hz_amp * (1.0f + 3.0f * tnl) * hb * (pm > 0.0f ? 1.0f : 0.0f);
			mprev = mo;
			eprev = eo;
			dprev = dps;
			out[k * CTRL + j] = o;
			// the player's ear: window flow low-passed, DC removed, upward zero crossings
			xlp += a_lp * (v - xlp);
			xhp += 0.004f * (xlp - xhp);
			{
				const real xc = xlp - xhp;
				xamp = rmax(xamp, fabsf(xc));
				if (xprev < 0.0f && xc >= 0.0f) {
					const real tc = (real)(k * CTRL + j) - xc / (xc - xprev);
					if (tlast > -4000.0f) {
						const real per = tc - tlast;
						if (fabsf(per - pprev) < 0.05f * per) {
							per_sum += per;
							ncross++;
						}
						pprev = per;
					}
					tlast = tc;
				}
				xprev = xc;
			}
		}
		pm = e.pmc[k];
	}
	s.v = v;
	s.xi = xi;
	s.q = q;
	s.mprev = mprev;
	s.eprev = eprev;
	s.dprev = dprev;
	s.lps = lps;
	s.lpw = lpw;
	s.aps = aps;
	s.tn1 = tn1;
	s.jr1 = jr1;
	s.jr2 = jr2;
	s.tn2 = tn2;
	s.hz1 = hz1;
	s.hz2 = hz2;
	s.jd = jd;
	s.w = w;
	s.wj = wj;
	s.nrng = nr;
	s.pm_prev = pm;
	s.xlp = xlp;
	s.xhp = xhp;
	s.xprev = xprev;
	s.tlast = tlast;
	s.pprev = pprev;
	s.xamp = rmax(xamp, s.xamp + 0.1f * (xamp - s.xamp));
	// the player's ear: a slow trim of the bore onto the note
	if (ncross > 0 && held && s.xamp > 0.05f) {
		const real err = 1200.0f * log2s(e.sr * (real)ncross / (rmax(per_sum, 1.0f) * fs)) - cover_c; // (the cover is a bend the ear keeps)
		if (fabsf(err) < 200.0f) s.trim = clampr(s.trim + clampr(err * rmin(per_sum * e.dt / clampr(P.pitch_t, 0.002f, 1.0f), 0.5f), -6.0f, 6.0f), -150.0f, 150.0f);
	}

	// directivity / room low-pass, gain
	{
		real t1 = s.t1, t2 = s.t2;
		for (int n = 0; n < BLOCK; n++) {
			real tb, ty;
			SVF_TICK(out[n], t1, t2, tcf, tb, ty);
			const real y = ty * gain;
			out[n] = y;
			peak = rmax(peak, fabsf(y));
		}
		s.t1 = t1;
		s.t2 = t2;
	}

	// guard: non-finite or absurd state -> reset this voice and drop its block
	const real st = fabsf(s.v) + fabsf(s.lps) + fabsf(s.lpw) + fabsf(s.aps) + fabsf(s.xi) * 1e3f + fabsf(s.t1) + fabsf(s.t2) + fabsf(s.mprev) + fabsf(s.eprev) + fabsf(s.dprev) + fabsf(s.amp) + fabsf(s.hz1) + fabsf(s.hz2) + fabsf(s.jr1) + fabsf(s.jr2);
	if (!(peak < 1e3f) || !(st < 1e7f)) {
#ifdef FLUTE_TEST_HOOK
		flute_guard_resets++;
		fprintf(stderr, "guard: voice %d peak %g st %g note %g pres %g\n", vi, (double)peak, (double)st, (double)note, (double)pres);
#endif
		reset_voice(e, s, vi);
		s.active = 0;
		return;
	}
	real* __restrict__ mix = e.mix;
	for (int n = 0; n < BLOCK; n++) mix[n] += out[n];
	s.level += 0.05f * (peak - s.level);
	// idle once released and faded out; the state is kept
	if (!held && vplev <= 0.0f && s.gate < 1e-4f && amp_end < 1e-5f && s.acc < 1e-4f && peak < 1e-5f) s.active = 0;
}

static inline void process(Engine& e, real* __restrict__ output) {
	// smooth the parameters towards the set values (live edits glide in ~13 ms instead of stepping)
	{
		const real* __restrict__ pt = reinterpret_cast<const real*>(&e.p);
		real* __restrict__ ps = reinterpret_cast<real*>(&e.ps);
		for (int i = 0; i < NPARAMS; i++) {
			const real d = pt[i] - ps[i];
			ps[i] = fabsf(d) <= 1e-5f * rmax(fabsf(pt[i]), 1.0f) ? pt[i] : ps[i] + 0.05f * d;
		}
	}
	{
		const real sh = clampr(e.g.motion.accel_g - 1.1f, 0.0f, 1.0f);
		e.shake_pk = rmax(sh, e.shake_pk * (1.0f - e.block_dt * 2.5f));
		e.shake += 0.03f * (e.shake_pk - e.shake);
	}
	for (int n = 0; n < BLOCK; n++) e.mix[n] = 0.0f;
	for (int v = 0; v < e.nvoices; v++) process_voice(e, v);
	real dc = e.dc;
	const real dcc = e.dc_c;
	for (int n = 0; n < BLOCK; n++) {
		const real x = e.mix[n];
		dc += dcc * (x - dc);
		output[n] = 4.2f * sat((x - dc) * (1.0f / 4.2f));
	}
	e.dc = (fabsf(dc) < 1e3f) ? dc : 0.0f;
}

// ---------------------------------------------------------------------------------------------
// live parameters

struct ParamEntry {
	awva::ParamDesc d;
	real Params::*m;
};

static constexpr awva::ParamKind C_ = awva::ParamKind::continuous;
static constexpr awva::ParamKind T_ = awva::ParamKind::toggle;

static constexpr ParamEntry param_table[] = {
    {{"gain", "level", 0.0f, 100.0f, C_, AWVA_TEXT(flute, gain)}, &Params::gain},
    {{"pres_lin", "level", 0.0f, 1.0f, C_, AWVA_TEXT(flute, pres_lin)}, &Params::pc_a},
    {{"pres_sq", "level", 0.0f, 1.5f, C_, AWVA_TEXT(flute, pres_sq)}, &Params::pc_b},
    {{"pres_hz", "level", 2.0f, 200.0f, C_, AWVA_TEXT(flute, pres_hz)}, &Params::lv_hz},
    {{"release", "level", 0.005f, 1.0f, C_, AWVA_TEXT(flute, release)}, &Params::rel},
    {{"rel_pedal", "level", 0.0f, 3.0f, C_, AWVA_TEXT(flute, rel_pedal)}, &Params::rel_ped},
    {{"p_soft", "breath", 10.0f, 3000.0f, C_, AWVA_TEXT(flute, p_soft)}, &Params::p_soft}, // Pa at C5
    {{"p_loud", "breath", 20.0f, 6000.0f, C_, AWVA_TEXT(flute, p_loud)}, &Params::p_loud},
    {{"p_reg", "breath", -0.5f, 2.0f, C_, AWVA_TEXT(flute, p_reg)}, &Params::p_reg},
    {{"p_curve", "breath", 0.3f, 3.0f, C_, AWVA_TEXT(flute, p_curve)}, &Params::p_curve},
    {{"theta", "jet", 0.03f, 2.0f, C_, AWVA_TEXT(flute, theta)}, &Params::theta},
    {{"theta_reg", "jet", -1.0f, 1.0f, C_, AWVA_TEXT(flute, theta_reg)}, &Params::theta_reg},
    {{"w_track", "jet", 0.0f, 1.0f, C_, AWVA_TEXT(flute, w_track)}, &Params::w_track},
    {{"jet_h", "jet", 0.2f, 3.0f, C_, AWVA_TEXT(flute, jet_h)}, &Params::jet_h}, // mm
    {{"h_loud", "jet", -1.0f, 2.0f, C_, AWVA_TEXT(flute, h_loud)}, &Params::h_loud},
    {{"jet_w", "jet", 2.0f, 20.0f, C_, AWVA_TEXT(flute, jet_w)}, &Params::jet_w}, // mm
    {{"g_soft", "jet", 0.3f, 20.0f, C_, AWVA_TEXT(flute, g_soft)}, &Params::g_soft},
    {{"amp_loud", "jet", -3.0f, 3.0f, C_, AWVA_TEXT(flute, amp_loud)}, &Params::amp_loud},
    {{"y0", "jet", -5.0f, 5.0f, C_, AWVA_TEXT(flute, y0)}, &Params::y0},
    {{"y_tilt", "jet", -3.0f, 3.0f, C_, AWVA_TEXT(flute, y_tilt)}, &Params::y_tilt},
    {{"jet_st", "jet", 0.01f, 1.0f, C_, AWVA_TEXT(flute, jet_st)}, &Params::jet_st},
    {{"vortex", "jet", 0.1f, 5.0f, C_, AWVA_TEXT(flute, vortex)}, &Params::vortex},
    {{"bore_mm", "bore", 2.0f, 20.0f, C_, AWVA_TEXT(flute, bore_mm)}, &Params::bore_mm},
    {{"bore_decay", "bore", 0.002f, 0.5f, C_, AWVA_TEXT(flute, bore_decay)}, &Params::bore_t},
    {{"bore_reg", "bore", -1.0f, 2.0f, C_, AWVA_TEXT(flute, bore_reg)}, &Params::bore_reg},
    {{"cut_hz", "bore", 200.0f, 12000.0f, C_, AWVA_TEXT(flute, cut_hz)}, &Params::cut_hz},
    {{"wall", "bore", 0.5f, 50.0f, C_, AWVA_TEXT(flute, wall)}, &Params::wall},
    {{"disp", "bore", -0.9f, 0.9f, C_, AWVA_TEXT(flute, disp)}, &Params::disp},
    {{"cover", "bore", -200.0f, 200.0f, C_, AWVA_TEXT(flute, cover)}, &Params::cover},
    {{"cover_hz", "bore", -2.0f, 2.0f, C_, AWVA_TEXT(flute, cover_hz)}, &Params::cover_hz},
    {{"low_note", "fingering", 24.0f, 96.0f, C_, AWVA_TEXT(flute, low_note)}, &Params::low_note},
    {{"overblow", "fingering", 0.0f, 1.0f, T_, AWVA_TEXT(flute, overblow)}, &Params::overblow},
    {{"range_lo", "fingering", 24.0f, 96.0f, C_, AWVA_TEXT(flute, range_lo)}, &Params::range_lo},
    {{"range_hi", "fingering", 48.0f, 120.0f, C_, AWVA_TEXT(flute, range_hi)}, &Params::range_hi},
    {{"g_out", "fingering", 1.0f, 6.0f, C_, AWVA_TEXT(flute, g_out)}, &Params::g_out},
    {{"out_db", "fingering", -24.0f, 6.0f, C_, AWVA_TEXT(flute, out_db)}, &Params::out_db},
    {{"ob_amt", "jet", 0.0f, 1.0f, C_, AWVA_TEXT(flute, ob_amt)}, &Params::ob_amt},
    {{"ob_thr", "jet", 0.0f, 1.5f, C_, AWVA_TEXT(flute, ob_thr)}, &Params::ob_thr},
    {{"mouth", "radiation", 0.0f, 4.0f, C_, AWVA_TEXT(flute, mouth)}, &Params::mouth},
    {{"end", "radiation", 0.0f, 4.0f, C_, AWVA_TEXT(flute, end)}, &Params::end},
    {{"dipole", "radiation", 0.0f, 4.0f, C_, AWVA_TEXT(flute, dipole)}, &Params::dipole},
    {{"tone_hz", "radiation", 500.0f, 16000.0f, C_, AWVA_TEXT(flute, tone_hz)}, &Params::tone_hz},
    {{"tone_q", "radiation", 0.5f, 2.0f, C_, AWVA_TEXT(flute, tone_q)}, &Params::tone_q},
    {{"turb", "air", 0.0f, 2.0f, C_, AWVA_TEXT(flute, turb)}, &Params::turb},
    {{"turb_hz", "air", 100.0f, 12000.0f, C_, AWVA_TEXT(flute, turb_hz)}, &Params::turb_hz},
    {{"hiss", "air", 0.0f, 1.0f, C_, AWVA_TEXT(flute, hiss)}, &Params::hiss},
    {{"hiss_st", "air", 0.02f, 1.0f, C_, AWVA_TEXT(flute, hiss_st)}, &Params::hiss_st},
    {{"t_noise", "air", 0.0f, 3.0f, C_, AWVA_TEXT(flute, t_noise)}, &Params::t_noise},
    {{"tongue", "attack", 0.0f, 2.0f, C_, AWVA_TEXT(flute, tongue)}, &Params::tongue},
    {{"acc_level", "attack", 0.0f, 1.5f, C_, AWVA_TEXT(flute, acc_level)}, &Params::acc_amp},
    {{"acc_decay", "attack", 0.005f, 0.3f, C_, AWVA_TEXT(flute, acc_decay)}, &Params::acc_t},
    {{"tongue_t", "attack", 0.0005f, 0.1f, C_, AWVA_TEXT(flute, tongue_t)}, &Params::tongue_t},
    {{"att_gain", "attack", 0.0f, 40.0f, C_, AWVA_TEXT(flute, att_gain)}, &Params::att_gain},
    {{"att_t", "attack", 0.005f, 0.15f, C_, AWVA_TEXT(flute, att_t)}, &Params::att_t},
    {{"att_soft", "attack", 0.0f, 1.0f, C_, AWVA_TEXT(flute, att_soft)}, &Params::att_soft},
    {{"slur", "attack", 0.0f, 1.0f, C_, AWVA_TEXT(flute, slur)}, &Params::slur},
    {{"pitch_t", "attack", 0.002f, 1.0f, C_, AWVA_TEXT(flute, pitch_t)}, &Params::pitch_t},
    {{"vib", "mod", 0.0f, 2.0f, C_, AWVA_TEXT(flute, vib)}, &Params::vib},
    {{"vib_hz", "mod", 2.0f, 9.0f, C_, AWVA_TEXT(flute, vib_hz)}, &Params::vib_hz},
    {{"tongue_nz", "attack", 0.0f, 2.0f, C_, AWVA_TEXT(flute, tongue_nz)}, &Params::tongue_nz},    // the strike's attack noise
    {{"tongue_rt", "attack", 0.0f, 2.0f, C_, AWVA_TEXT(flute, tongue_rt)}, &Params::tongue_rt},    // the strike's tongue speed
    {{"tongue_sus", "attack", 0.0f, 2.0f, C_, AWVA_TEXT(flute, tongue_sus)}, &Params::tongue_sus}, // a swell's accent and noise
};
static constexpr int NUM_LIVE_PARAMS = sizeof(param_table) / sizeof(param_table[0]);
static_assert(NUM_LIVE_PARAMS == NPARAMS, "every Params field is exposed once");

} // namespace flute

namespace awva {
template <> struct EngineParams<flute::Engine> {
	static constexpr int count = flute::NUM_LIVE_PARAMS;
	[[nodiscard]] static constexpr const ParamDesc* desc(int i) { return (i >= 0 && i < count) ? &flute::param_table[i].d : nullptr; }
	[[nodiscard]] static float* ptr(flute::Engine& e, int i) { return (i >= 0 && i < count) ? &(e.p.*(flute::param_table[i].m)) : nullptr; }
	static void changed(flute::Engine& e, int) { flute::update_derived(e); }
};
} // namespace awva

// ---------------------------------------------------------------------------------------------
// The factory presets: presets/flute/NN-<name>.preset.json, the files themselves (presets/flute.inc,
// tools/presets_inc.cmake), each read at compile time; a file that isn't every parameter in range
// doesn't compile (awva/preset.h factory).

namespace flute {
#include "presets/flute.inc"
static_assert(NUM_FACTORY_PRESETS == NUM_PRESETS, "presets/flute/: a file per preset");

[[nodiscard]] static constexpr const awva::preset::Factory<NPARAMS>& factory_preset(int i) {
	return awva::preset::factories<awva::EngineParams<Engine>, factory_json, NUM_PRESETS>[(i >= 0 && i < NUM_PRESETS) ? i : 0];
}

[[nodiscard]] static constexpr const char* preset_name(int i) { return factory_preset(i).name.c_str(); }

[[nodiscard]] static inline Params preset_params(int i) {
	const awva::preset::Factory<NPARAMS>& f = factory_preset(i);
	Params p;
	for (int k = 0; k < NPARAMS; k++) p.*(param_table[k].m) = f.values[k];
	return p;
}
} // namespace flute

#endif // VOICE_FLUTE_H_
