// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
/**
 * doublereed: a physical model of the double reeds (oboe, cor anglais, bassoon), one model with a
 * preset per instrument. Traits: engines/doublereed.h.
 *
 * Every voice is a player blowing a reed on a conical bore; nothing sets the pitch but the reed and
 * the bore together:
 *
 *   breath   key pressure -> breath level (pres (a + b pres) + the strike's accent, smoothed) ->
 *            mouth pressure: from p_soft x the oscillation threshold of this note (the lightest
 *            touch speaks) to p_loud x the reed's closing pressure (ff). The threshold is the
 *            quasi-static reed's: the mouth pressure at which its negative conductance meets the
 *            bore's conductance at the note's resonance
 *   tongue   the strike is the tongue on the reed's tip: it holds the reed closed while the strike
 *            rises (a harder strike: more closed) and lets it go at the peak, with the pressure built
 *            up behind it (an accent that decays) and a turbulent "t". A press without a strike is a
 *            breath attack. Three sensitivities to the strike, independent: tongue (the accent),
 *            att_rate (how fast the tongue lets go, and how far it held the reed shut: the attack's
 *            rate) and att_noise (the "t"); the sustain's texture (breath, jitter, the player's
 *            wander) doesn't follow the strike. A strike counts while the note starts (STRIKE_WIN
 *            after a start or a new note): inside a held note a swell of the pressure is breath
 *   slur     a step of the held note (a slur in mono) changes the fingering over GLIDE_S
 *            (15 ms, note_glide.h): at once, the bore would change in one block and the reed slap shut
 *   REED     two blades as one mechanical mode, blown closed (the mouth pressure pushes them
 *            shut; Fletcher 1979, Vergez et al. 2003): resonance and Q (the lips' damping), the tip
 *            opening at rest and the closing pressure (the blades' stiffness), contact stiffness and
 *            damping when the blades beat. Flow: Bernoulli jet u = w h sqrt(2 dp / rho), and the
 *            duct behind the tip (the staple, the bocal) limits it: the jet's head loss in the duct
 *            (Vergez et al. 2003, Almeida et al. 2007) saturates the flow at large openings,
 *            u = w h sqrt(2 dp / rho) / sqrt(1 + (h / h_duct)^2). Solved exactly per sample together
 *            with the bore's input (a quadratic in sqrt(dp)), the blades' position explicit
 *   BORE     a truncated cone (Ayers et al. 1985; Scavone 1997): spherical waves on one delay line
 *            (the round trip), the input at distance x0 from the apex. There the spherical wave's
 *            near field is an inertance rho x0 / S0 in parallel with the wave impedance (trapezoidal),
 *            and the reed's and the staple's air volume a compliance V / (rho c^2) (the missing
 *            cone's stand-in: Benade). The loop delay is solved per block so that the input
 *            impedance peaks at the note; the player's ear trims it slowly from the reed's motion
 *   TONE HOLES  the open-hole lattice reflects below its cutoff and passes above it (Benade 1960):
 *            the reflection is a 2-pole lowpass at the cutoff (x the round trip's wall losses, from
 *            a decay time); above the cutoff the bore has no resonances (no squeaks) and the wave
 *            leaves through the lattice
 *   RADIATION  what the lattice transmits (1 - its reflection), radiation high-pass, directivity
 *            low-pass
 *
 * Every element is passive or bounded: the jet only draws on the mouth pressure, the loop gain is
 * < 1, the input is solved implicitly; the blades' excursion and the pressures are clamped; a
 * per-voice guard resets a voice whose state is non-finite or absurd; the master has a DC blocker
 * and a soft ceiling at 4.2.
 *
 * Playing:
 *   pressure   breath: dynamics and brightness together (pp sweet and covered -> ff reedy, the
 *              blades beating)
 *   strike     tonguing: hard = a fast release from a closed reed, an accent and a "t"; soft = "d";
 *              none = a breath attack
 *   release    the breath stops, the bore rings out (pedal lengthens the release)
 *   timbre     embouchure: above 0 it bites (a smaller tip opening, more lip damping: thinner,
 *              brighter, a little sharp), below 0 it relaxes (darker, fuller, a little flat)
 *   pitch      bends (the host moves the note; the bore follows)
 *   shake      flutter tongue
 *
 * The player (v2) is alive: the breath's and the lips' slow wander (wander, lip_wander, wander_hz:
 * band-limited noise on the mouth pressure and on the reed's tip opening) gives the sustained tone
 * the small pitch and level fluctuations of a player; v1's tone was 10-30 times steadier than the
 * recordings'. No steady breath noise is added (hiss 0): what is left is the turbulence riding on the
 * flow's pulses and the tongue's "t".
 *
 * The oboe's and the bassoon's presets were fitted (CMA-ES) to the University of Iowa Musical
 * Instrument Samples (pp/mf/ff chromatic scales; free for any project, without restrictions), the
 * Track B way: the sustain's harmonics, noise and level across notes and dynamics, the timbre's
 * response to the dynamic, the attack's spectra, the tone's rise from the key and its pitch excursion,
 * the tone's pitch and level wobble re the recordings', pitch and register; plus the tone's
 * rise from the key on played phrases, every in-range note speaking at four pressures, and a dynamic
 * that keeps growing to the firmest press. The cor anglais is the oboe's player on its
 * geometry (not fitted: no permissively licensed recordings). The output gains put each preset's mf
 * level at v1's.
 *
 * Cost (QEMU Cortex-M7 instruction counts per block, 10 voices, the default performance):
 * oboe 28.4k, cor anglais 28.2k, bassoon 27.8k. Per voice-sample:
 * two square roots, two divisions, ~110 instructions; per block the loop delay's Newton step on the
 * small-signal loop gain.
 * Memory: the bore delay lines, 4 KB per voice (40 KB BulkMemory). Engine ~3 KB.
 */
#ifndef VOICE_DOUBLEREED_H_
#define VOICE_DOUBLEREED_H_

#include <math.h>
#include <stdint.h>

#include "controls.h"
#include "dspmath.h"
#include "params.h"
#include "param_text/doublereed.inc" // the parameters' text (AWVA_TEXT)
#include "note_glide.h"
#include "preset.h"
#include "svf.h"

namespace doublereed {

using real = float;
using awva::rmax;
using awva::rmin;

static constexpr int BLOCK = awva::BLOCK;
// the voices (arrays sized by it): 10 unless the build says another (AWVA_VOICES_DOUBLEREED)
#ifndef AWVA_VOICES_DOUBLEREED
#define AWVA_VOICES_DOUBLEREED 10
#endif
static constexpr int MAX_VOICES = AWVA_VOICES_DOUBLEREED;
static constexpr int CTRL = 8; // control rate divider (6 kHz)
static constexpr int NCTRL = BLOCK / CTRL;
static_assert(BLOCK % CTRL == 0, "CTRL must divide BLOCK");
static constexpr int BLEN = 1024; // the round trip (period <= ~1000 samples: MIDI >= 31 at 48 kHz)
static constexpr uint32_t BMASK = BLEN - 1;
static constexpr real DMAX = (real)(BLEN - 4);
static constexpr real RHO = 1.2f;
static constexpr real C0 = 343.0f;
static constexpr real PI = 3.14159265f;
static constexpr real NOTE_LO = 31.5f;    // below: an octave up (the delay line's length)
static constexpr real NOTE_HI = 100.0f;   // above: an octave down
static constexpr real STRIKE_WIN = 0.05f; // s: a strike counts while the note starts
static constexpr real GLIDE_S = 0.015f;   // a slur's fingering change (note_glide.h)

// the bore delay lines, placed by the host (engine SDK): BulkMemory (the bulk region: on an embedded target, its slower RAM)
struct BulkMemory {
	alignas(32) float bore[MAX_VOICES][BLEN];
};

// ---------------------------------------------------------------------------------------------
// Parameters (all floats: the live parameters)

struct Params {
	// level / breath
	real gain;         // output level (x 1e-4 per Pa)
	real pc_a, pc_b;   // breath from key pressure: pres * (pc_a + pc_b * pres)
	real lv_hz;        // breath smoothing (critically damped 2-pole)
	real rel, rel_ped; // release time constant (s): rel + rel_ped * pedal^2
	real p_soft;       // mouth pressure at the lightest touch, re the oscillation threshold
	real p_loud;       // mouth pressure at full breath, re the reed's closing pressure (at the reference note)
	real p_reg;        // octaves of that ratio per octave of pitch
	real p_curve;      // breath -> mouth pressure exponent
	// reed
	real reed_hz;    // the blades' resonance (Hz, at the reference note)
	real reed_reg;   // octaves per octave of pitch (the embouchure follows the register)
	real reed_q;     // its Q (the lips' damping)
	real reed_open;  // tip opening at rest (mm, at the reference note)
	real open_reg;   // octaves smaller per octave up
	real reed_w;     // tip width (mm)
	real p_close;    // closing pressure (kPa): the blades' stiffness x the opening
	real contact;    // contact stiffness when the blades beat, re their stiffness
	real duct;       // the duct's limit on the jet: the opening (mm) where the head loss halves the flow's growth
	real emb_tilt;   // timbre above 0 bites: octaves of opening per unit (and damping)
	real emb_soft;   // the embouchure follows the breath: the lips pinch the tip to this part of its opening at
	                 // the lightest breath and relax to the full opening at full breath (soft playing)
	real bite;       // and the pitch it pulls (cents per unit of timbre)
	real jitter;     // embouchure jitter (cents of the reed's resonance)
	real wander;     // the player is no machine: the breath's slow wander (the mouth pressure's relative
	real lip_wander; // std) and the lips' (the reed's tip opening's), band-limited at wander_hz: the
	real wander_hz;  // sustained tone's small, living pitch and level fluctuations
	// bore
	real ref_note; // the reference note (MIDI) the parameters' keytracking is about ("at the reference note")
	real apex;     // distance of the input from the cone's apex (cm)
	real cavity;   // the reed's and the staple's air volume, re the missing cone's (S0 x0 / 3: Benade)
	real bore_d;   // bore diameter at the input (mm)
	real bore_t;   // resonance decay time (s) at the reference note
	real bore_reg; // octaves shorter per octave up
	// tone holes
	real lat_hz;  // open-hole lattice cutoff (Hz)
	real lat_q;   // its Q
	real lat_reg; // octaves per octave of pitch (cross and forked fingerings in the upper register)
	// radiation
	real rad_hz;  // radiation high-pass
	real tone_hz; // directivity low-pass
	real tone_q;  //
	// air
	real breath;  // jet turbulence riding on the flow's pulses
	real hiss;    // steady jet turbulence (re the mean flow)
	real t_noise; // tongue "t"
	// attack
	real tongue;      // strike sensitivity of the accent (the pressure built up behind the tongue)
	real att_rate;    // strike sensitivity of the attack's rate: how fast the tongue lets the reed go, and how far it held it shut
	real att_noise;   // strike sensitivity of the attack's noise (the tongue's "t")
	real acc_amp;     // the pressure built up behind the tongue: overshoot
	real acc_t;       // its decay (s)
	real tongue_t;    // tongue release time of a soft "d" (s); a hard strike is ~16x faster
	real tongue_shut; // a tongued start: the tongue held the reed this far closed (at a full strike)
	real slur;        // articulation on a legato note change
	real pitch_t;     // the player's ear: pitch correction time constant (s)
	// modulation
	real flutter, flut_hz; // shake -> flutter tongue
};
static constexpr int NPARAMS = sizeof(Params) / sizeof(real);
static_assert(sizeof(Params) == NPARAMS * sizeof(real), "Params must hold only floats");

// The factory presets: presets/doublereed/NN-<name>.preset.json, compiled in (the end of this file);
// their values fitted to recordings (above). An instrument each; the bore follows the note (its
// sounding length: the fingering's first open hole), every register on the bore's first mode; the
// register's character comes from the parameters' keytracking about the reference note (ref_note):
//   0 oboe         fitted to the University of Iowa MIS oboe (pp/mf/ff chromatic scales); ref_note 69
//   1 cor anglais  the oboe's player on the cor anglais's geometry, not fitted (no permissively
//                  licensed recordings): the cone a fifth longer (apex x1.5) and wider (bore_d
//                  x1.15), a larger reed (reed_hz x0.85, reed_open x1.15, reed_w x1.07), the bulb's
//                  darker lattice (lattice_hz x0.75); ref_note 62
//   2 bassoon      fitted to the University of Iowa MIS bassoon (pp/mf/ff chromatic scales); ref_note 46
// The output gains put each preset's mf level at v1's.
static constexpr int NUM_PRESETS = 3;
[[nodiscard]] static constexpr const char* preset_name(int i);
[[nodiscard]] static inline Params preset_params(int i);

#ifdef DOUBLEREED_PRESET
static constexpr int DEFAULT_PRESET = DOUBLEREED_PRESET;
#else
static constexpr int DEFAULT_PRESET = 0;
#endif

struct VoiceState {
	// reed: blade position y (m, 0 at rest, -h0 shut) and velocity; the input pressure, the cavity's
	// flow and the near field's (apex inertance) flow
	real y, yv, p, ic, ul;
	real um;      // the jet's mean flow
	real tlev;    // the strike the tongue has held the reed for
	int age;      // blocks since the note started from silence
	real sage;    // seconds since the note's strike (a start, or a new note): the window a strike counts in
	int was_held; // the key was held in the last block (a press on a voice still sounding its release)
	// bore
	real l1, l2; // lattice lowpass (SVF) states
	real D;      // loop delay (samples)
	uint32_t w;
	// radiation
	real rs, t1, t2;
	// player
	real lv1, lv2;           // breath smoother
	real amp;                // breath level after the release logic
	real gate;               // tongue / breath gate
	real gate_rate;          // its opening rate (per control step)
	real acc, tn;            // accent and tongue noise envelopes
	real racc;               // the attack rate's strike envelope (as acc, of the rate's strike)
	real accf;               // the accent's pressure
	real ty1, ty2;           // timbre smoother
	real pm_prev;            // mouth pressure at the end of the last block
	real jn, dr;             // embouchure jitter, slow wander
	real wb1, wb2, wl1, wl2; // the breath's and the lips' wander (2-pole low-passed noise)
	real flut_ph;            //
	awva::NoteGlide glide;   // the fingering: a slur's step of the note over GLIDE_S
	real note_prev;          //
	real vneg;               // release speed latch
	uint32_t rng, nrng;
	real level; // smoothed peak (activity)
	// the player's ear: the reed's period from its zero crossings, the bore trimmed onto the note
	real ylp, yhp, yprev, tlast, pprev, trim, yamp;
	real pth; // the oscillation threshold (Pa)
	real Dff; // the loop delay from the small-signal loop gain (before the ear's trim)
	int active;
};

struct Engine {
	awva::InstrumentControls g;
	awva::NoteControls vc[MAX_VOICES];
	int nvoices;
	int preset;
	Params p;         // as set (preset, live edits)
	Params ps;        // smoothed copy used by the DSP
	BulkMemory* bulk; // the host's (init): the bore delay lines
	real sr, dt, pi_sr, ctrl_dt, block_dt;
	int glide_n; // a slur's fingering change, in blocks (GLIDE_S)
	awva::SvfCoef lv_c, ty_c;
	real shake_pk, shake;
	real dc, dc_c;
	VoiceState vs[MAX_VOICES];
	real vout[BLOCK];
	real mix[BLOCK];
	real pmc[NCTRL]; // mouth pressure at the control points
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

// atan2(y, x) for y >= 0: in [0, pi]
[[nodiscard]] static inline real atan2p(real y, real x) {
	if (x > 0.0f) return atans(y / x);
	if (x < 0.0f) return PI - atans(y / -x);
	return 0.5f * PI;
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

struct Cx {
	real re, im;
};
[[nodiscard]] static inline Cx cmul(Cx a, Cx b) { return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re}; }
[[nodiscard]] static inline Cx cinv(Cx a) {
	const real d = 1.0f / rmax(a.re * a.re + a.im * a.im, 1e-30f);
	return {a.re * d, -a.im * d};
}

// ---------------------------------------------------------------------------------------------
// setup

static inline void reset_voice(Engine& e, VoiceState& s, int v) {
	s.y = s.yv = s.p = s.ic = s.ul = s.um = 0.0f;
	s.tlev = 0.0f;
	s.age = 0;
	s.sage = 0.0f;
	s.was_held = 0;
	s.l1 = s.l2 = 0.0f;
	s.D = 100.0f;
	s.w = 0;
	s.rs = s.t1 = s.t2 = 0.0f;
	s.lv1 = s.lv2 = s.amp = s.gate = 0.0f;
	s.gate_rate = 0.0f;
	s.acc = s.tn = s.accf = 0.0f;
	s.racc = 0.0f;
	s.ty1 = s.ty2 = 0.0f;
	s.pm_prev = 0.0f;
	s.jn = s.dr = 0.0f;
	s.wb1 = s.wb2 = s.wl1 = s.wl2 = 0.0f;
	s.vneg = 0.0f;
	s.level = 0.0f;
	s.ylp = s.yhp = s.yprev = 0.0f;
	s.tlast = -1e4f;
	s.pprev = 0.0f;
	s.trim = s.yamp = 0.0f;
	s.pth = 1000.0f;
	s.Dff = 100.0f;
	if (v >= 0 && v < MAX_VOICES)
		for (int i = 0; i < BLEN; i++) e.bulk->bore[v][i] = 0.0f;
}

static inline void update_derived(Engine& e) {
	e.lv_c = awva::svf_from_g(awva::tan0(e.pi_sr * CTRL * rmin(e.p.lv_hz, 400.0f)), 0.5f);
}

// after the parameters of preset index were set (set_preset, or a preset loaded by name: -1)
static inline void params_loaded(Engine& e, int index) {
	e.preset = index;
	e.ps = e.p;
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
	e.block_dt = (real)BLOCK / sr;
	e.glide_n = awva::NoteGlide::blocks_for(GLIDE_S, sr, BLOCK);
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
		s.flut_ph = 0.31f * (real)v;
		s.flut_ph -= (real)(int)s.flut_ph;
		s.note_prev = 69.0f;
		s.glide = {};
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
// one voice, one block

static inline void process_voice(Engine& e, int vi) {
	VoiceState& s = e.vs[vi];
	const Params& P = e.ps;
	const awva::NoteControls cc = e.vc[vi];
	const real pres = clampr(cc.pressure, 0.0f, 1.5f);
	// after a release a host may leave the last vpres in place: a positive leftover must not
	// strike or hold a released voice; negative values still set the release speed
	const real vpres = clampr(cc.pressure_rate, -1.0f, pres > 0.0f ? 1.0f : 0.0f);
	const real vp1 = rmax(vpres - 0.02f, 0.0f);
	const real vplev = rmin(rmax(P.tongue, 0.0f) * vp1 / (0.4f + vp1), 1.0f);
	const real vrlev = rmin(rmax(P.att_rate, 0.0f) * vp1 / (0.4f + vp1), 1.0f);
	const real vnlev = rmin(rmax(P.att_noise, 0.0f) * vp1 / (0.4f + vp1), 1.0f);
	real target = clampr(cc.pitch, 0.0f, 132.0f); // the host's note (a slur's articulation follows it)
	while (target > NOTE_HI) target -= 12.0f;
	while (target < NOTE_LO) target += 12.0f;
	const bool held = pres > 0.0f;
	// a slur changes the fingering over GLIDE_S (note_glide.h: at once, the reed slaps shut once)
	const real note = s.glide.next(target, held, e.glide_n);
	bool start = false;
	if (!s.active) {
		if (!held && vplev <= 0.0f) {
			s.note_prev = target;
			return;
		}
		s.active = 1;
		start = true;
	}

	// a press: a start, or the key pressed again on a voice still sounding its release (the same note
	// played again): its articulation as a start's (the strike's window, the tongue), the state kept
	const bool press = start || (held && !s.was_held);
	s.was_held = held;

	// ---- block rate: the player and the instrument for this note ----------------------------------
	const bool jump = start || fabsf(target - s.note_prev) >= 1.0f;
	if (jump && !start && held) {
		s.acc = rmax(s.acc, P.slur); // legato change: a small articulation
		s.racc = rmax(s.racc, P.slur);
	}
	if (jump) s.vneg = 0.0f;
	// only a strike strikes: the key's speed counts while the note starts (STRIKE_WIN after a start or a
	// new note); later in the note a swell of the pressure (a positive vpres) is breath, not the tongue
	s.sage = (jump || press) ? 0.0f : rmin(s.sage + e.block_dt, 1.0f);
	const bool striking = s.sage < STRIKE_WIN;
	const real vstrike = striking ? vplev : 0.0f; // the accent's strike
	const real vrate = striking ? vrlev : 0.0f;   // the attack rate's
	const real vnoise = striking ? vnlev : 0.0f;  // the attack noise's
	s.note_prev = target;
	if (start) {
		// from silence: the reed at rest, the bore quiet
		s.y = s.yv = s.p = s.ic = s.ul = s.um = 0.0f;
		s.gate = 0.0f;
		s.lv1 = s.lv2 = 0.0f;
		s.amp = 0.0f;
		s.pm_prev = 0.0f;
	}
	// a strike opens the tongue: fast for a hard one
	if (vrate > 0.05f && (press || vrate > s.racc + 0.05f)) s.gate_rate = rmin(e.ctrl_dt / rmax(P.tongue_t * exp2s(-4.0f * vrate), 0.0005f), 1.0f);
	if (press && vrate <= 0.05f) s.gate_rate = rmin(e.ctrl_dt / rmax(P.tongue_t, 0.0005f), 1.0f);

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
	// embouchure life: jitter (~35 Hz noise) and a slow wander
	s.jn += 0.15f * (6.0f * block_noise(s.rng) - s.jn);
	s.jn = clampr(s.jn, -4.0f, 4.0f);
	s.dr += 0.003f * (45.0f * block_noise(s.rng) - s.dr);
	s.dr = clampr(s.dr, -4.0f, 4.0f);
	// the player's wander: white noise through two one-poles at wander_hz, scaled to unit std
	real wbr = 0.0f, wlp = 0.0f;
	if (P.wander > 0.0f || P.lip_wander > 0.0f) {
		const real aw = clampr(2.0f * PI * clampr(P.wander_hz, 0.3f, 30.0f) * e.block_dt, 1e-4f, 0.5f);
		const real nsc = fsqrt(12.0f / aw); // ~ unit std after two poles (uniform noise, var 1/3 x aw / 4)
		s.wb1 += aw * (block_noise(s.rng) - s.wb1);
		s.wb2 += aw * (s.wb1 - s.wb2);
		s.wl1 += aw * (block_noise(s.rng) - s.wl1);
		s.wl2 += aw * (s.wl1 - s.wl2);
		wbr = clampr(clampr(P.wander, 0.0f, 0.3f) * nsc * s.wb2, -0.5f, 0.5f);
		wlp = clampr(clampr(P.lip_wander, 0.0f, 0.3f) * nsc * s.wl2, -0.5f, 0.5f);
	}

	const real sr = e.sr, dt = e.dt;
	const real ftgt = note_freq(note + clampr(P.bite, -100.0f, 100.0f) * tmb * (1.0f / 100));
	const real f0 = rmin(ftgt, 0.4f * sr);
	const real P0 = sr / f0;
	const real oct = (note - P.ref_note) * (1.0f / 12); // octaves re the reference note
	// reed: resonance, damping, opening, stiffness
	const real bitef = clampr(P.emb_tilt, -1.0f, 1.0f) * tmb; // octaves of bite
	const real rc = rmax(P.jitter, 0.0f) * (s.jn + 0.5f * s.dr);
	const real fr = clampr(P.reed_hz * exp2s(P.reed_reg * oct + 0.5f * bitef + rc * (1.0f / 1200)), 100.0f, 0.4f * sr);
	const real wr = 2.0f * PI * fr;
	const real wr2 = wr * wr;
	const real rq = clampr(P.reed_q * exp2s(-0.5f * bitef), 0.3f, 50.0f);
	// the lips: the opening follows the breath (the blades' stiffness stays: the closing pressure with it)
	const real es = clampr(P.emb_soft, 0.1f, 1.0f);
	const real lf = rmin(es + (1.0f - es) * rmax(s.amp, 0.0f), 1.0f);
	const real h0 = clampr(P.reed_open * 1e-3f * lf * exp2s(-P.open_reg * oct - bitef) * (1.0f + wlp), 2e-5f, 3e-3f);
	const real pM = clampr(P.p_close * 1000.0f * lf * exp2s(-P.open_reg * oct - bitef) * (1.0f + wlp), 300.0f, 60000.0f);
	const real ks = pM / h0; // the blades' stiffness per area (Pa / m)
	const real iks = 1.0f / ks;
	const real rw = clampr(P.reed_w * 1e-3f, 1e-3f, 0.03f);
	const real ihd = 1.0f / clampr(P.duct * 1e-3f, 2e-5f, 0.01f);
	const real c1 = dt * wr / rq; // damping per sample
	const real kcon = rmax(P.contact, 0.0f) * wr2;
	const real ccon = dt * 2.0f * fsqrt(kcon); // contact damping
	// bore input: wave impedance, the near field's inertance, the cavity's compliance
	const real r0 = clampr(P.bore_d, 1.0f, 20.0f) * 0.5e-3f;
	const real S0 = PI * r0 * r0;
	const real Z0 = RHO * C0 / S0;
	const real iZ0 = 1.0f / Z0;
	const real x0 = clampr(P.apex, 0.5f, 100.0f) * 0.01f;
	const real La = RHO * x0 / S0;
	const real Cv = clampr(P.cavity, 0.0f, 10.0f) * (S0 * x0 * (1.0f / 3)) / (RHO * C0 * C0);
	const real alpha = 2.0f * Cv / dt; // trapezoidal compliance
	const real beta = 0.5f * dt / La;  // trapezoidal inertance
	const real A = alpha + iZ0 + beta;
	// the lattice: reflection lowpass (cutoff above the note, as the fingerings keep it), wall losses
	const real fc = clampr(rmax(P.lat_hz * exp2s(P.lat_reg * oct), 1.2f * f0), 50.0f, 0.42f * sr);
	const real lq = clampr(P.lat_q, 0.4f, 4.0f);
	const real glc = tanp(e.pi_sr * fc);
	const awva::SvfCoef lc = awva::svf_from_g(glc, lq);
	// the loop delay at the note: the input impedance peaks where the reflection's phase makes the
	// input's susceptance vanish (lossless): tan(theta / 2) = X, X = Z0 (w C - 1 / (w La)) (warped)
	const real t0 = tanp(rmin(PI / P0, 1.3f));
	const real ww = 2.0f * sr * t0;
	const real X = Z0 * (ww * Cv - 1.0f / (ww * La));
	const real theta = 2.0f * atans(X);
	const real Om = t0 / glc; // the lowpass's warped frequency re its cutoff
	const real phlp = -atan2p(Om / lq, 1.0f - Om * Om);
	real Dn = (PI + phlp - theta) * P0 * (0.5f / PI);
	if (Dn < 2.0f) Dn += P0;
	Dn = clampr(Dn, 2.0f, DMAX);
	// the round trip's loss; the reflection is passive: g x the lowpass's peak (Q / sqrt(1 - 1 / 4Q^2)
	// above Q = 1 / sqrt 2) stays below 0.98
	const real lpk = lq > 0.7072f ? lq / fsqrt(1.0f - 0.25f / (lq * lq)) : 1.0f;
	const real g = rmin(exp2s(-1.442695f * Dn / (clampr(P.bore_t * exp2s(-P.bore_reg * oct), 0.002f, 0.5f) * sr)), 0.98f / lpk);
	// What the reed plays on: the small-signal loop gain L = Y_r Z_in of the reed's admittance (the jet
	// modulated by the blades' motion, which lags through their resonance, minus the jet's own
	// conductance; at the present mouth pressure) and the input impedance (the bore's from its round
	// trip reflection -g LP e^{-i w D}, the near field, the cavity) must be real at the note: a Newton
	// step on D per block (four at a new note). The reed's resonance pulls the note; this puts it back.
	if (jump) {
		s.trim = 0.0f;
		s.tlast = -1e4f;
		s.Dff = Dn;
	}
	{
		const real dpo = rmax(rmax(s.pm_prev, s.pth), 50.0f);
		const real sq0 = fsqrt(dpo);
		const real hb = rmax(h0 - dpo * iks, 0.05f * h0);
		const real hn = hb * ihd;
		const real den = 1.0f + hn * hn;
		const real iden = 1.0f / fsqrt(den);
		const real a2 = 1.2909944f; // sqrt(2 / rho)
		const real Kb = rw * hb * a2 * iden;
		const real dudh = rw * a2 * sq0 * iden / den;
		const real dudp = 0.5f * Kb / sq0;
		const real fq = f0 / fr;
		const Cx Hr = cinv({1.0f - fq * fq, fq / rq});
		const Cx Yr = {dudh * iks * Hr.re - dudp, dudh * iks * Hr.im};
		const Cx lp = cinv({1.0f - Om * Om, Om / lq});
		const real Bx = ww * Cv - 1.0f / (ww * La);
		auto argL = [&](real D) -> real {
			real tw = -D / P0;
			tw -= (real)(int)tw;
			if (tw < 0.0f) tw += 1.0f;
			// cos(2 pi tw), sin(2 pi tw): parabolic sine refined (error ~1e-3)
			const real sn0 = psin(tw);
			const real sn = sn0 * (0.775f + 0.225f * fabsf(sn0));
			real tc = tw + 0.25f;
			if (tc >= 1.0f) tc -= 1.0f;
			const real cn0 = psin(tc);
			const real cn = cn0 * (0.775f + 0.225f * fabsf(cn0));
			const Cx G = cmul({-g * lp.re, -g * lp.im}, {cn, sn});
			const Cx yb = cmul({1.0f - G.re, -G.im}, cinv({1.0f + G.re, G.im}));
			const Cx L = cmul(Yr, cinv({yb.re * iZ0, yb.im * iZ0 + Bx}));
			return L.re > 0.0f ? atans(L.im / L.re) : (L.im >= 0.0f ? 1.5f : -1.5f);
		};
		const int iters = jump ? 4 : 1;
		const real lo = rmax(Dn - 0.25f * P0, 2.0f), hi = rmin(Dn + 0.25f * P0, DMAX);
		real D = clampr(s.Dff, lo, hi);
		for (int it = 0; it < iters; it++) {
			const real ph = argL(D), ph2 = argL(D + 0.25f);
			const real der = (ph2 - ph) * 4.0f;
			if (der < -1e-4f) D = clampr(D - clampr(ph / der, -0.03f * P0, 0.03f * P0), lo, hi);
		}
		s.Dff = D;
	}
	s.D = clampr(s.Dff * exp2s(s.trim * (1.0f / 1200)), 2.0f, DMAX);
	const real Dl = s.D;
	// the threshold: the quasi-static reed's conductance meets the bore's at the resonance
	{
		const real Om2 = Om * Om;
		const real hm2 = 1.0f / ((1.0f - Om2) * (1.0f - Om2) + Om2 / (lq * lq));
		const real r = g * fsqrt(hm2);
		const real Gb = iZ0 * (1.0f - r) / (1.0f + r);
		const real a = 1.2909944f; // sqrt(2 / rho)
		const real wa = rw * a;
		const real sq = (2.0f * Gb + fsqrt(4.0f * Gb * Gb + 12.0f * wa * wa * h0 * iks)) / (6.0f * wa * iks);
		s.pth = sq * sq;
	}
	const real pthr = clampr(s.pth, 50.0f, pM);

	// radiation filters
	const real frd = clampr(P.rad_hz, 10.0f, 4000.0f);
	const real grr = tanp(e.pi_sr * frd);
	const real Gr = grr / (1.0f + grr);
	const awva::SvfCoef tcf = awva::svf_pade(e.pi_sr * clampr(P.tone_hz, 200.0f, 16000.0f), clampr(P.tone_q, 0.5f, 4.0f));
	const real gain = clampr(P.gain, 0.0f, 100.0f) * 1e-4f;

	// ---- control rate: breath, tongue, accent ----------------------------------------------------
	if (vpres > 0.1f) s.vneg = 0.0f;
	if (held && vpres < 0.0f) s.vneg = rmax(s.vneg, -vpres);
	const real ped = clampr(e.g.sustain, 0.0f, 1.0f);
	const real rel_tc = rmax(P.rel + P.rel_ped * ped * ped, 0.004f) / (1.0f + 3.0f * s.vneg);
	const real fac = 1.0f - e.ctrl_dt / (held ? 0.03f : rel_tc);
	const real fac_acc = 1.0f - e.ctrl_dt / rmax(P.acc_t, 0.005f);
	const real fac_tn = 1.0f - e.ctrl_dt / 0.006f;
	const real fac_gate = rmin(e.ctrl_dt / rel_tc, 1.0f);
	const real pcl = pres * (P.pc_a + P.pc_b * pres);
	const real p_lo = pthr * clampr(P.p_soft, 0.3f, 4.0f);
	const real p_hi = rmax(pM * clampr(P.p_loud, 0.1f, 3.0f) * exp2s(P.p_reg * oct), p_lo * 1.05f);
	const real pcv = clampr(P.p_curve, 0.3f, 3.0f);
	const real shake = e.shake * rmax(P.flutter, 0.0f);
	const real flut_inc = P.flut_hz * e.ctrl_dt;
	{
		real lv1 = s.lv1, lv2 = s.lv2, amp = s.amp, acc = s.acc, racc = s.racc, accf = s.accf, tn = s.tn, gate = s.gate, fph = s.flut_ph;
		// the lungs follow the key slowly (the breath smoother); the tongue's accent is fast
		const real fac_af = rmin(e.ctrl_dt / rmax(rmax(P.tongue_t, 0.0005f) * exp2s(-4.0f * acc), 0.001f), 1.0f);
		const awva::SvfCoef bc = e.lv_c;
		for (int k = 0; k < NCTRL; k++) {
			acc = rmax(acc * fac_acc, vstrike);
			racc = rmax(racc * fac_acc, vrate);
			tn = rmax(tn * fac_tn, vnoise);
			real t1, ls;
			SVF_TICK(rmin(pcl, 2.0f), lv1, lv2, bc, t1, ls);
			amp = rmax(rmax(ls, 0.0f), amp * fac);
			accf += fac_af * (P.acc_amp * acc - accf);
			if (held) gate += s.gate_rate * (1.0f - gate);
			else gate -= fac_gate * gate;
			fph += flut_inc;
			if (fph >= 1.0f) fph -= 1.0f;
			const real am = 1.0f - shake * (0.5f + 0.5f * psin(fph));
			// breath -> mouth pressure (amp^p_curve: exp2s(p_curve log2 amp) without libm)
			const real a = rmin(amp + accf, 1.5f);
			const real ap = (pcv != 1.0f && a > 1e-6f) ? exp2s(pcv * log2s(a)) : a;
			e.pmc[k] = gate * am * (p_lo + (p_hi - p_lo) * ap) * (1.0f + wbr);
			e.tnc[k] = P.t_noise * tn * tn;
		}
		s.lv1 = lv1;
		s.lv2 = lv2;
		s.amp = amp;
		s.acc = acc;
		s.racc = racc;
		s.accf = accf;
		s.tn = tn;
		s.gate = gate;
		s.flut_ph = fph;
	}

	// ---- audio rate: reed, input, bore, lattice --------------------------------------------------
	float* __restrict__ B = e.bulk->bore[vi];
	// a tongued start: while the strike rises (the first ~16 ms of a note from silence) the tongue
	// holds the reed closed, more for a harder strike; at its peak it lets it go
	if (press) {
		s.age = 0;
		s.tlev = 0.0f;
	} else if (s.age < 1000) {
		s.age++;
	}
	if (s.age < 24 && vrate > s.tlev + 0.02f) {
		s.tlev = vrate;
		s.y = rmin(s.y, -h0 * clampr(P.tongue_shut, 0.0f, 1.2f) * vrate);
		s.yv = rmin(s.yv, 0.0f);
	}
	real y = s.y, yv = s.yv, p = s.p, ic = s.ic, ul = s.ul, um = s.um;
	real l1 = s.l1, l2 = s.l2;
	uint32_t w = s.w, nr = s.nrng;
	real* __restrict__ out = e.vout;
	const uint32_t id = (uint32_t)(int)Dl;
	const real fd = Dl - (real)(int)id;
	const real br = rmax(P.breath, 0.0f);
	const real hs = rmax(P.hiss, 0.0f);
	real pm = s.pm_prev;
	real peak = 0.0f;
	// pitch tracker: blade position low-passed (~1.3 f0), DC removed, upward zero crossings
	const real a_lp = clampr(2.0f * PI * 1.3f * f0 * dt, 0.001f, 0.9f);
	real ylp = s.ylp, yhp = s.yhp, yprev = s.yprev, tlast = s.tlast - (real)BLOCK, pprev = s.pprev, yamp = 0.0f;
	int ncross = 0;
	real per_sum = 0.0f;
	const real a2 = 1.2909944f; // sqrt(2 / rho)
	for (int k = 0; k < NCTRL; k++) {
		const real dpm = (e.pmc[k] - pm) * (1.0f / CTRL);
		const real tnl = e.tnc[k] * 2e-4f;
		for (int j = 0; j < CTRL; j++) {
			pm += dpm;
			// the wave returning from the lattice, at the input (-g x lowpass of the round trip)
			const real r0 = B[(w - id) & BMASK], r1 = B[(w - id - 1u) & BMASK];
			const real rd = r0 + fd * (r1 - r0);
			real lb, ll;
			SVF_TICK(rd, l1, l2, lc, lb, ll);
			const real pminus = -g * ll;
			// the jet through the blades (Bernoulli, the duct's head loss), solved with the input:
			// u(dp) + A p = Bq, dp = pm - p
			const real h = rmax(h0 + y, 0.0f);
			const real hn = h * ihd;
			const real K = rw * h * a2 / fsqrt(1.0f + hn * hn);
			const real Bq = alpha * p + ic + 2.0f * pminus * iZ0 - ul - beta * p;
			const real Q = A * pm - Bq;
			const real aq = fabsf(Q);
			const real sq = 2.0f * aq / (K + fsqrt(K * K + 4.0f * A * aq) + 1e-20f);
			const real dp = Q >= 0.0f ? sq * sq : -sq * sq;
			const real u = Q >= 0.0f ? K * sq : -K * sq;
			const real pn = clampr(pm - dp, -60000.0f, 60000.0f);
			// the cavity and the near field (trapezoidal)
			ic = alpha * (pn - p) - ic;
			ul = clampr(ul + beta * (pn + p), -0.05f, 0.05f);
			p = pn;
			// turbulence: riding on the flow's pulses, steady, the tongue's "t" (radiated with the flow)
			nr = nr * 1664525u + 1013904223u;
			const real nz = (real)(int32_t)nr * 4.656613e-10f;
			um += 0.0005f * (u - um);
			const real un = (br * (u - um) + hs * um + tnl) * nz;
			// the wave leaving into the bore: p+ = p - p- (+ the turbulence's pressure on the wave impedance)
			B[w] = p - pminus + 0.5f * Z0 * un;
			w = (w + 1u) & BMASK;
			// the blades: blown closed, contact when shut
			{
				const real yq = -dp * iks; // quasi-static position
				if (h0 + y > 0.0f) yv = (yv + dt * (-wr2 * (y - yq))) / (1.0f + c1);
				else yv = (yv + dt * (-wr2 * (y - yq) - kcon * (h0 + y))) / (1.0f + c1 + ccon);
			}
			y = clampr(y + dt * yv, -2.0f * h0, 3.0f * h0);
			ylp += a_lp * (y - ylp);
			yhp += 0.004f * (ylp - yhp);
			{
				const real yc = ylp - yhp;
				yamp = rmax(yamp, fabsf(yc));
				if (yprev < 0.0f && yc >= 0.0f) {
					const real tc = (real)(k * CTRL + j) - yc / (yc - yprev);
					if (tlast > -4000.0f) {
						const real per = tc - tlast;
						if (fabsf(per - pprev) < 0.05f * per) { // a clean, periodic regime
							per_sum += per;
							ncross++;
						}
						pprev = per;
					}
					tlast = tc;
				}
				yprev = yc;
			}
			// what the lattice transmits radiates (1 - its reflection)
			out[k * CTRL + j] = rd - ll;
		}
	}
	s.y = y;
	s.yv = yv;
	s.p = p;
	s.ic = ic;
	s.ul = ul;
	s.um = um;
	s.l1 = l1;
	s.l2 = l2;
	s.w = w;
	s.nrng = nr;
	s.pm_prev = pm;
	s.ylp = ylp;
	s.yhp = yhp;
	s.yprev = yprev;
	s.tlast = tlast;
	s.pprev = pprev;
	s.yamp = rmax(yamp, s.yamp + 0.1f * (yamp - s.yamp));
	// the player's ear: a slow trim of the bore onto the note
	if (ncross > 0 && held && s.yamp > 1e-3f * h0) {
		const real err = 1200.0f * log2s(sr * (real)ncross / (rmax(per_sum, 1.0f) * f0));
		if (fabsf(err) < 400.0f) s.trim = clampr(s.trim + clampr(err * rmin(per_sum * dt / clampr(P.pitch_t, 0.002f, 1.0f), 0.5f), -12.0f, 12.0f), -350.0f, 350.0f);
	}

	// radiation high-pass, directivity low-pass, gain
	{
		real rs = s.rs, t1 = s.t1, t2 = s.t2;
		for (int n = 0; n < BLOCK; n++) {
			const real o = out[n];
			const real vr = (o - rs) * Gr;
			const real lr = vr + rs;
			rs = lr + vr;
			const real hp = o - lr;
			real tb, ty;
			SVF_TICK(hp, t1, t2, tcf, tb, ty);
			const real yo = ty * gain;
			out[n] = yo;
			peak = rmax(peak, fabsf(yo));
		}
		s.rs = rs;
		s.t1 = t1;
		s.t2 = t2;
	}

	// guard: non-finite or absurd state -> reset this voice and drop its block
	const real st = fabsf(s.p) + fabsf(s.l1) + fabsf(s.l2) + fabsf(s.rs) + fabsf(s.t1) + fabsf(s.t2) + fabsf(s.y) * 1e6f + fabsf(s.yv) + fabsf(s.ic) * 1e3f + fabsf(s.amp);
	if (!(peak < 1e3f) || !(st < 1e7f)) {
		reset_voice(e, s, vi);
		s.active = 0;
		return;
	}
	real* __restrict__ mix = e.mix;
	for (int n = 0; n < BLOCK; n++) mix[n] += out[n];
	s.level += 0.05f * (peak - s.level);
	// idle once released and faded out; the state is kept
	if (!held && vplev <= 0.0f && s.gate < 1e-4f && s.amp < 1e-5f && s.acc < 1e-4f && peak < 1e-5f) s.active = 0;
}

static inline void process(Engine& e, real* __restrict__ output) {
	// smooth the parameters towards the set values (live edits glide in ~13 ms instead of stepping)
	{
		const real* __restrict__ pt = reinterpret_cast<const real*>(&e.p);
		real* __restrict__ ps = reinterpret_cast<real*>(&e.ps);
		for (int i = 0; i < NPARAMS; i++) {
			const real d = pt[i] - ps[i];
			// (the snap is relative: a large value's last steps would be below its float resolution)
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
	// master: DC blocker, then a gentle soft clip with a ceiling of 4.2
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

static constexpr ParamEntry param_table[] = {
    {{"gain", "level", 0.05f, 100.0f, C_, AWVA_TEXT(doublereed, gain)}, &Params::gain},
    {{"pres_lin", "level", 0.0f, 1.0f, C_, AWVA_TEXT(doublereed, pres_lin)}, &Params::pc_a},
    {{"pres_sq", "level", 0.0f, 1.5f, C_, AWVA_TEXT(doublereed, pres_sq)}, &Params::pc_b},
    {{"pres_hz", "level", 2.0f, 200.0f, C_, AWVA_TEXT(doublereed, pres_hz)}, &Params::lv_hz},
    {{"release", "level", 0.005f, 1.0f, C_, AWVA_TEXT(doublereed, release)}, &Params::rel},
    {{"rel_pedal", "level", 0.0f, 3.0f, C_, AWVA_TEXT(doublereed, rel_pedal)}, &Params::rel_ped},
    {{"p_soft", "breath", 0.5f, 3.0f, C_, AWVA_TEXT(doublereed, p_soft)}, &Params::p_soft}, // lightest touch re the threshold
    {{"p_loud", "breath", 0.1f, 3.0f, C_, AWVA_TEXT(doublereed, p_loud)}, &Params::p_loud}, // ff mouth pressure re the closing pressure
    {{"p_reg", "breath", -1.0f, 1.0f, C_, AWVA_TEXT(doublereed, p_reg)}, &Params::p_reg},
    {{"p_curve", "breath", 0.3f, 3.0f, C_, AWVA_TEXT(doublereed, p_curve)}, &Params::p_curve},
    {{"reed_hz", "reed", 200.0f, 8000.0f, C_, AWVA_TEXT(doublereed, reed_hz)}, &Params::reed_hz},
    {{"reed_reg", "reed", -1.0f, 1.5f, C_, AWVA_TEXT(doublereed, reed_reg)}, &Params::reed_reg},
    {{"reed_q", "reed", 0.3f, 30.0f, C_, AWVA_TEXT(doublereed, reed_q)}, &Params::reed_q},
    {{"reed_open", "reed", 0.05f, 2.5f, C_, AWVA_TEXT(doublereed, reed_open)}, &Params::reed_open}, // mm
    {{"open_reg", "reed", -1.0f, 1.5f, C_, AWVA_TEXT(doublereed, open_reg)}, &Params::open_reg},
    {{"reed_w", "reed", 2.0f, 25.0f, C_, AWVA_TEXT(doublereed, reed_w)}, &Params::reed_w},    // mm
    {{"p_close", "reed", 1.0f, 40.0f, C_, AWVA_TEXT(doublereed, p_close)}, &Params::p_close}, // kPa
    {{"contact", "reed", 0.0f, 50.0f, C_, AWVA_TEXT(doublereed, contact)}, &Params::contact},
    {{"duct", "reed", 0.05f, 5.0f, C_, AWVA_TEXT(doublereed, duct)}, &Params::duct}, // mm
    {{"emb_tilt", "reed", -1.0f, 1.0f, C_, AWVA_TEXT(doublereed, emb_tilt)}, &Params::emb_tilt},
    {{"emb_soft", "reed", 0.1f, 1.0f, C_, AWVA_TEXT(doublereed, emb_soft)}, &Params::emb_soft},
    {{"bite", "reed", -100.0f, 100.0f, C_, AWVA_TEXT(doublereed, bite)}, &Params::bite},
    {{"jitter", "reed", 0.0f, 40.0f, C_, AWVA_TEXT(doublereed, jitter)}, &Params::jitter},
    {{"wander", "player", 0.0f, 0.3f, C_, AWVA_TEXT(doublereed, wander)}, &Params::wander},
    {{"lip_wander", "player", 0.0f, 0.3f, C_, AWVA_TEXT(doublereed, lip_wander)}, &Params::lip_wander},
    {{"wander_hz", "player", 0.3f, 30.0f, C_, AWVA_TEXT(doublereed, wander_hz)}, &Params::wander_hz},
    {{"ref_note", "bore", 24.0f, 96.0f, C_, AWVA_TEXT(doublereed, ref_note)}, &Params::ref_note}, // MIDI
    {{"apex", "bore", 0.5f, 80.0f, C_, AWVA_TEXT(doublereed, apex)}, &Params::apex},              // cm
    {{"cavity", "bore", 0.0f, 4.0f, C_, AWVA_TEXT(doublereed, cavity)}, &Params::cavity},         // re the missing cone's volume
    {{"bore_d", "bore", 1.5f, 15.0f, C_, AWVA_TEXT(doublereed, bore_d)}, &Params::bore_d},        // mm
    {{"bore_decay", "bore", 0.003f, 0.3f, C_, AWVA_TEXT(doublereed, bore_decay)}, &Params::bore_t},
    {{"bore_reg", "bore", -1.0f, 2.0f, C_, AWVA_TEXT(doublereed, bore_reg)}, &Params::bore_reg},
    {{"lattice_hz", "holes", 100.0f, 6000.0f, C_, AWVA_TEXT(doublereed, lattice_hz)}, &Params::lat_hz},
    {{"lattice_q", "holes", 0.4f, 4.0f, C_, AWVA_TEXT(doublereed, lattice_q)}, &Params::lat_q},
    {{"lattice_reg", "holes", -1.0f, 1.5f, C_, AWVA_TEXT(doublereed, lattice_reg)}, &Params::lat_reg},
    {{"rad_hz", "radiation", 10.0f, 3000.0f, C_, AWVA_TEXT(doublereed, rad_hz)}, &Params::rad_hz},
    {{"tone_hz", "radiation", 300.0f, 16000.0f, C_, AWVA_TEXT(doublereed, tone_hz)}, &Params::tone_hz},
    {{"tone_q", "radiation", 0.5f, 3.0f, C_, AWVA_TEXT(doublereed, tone_q)}, &Params::tone_q},
    {{"breath", "air", 0.0f, 1.0f, C_, AWVA_TEXT(doublereed, breath)}, &Params::breath},
    {{"hiss", "air", 0.0f, 0.5f, C_, AWVA_TEXT(doublereed, hiss)}, &Params::hiss},
    {{"t_noise", "air", 0.0f, 1.0f, C_, AWVA_TEXT(doublereed, t_noise)}, &Params::t_noise},
    {{"tongue", "attack", 0.0f, 2.0f, C_, AWVA_TEXT(doublereed, tongue)}, &Params::tongue},
    {{"att_rate", "attack", 0.0f, 2.0f, C_, AWVA_TEXT(doublereed, att_rate)}, &Params::att_rate},
    {{"att_noise", "attack", 0.0f, 2.0f, C_, AWVA_TEXT(doublereed, att_noise)}, &Params::att_noise},
    {{"acc_level", "attack", 0.0f, 1.5f, C_, AWVA_TEXT(doublereed, acc_level)}, &Params::acc_amp},
    {{"acc_decay", "attack", 0.005f, 0.3f, C_, AWVA_TEXT(doublereed, acc_decay)}, &Params::acc_t},
    {{"tongue_t", "attack", 0.0005f, 0.05f, C_, AWVA_TEXT(doublereed, tongue_t)}, &Params::tongue_t},
    {{"tongue_shut", "attack", 0.0f, 1.2f, C_, AWVA_TEXT(doublereed, tongue_shut)}, &Params::tongue_shut},
    {{"slur", "attack", 0.0f, 1.0f, C_, AWVA_TEXT(doublereed, slur)}, &Params::slur},
    {{"pitch_t", "attack", 0.002f, 0.5f, C_, AWVA_TEXT(doublereed, pitch_t)}, &Params::pitch_t},
    {{"flutter", "mod", 0.0f, 1.0f, C_, AWVA_TEXT(doublereed, flutter)}, &Params::flutter},
    {{"flut_hz", "mod", 8.0f, 40.0f, C_, AWVA_TEXT(doublereed, flut_hz)}, &Params::flut_hz},
};
static constexpr int NUM_LIVE_PARAMS = sizeof(param_table) / sizeof(param_table[0]);
static_assert(NUM_LIVE_PARAMS == NPARAMS, "every Params field is exposed once");

} // namespace doublereed

namespace awva {
template <> struct EngineParams<doublereed::Engine> {
	static constexpr int count = doublereed::NUM_LIVE_PARAMS;
	[[nodiscard]] static constexpr const ParamDesc* desc(int i) { return (i >= 0 && i < count) ? &doublereed::param_table[i].d : nullptr; }
	[[nodiscard]] static float* ptr(doublereed::Engine& e, int i) { return (i >= 0 && i < count) ? &(e.p.*(doublereed::param_table[i].m)) : nullptr; }
	static void changed(doublereed::Engine& e, int) { doublereed::update_derived(e); }
};
} // namespace awva

// ---------------------------------------------------------------------------------------------
// The factory presets: presets/doublereed/NN-<name>.preset.json, the files themselves
// (presets/doublereed.inc, tools/presets_inc.cmake), each read at compile time; a file that isn't
// every parameter in range doesn't compile (awva/preset.h factory).

namespace doublereed {
#include "presets/doublereed.inc"
static_assert(NUM_FACTORY_PRESETS == NUM_PRESETS, "presets/doublereed/: a file per preset");

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
} // namespace doublereed

#endif // VOICE_DOUBLEREED_H_
