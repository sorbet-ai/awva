// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
/**
 * Wind: physically modelled woodwinds with presets (traits: engines/wind.h, -DWIND_PRESET=n on the host).
 * API: the voice_<name>.h shape (engine.h, adapter.h); live parameters: awva::EngineParams.
 *
 * Presets (realistic separate instruments, level matched):
 *   0 clarinet       B-flat clarinet: hollow chalumeau (odd harmonics), clear clarion
 *   1 bass clarinet  darker, woody, an octave lower
 *   2 soprano sax    bright and focused
 *   3 alto sax       the classic sax: rich, buzzy, formants ~650 Hz and ~2.5 kHz
 *   4 tenor sax      warmer and fatter, formants ~500 Hz / 2 kHz
 *   5 baritone sax   big, dark and gruff
 *   6 oboe           nasal double reed, formants ~1.25 and 3 kHz
 *   7 bassoon        double reed, dark and reedy, formants ~480 Hz / 1.15 kHz
 *   8 flute          air jet on an open pipe: nearly sinusoidal, breathy
 *
 * Three excitations / bores, chosen by the preset:
 *   cylinder (clarinets): a pressure-controlled single reed on one waveguide (closed at the reed,
 *            open at the bell): odd harmonics below the tone-hole lattice cutoff, weak above it.
 *   cone (saxophones, oboe, bassoon): the same reed on a truncated cone, modelled as the main
 *            tube in parallel with a short open tube standing for the missing apex (the
 *            "cylindrical saxophone"). The apex has a fixed length per instrument, so its share
 *            of the period grows towards high notes as on the real instrument; its reflection is
 *            lowpassed (3 f0), which evens out the cone's impedance peaks and keeps the low notes
 *            in their register even when blown hard.
 *   jet (flute): an air jet deflected by the acoustic flow at the mouth hole, travelling half a
 *            period to the edge, which splits it (saturating, offset -> even harmonics).
 *
 * Reed: mass-spring reed with lip damping, Bernoulli flow u = zeta*max(1+x,0)*sqrt|gamma-p|,
 * solved exactly per sample (Guillemain, Kergomard & Voinier 2005). The bell / tone-hole lattice
 * is a 2-pole lowpass in the loop: above its cutoff the bore has no resonances, so the internal
 * spectrum falls and the reed can't squeak. The cutoff opens a little with breath.
 *
 * Output: the wave radiated through the lattice (complement of the loop filter) plus some
 * internal wave, a per-voice brightness that grows with breath; then, shared by all voices (they
 * all play the same preset): radiation highpass, brightness lowpass, two formant bands
 * (instrument body / bore character), the band-limited breath noise bus, DC blocker, soft limit.
 *
 * Tuning: period minus the loop filters' phase delays at f0 (block rate, polynomial atan), minus
 * a measured correction table per preset (voice_wind_tune.h). Delay lengths ramp per sample.
 * Live parameter writes go to e.p; the DSP follows a smoothed copy e.ps (~50 ms), so edits glide.
 *
 * How to play:
 *   pres   breath. Below the speaking pressure only a soft breath; then the tone blooms and gets
 *          louder and brighter. The saxes growl when pushed very hard (pressure > ~1.05) or shaken.
 *   life   (a dial) micro-randomness, never a vibrato: every note a little different (start pitch
 *          offset that settles, embouchure brightness, attack, tongue and breath noise), and a tiny
 *          broadband pitch, level, brightness and breath-noise flicker, each voice its own
 *   vpres  tonguing: a fast strike gives a crisp tongued attack (pressure kick + "t" noise); a
 *          slow press a breathy swell.
 *   but_y  embouchure, subtle: forward a little brighter / edgier, backward darker and breathier
 *          (subtone on the saxes, covered tone on the flute).
 *   note   pitch, vibrato and bends. Automatic vibrato is available as a parameter (off).
 *   pedal  unused.
 * No libm; no divisions in the sample loops; one vsqrt per sample for the reeds.
 */
#ifndef VOICE_WIND_H_
#define VOICE_WIND_H_

#include <stdint.h>
#include <type_traits>
#include <math.h>
#include "controls.h"
#include "note_glide.h"
#include "dspmath.h"
#include "params.h"
#include "preset.h"
#include "svf.h"

namespace wind {

using real = float;
static constexpr int BLOCK = awva::BLOCK;
static constexpr float GLIDE_S = 0.010f; // a slur's fingering change (note_glide.h)
static constexpr int MAX_VOICES = 10;

// delay lines (AXI SRAM): main bore up to a period (cone, flute) or half a period (cylinder) of
// the lowest note (MIDI 20: 1881 samples), short line for the cone apex / the flute's jet
static constexpr int MAIN_LEN = 2048;
static constexpr int SHORT_LEN = 1024;
static constexpr real NOTE_MIN = 18.0f;
static constexpr real NOTE_MAX = 122.0f;
static constexpr real G_TH = 0.36f;     // gamma near the reeds' oscillation threshold
static constexpr real G_TH_JET = 0.42f; // jet drive near the flute's threshold

enum { KIND_CYL = 0,
	KIND_CONE = 1,
	KIND_JET = 2 };

// flute (jet) constants, found by sweeping the model on the host: the open pipe reflects without
// inversion; the jet is deflected against the acoustic flow (sign -1) and travels half a period to
// the edge, which keeps the fundamental in tune whatever the breath; gain, amplitude and the
// asymmetry term give about -12 dB 2nd and -18 dB 3rd harmonic at mezzo forte.
static constexpr real JET_GAIN = 6.0f;
static constexpr real JET_AMP = 0.05f;
static constexpr real JET_H2 = 1.0f;


using awva::rmax;
using awva::rmin;
[[nodiscard]] static inline real clampr(real x, real lo, real hi) { return rmin(rmax(x, lo), hi); }

[[nodiscard]] static inline real fsqrt(real x) {
#if defined(__arm__) && defined(__ARM_FP)
	real r;
	__asm__("vsqrt.f32 %0, %1"
	        : "=t"(r)
	        : "t"(x));
	return r;
#else
	return __builtin_sqrtf(x);
#endif
}

// 2^p for |p| < 60, relative error ~1e-7 (block rate)
[[nodiscard]] static inline real pow2(real p) {
	p = clampr(p, -60.0f, 60.0f);
	int w = (int)p;
	if ((real)w > p) w--;
	const real z = p - (real)w;
	const real f = 1.0f + z * (0.693146839f + z * (0.240231090f + z * (0.0554789288f + z * (0.00968616940f + z * (0.00123824704f + z * 0.000218713408f)))));
	union {
		uint32_t i;
		float f;
	} e = {(uint32_t)(w + 127) << 23};
	return f * e.f;
}
[[nodiscard]] static inline real cos_poly(real x) {
	real x2 = x * x;
	return 1.0f + x2 * (-0.5f + x2 * (1.0f / 24 + x2 * (-1.0f / 720 + x2 * (1.0f / 40320))));
}
[[nodiscard]] static inline real sin_poly(real x) {
	real x2 = x * x;
	return x * (1.0f + x2 * (-1.0f / 6 + x2 * (1.0f / 120 + x2 * (-1.0f / 5040 + x2 * (1.0f / 362880)))));
}
// sine of a phase in turns (any value), block rate
[[nodiscard]] static inline real sin_turns(real ph) {
	ph -= (real)(int)ph;
	if (ph < 0.0f) ph += 1.0f;
	// fold to [-1/4, 1/4] turn
	real x = ph < 0.25f ? ph : (ph < 0.75f ? 0.5f - ph : ph - 1.0f);
	return sin_poly(6.2831853f * x);
}
[[nodiscard]] static inline real atan_pos(real t) {
	bool inv = t > 1.0f;
	real z = inv ? 1.0f / t : t;
	real z2 = z * z;
	real r = z * (0.99997726f + z2 * (-0.33262347f + z2 * (0.19354346f + z2 * (-0.11643287f + z2 * (0.05265332f + z2 * (-0.01172120f))))));
	return inv ? 1.5707963f - r : r;
}
[[nodiscard]] static inline real atan2_r(real y, real x) {
	const real ax = fabsf(x), ay = fabsf(y);
	if (ax + ay <= 0.0f) return 0.0f;
	real a = ax >= ay ? atan_pos(ay / ax) : 1.5707963f - atan_pos(ax / ay);
	if (x < 0.0f) a = 3.1415927f - a;
	return y < 0.0f ? -a : a;
}
[[nodiscard]] static inline real db2lin(real db) { return pow2(db * 0.16609640f); }

// ---------------------------------------------------------------------------------------------
// Sound design parameters (all live, see param_table)

struct Params {
	// breath
	real level;    // output level
	real speak;    // pressure where the tone starts to speak
	real dyn;      // breath drive (gamma) at pressure 1
	real attack;   // breath smoothing, ms
	real dyn_gain; // 0: loudness from the physics only .. 1: also scaled by pressure
	// articulation
	real tongue;       // pressure kick of a fast strike
	real tongue_noise; // "t" noise burst of a fast strike
	// reed (jet: unused)
	real reed_open; // zeta: reed opening / embouchure looseness
	real reed_freq; // reed resonance, Hz (at least 6 f0)
	real reed_damp; // lip damping of the reed
	// bore
	real loss;       // reflection gain at low frequencies
	real lattice;    // tone-hole lattice / bell cutoff, Hz (at least 2 f0)
	real lat_breath; // cutoff opening with breath, octaves per unit pressure
	real apex;       // cone: apex share of the period at the lowest note
	real apex_lp;    // cone: apex reflection lowpass, multiples of f0
	real jet;        // flute: jet travel delay, fraction of the period
	real jet_off;    // flute: jet offset from the edge (even harmonics)
	// tone (shared output stage)
	real body;                            // internal wave mixed into the radiated one
	real rad_hp;                          // radiation highpass, Hz
	real bright;                          // brightness lowpass, Hz
	real br_breath;                       // extra brightness with breath (per voice, grows with pressure^2)
	real f1_hz, f1_db, f2_hz, f2_db, f_q; // formant bands
	// noise
	real breath;   // audible breath (band-limited), relative
	real turb;     // turbulence on the breath drive
	real noise_hz; // breath noise band centre, Hz
	// expression
	real tilt_bright; // embouchure brightness from tilt
	real tilt_breath; // breathiness / subtone with backward tilt
	real growl;       // growl depth when pushed hard
	real growl_from;  // pressure where the growl starts
	real vib;         // automatic vibrato, semitones
	real vib_hz;
	real vib_breath; // automatic breath (amplitude) vibrato
	real life;       // micro-randomness (0 = none)
	real instrument; // which instrument (instruments[]): its bore kind, lowest note and tuning
};
static constexpr int NPARAMS = sizeof(Params) / sizeof(real);

// the instruments: the bore kind, the lowest note (it sets the cone apex length) and the tuning
// correction table (voice_wind_tune.h CORR) of each; the parameter instrument picks one
struct Instrument {
	int kind;
	real low_note;
};

static constexpr int NUM_INSTRUMENTS = 9;
static constexpr Instrument instruments[NUM_INSTRUMENTS] = {
    {KIND_CYL, 50},  // clarinet
    {KIND_CYL, 34},  // bass clarinet
    {KIND_CONE, 56}, // soprano sax
    {KIND_CONE, 49}, // alto sax
    {KIND_CONE, 44}, // tenor sax
    {KIND_CONE, 37}, // baritone sax
    {KIND_CONE, 58}, // oboe
    {KIND_CONE, 34}, // bassoon
    {KIND_JET, 60},  // flute
};

// the factory presets: presets/wind/NN-<name>.preset.json, compiled in (the end of this file)
static constexpr int NUM_PRESETS = 9;
[[nodiscard]] static constexpr const char* preset_name(int i);
[[nodiscard]] static inline Params preset_params(int i);

// measured tuning correction per instrument: samples of extra loop delay at notes 20 + 7k, at
// 48 kHz (a time: Engine::corr_sr scales it to the rate)
#include "voice_wind_tune.h"

[[nodiscard]] static inline real tuning_corr(int instrument, real note) {
	const real* t = CORR[instrument];
	real k = clampr((note - 20.0f) * (1.0f / 7.0f), 0.0f, 12.999f);
	int i = (int)k;
	real f = k - (real)i;
	return t[i] + f * (t[i + 1] - t[i]);
}

// ---------------------------------------------------------------------------------------------

struct VoiceState {
	int active;
	int quiet;
	uint32_t widx, rng;
	real note_prev;
	awva::NoteGlide glide; // the fingering: a slur's step of the note over GLIDE_S
	real ps;               // smoothed pressure
	real ty;               // smoothed tilt
	real tongue;           // articulation envelope
	real growl;            // smoothed growl amount
	real gph;              // growl: throat phase (turns)
	real gsph;             // growl: subharmonic phase (turns, f0 / 2)
	real gn, gd, gb;       // growl: rate and depth wander (slow noise), burst envelope
	real gcd, gcr, gpz;    // growl: this throat cycle's depth and rate, the subharmonic's depth
	uint32_t grng;         // growl noise
	real gm;               // breath multiplier (growl, breath vibrato) at the end of the last block
	real gam;              // breath drive at the end of the last block
	real og;               // output gain at the end of the last block
	real d1, d2;           // delay lengths at the end of the last block
	real lp, lp2;          // loop filter states
	real ls;               // apex lowpass state
	real dcx, dcy;         // flute loop DC blocker
	real x1, x2, pm1, gm1; // reed
	real tn;               // turbulence noise lowpass
	real yp;               // previous output sample (breath brightness)
	// life: per note (start pitch offset, decaying; embouchure brightness; attack speed, tongue kick
	// and breath noise factors) and flicker noises (pitch fast / slow, level, brightness, breath noise)
	real lpo, lbo, lkp, ltg, lnn;
	real lnf, lns, llv, lbr, lnz;
	uint32_t lrng;
};

// the delay lines, 16 KB per voice: the host gives them to the engine (engine contract,
// engines/wind.h; e.g. AXI SRAM)
struct BulkMemory {
	float main[MAX_VOICES][MAIN_LEN];
	float tube[MAX_VOICES][SHORT_LEN]; // the short tube
};

struct Engine {
	awva::InstrumentControls g;
	awva::NoteControls vc[MAX_VOICES];
	int nvoices;
	int preset, kind;
	int instrument;   // e.p.instrument as an index: instruments[], CORR[]
	BulkMemory* bulk; // the host's (init)
	real sr;
	Params p;  // as set (presets, live edits)
	Params ps; // smoothed copy used by the DSP
	real low_note;
	real inv_sr;   // 1 / sample rate
	int glide_n;   // a slur's fingering change, in blocks (GLIDE_S)
	real corr_sr;  // sample rate / 48000: CORR is in samples at 48 kHz, of a delay that is a time
	real kp;       // breath smoothing coefficient (per block)
	real inv_1mp0; // 1 / (1 - speak)
	real inv_p0;   // 1 / speak
	real p_low;    // period of the lowest note (cone apex length)
	real shake;
	real vph; // vibrato phase (turns)
	// shared output stage
	real hp_s, lp_s; // radiation highpass / brightness lowpass states
	awva::SvfState f1s, f2s, nbs;
	real dc1, dcx;
	VoiceState vs[MAX_VOICES];
	real mix[BLOCK];  // voices
	real nmix[BLOCK]; // breath noise bus
};

static inline void reset_voice(Engine& e, int v, bool clear) {
	VoiceState& s = e.vs[v];
	real note = s.note_prev;
	s = VoiceState();
	s.rng = 0x9E3779B9u * (uint32_t)(v + 1);
	s.note_prev = note;
	s.gm = 1.0f;
	s.gcd = s.gcr = s.gpz = 1.0f;
	s.grng = 0x27D4EB2Fu * (uint32_t)(v + 5) + 777u;
	s.lkp = s.ltg = s.lnn = 1.0f;
	s.lrng = 0x165667B1u * (uint32_t)(v + 7) + 4242u;
	s.d1 = 100.0f;
	s.d2 = 10.0f;
	if (clear) {
		for (int i = 0; i < MAIN_LEN; i++) e.bulk->main[v][i] = 0.0f;
		for (int i = 0; i < SHORT_LEN; i++) e.bulk->tube[v][i] = 0.0f;
	}
}

// the instrument of e.p: its lowest note and tuning; a change of excitation kind silences the voices
static inline void instrument_changed(Engine& e) {
	const int k = (int)clampr(e.p.instrument, 0.0f, (real)(NUM_INSTRUMENTS - 1));
	e.instrument = k;
	e.low_note = instruments[k].low_note;
	if (instruments[k].kind != e.kind) {
		e.kind = instruments[k].kind;
		for (int v = 0; v < MAX_VOICES; v++) reset_voice(e, v, true);
	}
}

// e.p, as a preset leaves it, takes effect (index: the factory preset, -1 a preset by name)
static inline void params_loaded(Engine& e, int index) {
	e.preset = index;
	e.ps = e.p;
	instrument_changed(e);
}

// Select a preset (between blocks, any time). A change of excitation kind silences the voices.
static inline void set_preset(Engine& e, int i) {
	if (i < 0 || i >= NUM_PRESETS) i = 0;
	e.p = preset_params(i);
	params_loaded(e, i);
}

// preset after init() (host renders: -DWIND_PRESET=n)
#ifdef WIND_PRESET
static constexpr int DEFAULT_PRESET = WIND_PRESET;
#else
static constexpr int DEFAULT_PRESET = 0;
#endif

static inline void init(Engine& e, int sample_rate, int nvoices) {
	e.sr = (real)sample_rate;
	e.inv_sr = 1.0f / e.sr;
	e.glide_n = awva::NoteGlide::blocks_for(GLIDE_S, e.sr, BLOCK);
	e.corr_sr = e.sr / 48000.0f; // (exactly 1 at 48 kHz)
	e.nvoices = nvoices < MAX_VOICES ? nvoices : MAX_VOICES;
	e.g = awva::instrument_at_rest;
	e.shake = 0.0f;
	e.vph = 0.0f;
	e.hp_s = e.lp_s = 0.0f;
	e.f1s = e.f2s = e.nbs = awva::SvfState();
	e.dc1 = e.dcx = 0.0f;
	for (int n = 0; n < BLOCK; n++) e.mix[n] = e.nmix[n] = 0.0f;
	for (int v = 0; v < MAX_VOICES; v++) {
		e.vc[v] = awva::note_at_rest;
		e.vs[v].note_prev = 69.0f;
		reset_voice(e, v, true);
	}
	e.kind = instruments[(int)preset_params(DEFAULT_PRESET).instrument].kind;
	set_preset(e, DEFAULT_PRESET);
}

// breath pressure -> drive (gamma for the reeds, jet gain for the flute); linear to the speaking
// pressure, then linear to q.dyn at pressure 1
[[nodiscard]] static inline real drive_of(const Params& q, real p, int kind, real inv_p0, real inv_1mp0) {
	const real p0 = clampr(q.speak, 0.02f, 0.6f);
	const real th = kind == KIND_JET ? G_TH_JET : G_TH;
	return p < p0 ? p * th * inv_p0 : th + (q.dyn - th) * (p - p0) * inv_1mp0;
}

// 3rd order Lagrange read at delay d (>= 2) behind write index w
// (FAST: w is a plain index and no slot wraps: the same slots without the masks)
template <bool FAST = false>
[[nodiscard]] static inline real lagrange_read(const float* __restrict__ buf, uint32_t w, real dl) {
	const int i0 = (int)dl - 1;
	const real d = dl - (real)i0;
	const real dm1 = d - 1.0f, dm2 = d - 2.0f, dm3 = d - 3.0f;
	const real p01 = d * dm1, p23 = dm2 * dm3;
	if constexpr (FAST) {
		const float* t = buf + ((int)w - i0);
		return -(dm1 * p23) * (1.0f / 6.0f) * t[0] + (d * p23) * 0.5f * t[-1] - (p01 * dm3) * 0.5f * t[-2] + (p01 * dm2) * (1.0f / 6.0f) * t[-3];
	} else {
		const uint32_t b0 = w - (uint32_t)i0;
		return -(dm1 * p23) * (1.0f / 6.0f) * buf[b0 & (MAIN_LEN - 1)] + (d * p23) * 0.5f * buf[(b0 - 1u) & (MAIN_LEN - 1)] - (p01 * dm3) * 0.5f * buf[(b0 - 2u) & (MAIN_LEN - 1)] + (p01 * dm2) * (1.0f / 6.0f) * buf[(b0 - 3u) & (MAIN_LEN - 1)];
	}
}

// per-block values of a voice, handed to the sample loops
struct BlockCoef {
	real d1, dd1, d2, dd2, gam, dgam, gm, dgm, og, dog;
	real bb0, ba1, ba2, g;   // loop lowpass (2-pole) and reflection gain
	real cbs, gs;            // apex lowpass, apex reflection (cone) / flute: one-pole and DC
	real zeta, rb, r1, r2;   // reed
	real turb, ct, body, bo; // turbulence depth, its lowpass, body mix, breath bus level
	real joff, soff, ja, jn; // flute: jet offset, its saturated value, jet amplitude, jet noise
	real kb;                 // breath brightness (first difference)
};

template <int KIND>
[[nodiscard]] static inline real run_voice(Engine& e, int v, const BlockCoef& c) {
	VoiceState& s = e.vs[v];
	float* __restrict__ mb = e.bulk->main[v];
	float* __restrict__ sb = e.bulk->tube[v];
	real* __restrict__ mix = e.mix;
	real* __restrict__ nmix = e.nmix;
	uint32_t widx = s.widx, rng = s.rng;
	real lp = s.lp, lp2 = s.lp2, ls = s.ls, dcx = s.dcx, dcy = s.dcy;
	real x1 = s.x1, x2 = s.x2, pm1 = s.pm1, gm1 = s.gm1, tn = s.tn, yp = s.yp;
	real d1 = c.d1, d2 = c.d2, gam = c.gam, gm = c.gm, og = c.og;
	const real bb0 = c.bb0, ba1 = c.ba1, ba2 = c.ba2, g = c.g, cbs = c.cbs, gs = c.gs;
	const real zeta = c.zeta, rb = c.rb, r1 = c.r1, r2 = c.r2, turb = c.turb, ct = c.ct, body = c.body, bo = c.bo;
	const real joff = c.joff, soff = c.soff, ja = c.ja, jn = c.jn, kb = c.kb;
	real peak = 0.0f;
	// The delay slots: writes at widx & mask, the Lagrange taps at (widx - i0 .. widx - i0 - 3) &
	// (MAIN_LEN - 1) for i0 = (int)d1 - 1, the short line's at (widx - j0, widx - j0 - 1) &
	// (SHORT_LEN - 1) for j0 = (int)d2; widx rises by one per sample and the delays ramp linearly
	// (their integer parts stay within one of the block's start and end values). When no slot wraps
	// around its buffer's end in this block (most blocks), the loop addresses the same slots as
	// plain indices (the same reads and writes in the same order: identical results).
	const int cm = (int)(widx & (MAIN_LEN - 1)), cs = (int)(widx & (SHORT_LEN - 1));
	int om = 0, os = 0;
	bool fast = cm + BLOCK <= MAIN_LEN;
	{
		const real e1 = c.d1 + (real)BLOCK * c.dd1;
		const bool ok = rmin(c.d1, e1) >= 2.0f && rmax(c.d1, e1) <= (real)(MAIN_LEN - 5);       // (false for NaN)
		const int lo = ok ? (int)rmin(c.d1, e1) - 2 : 0, hi = ok ? (int)rmax(c.d1, e1) + 1 : 0; // (int)d1 - 1 range
		const int y = (int)((widx - (uint32_t)(hi + 3)) & (MAIN_LEN - 1));                      // lowest slot
		fast &= ok & (y + (BLOCK - 1) + (hi - lo) + 3 < MAIN_LEN);
		om = y + hi + 3 - cm; // fast: the Lagrange read's w = cm + n + om
	}
	if (KIND != KIND_CYL) {
		const real e2 = c.d2 + (real)BLOCK * c.dd2;
		const bool ok = rmin(c.d2, e2) >= 1.0f && rmax(c.d2, e2) <= (real)(SHORT_LEN - 2);
		const int lo = ok ? (int)rmin(c.d2, e2) - 1 : 0, hi = ok ? (int)rmax(c.d2, e2) + 1 : 0;
		const int y = (int)((widx - (uint32_t)(hi + 1)) & (SHORT_LEN - 1));
		fast &= ok & (cs + BLOCK <= SHORT_LEN) & (y + (BLOCK - 1) + (hi - lo) + 1 < SHORT_LEN);
		os = y + hi + 1 - cs; // fast: slot = cs + n + os - j0 (- 1)
	}
	auto run = [&](auto fast_tag) {
		constexpr bool FAST = decltype(fast_tag)::value;
		for (int n = 0; n < BLOCK; n++) {
			d1 += c.dd1;
			gam += c.dgam;
			gm += c.dgm;
			og += c.dog;
			if (KIND != KIND_CYL) d2 += c.dd2;
			rng = rng * 1664525u + 1013904223u;
			const real wn = (real)(int32_t)rng * (1.0f / 2147483648.0f);
			tn += ct * (wn - tn);
			nmix[n] += bo * wn;
			const real drv = gam * (gm + turb * tn);
			const real bi = lagrange_read<FAST>(mb, FAST ? (uint32_t)(cm + n + om) : widx, d1);
			real y;
			if (KIND == KIND_JET) {
				// bore: one-pole loss lowpass, DC blocker, reflection
				lp += cbs * (bi - lp);
				dcy = lp - dcx + gs * dcy;
				dcx = lp;
				const real tmp = g * dcy;
				// jet: deflected by the acoustic flow at the mouth hole, the deflection travels with
				// the jet to the edge (delay d2); the edge splits it (saturating, offset -> even
				// harmonics). The gain grows with the breath (jet speed).
				if constexpr (FAST) sb[cs + n] = tmp;
				else sb[widx & (SHORT_LEN - 1)] = tmp;
				const int j0 = (int)d2;
				const real fj = d2 - (real)j0;
				const uint32_t c0 = widx - (uint32_t)j0;
				const real s0 = FAST ? sb[cs + n + os - j0] : sb[c0 & (SHORT_LEN - 1)], s1 = FAST ? sb[cs + n + os - j0 - 1] : sb[(c0 - 1u) & (SHORT_LEN - 1)];
				const real jx = clampr(drv * (jn * tn - JET_GAIN * (s0 + fj * (s1 - s0))) + joff, -1.5f, 1.5f);
				const real jt = (jx * (1.0f - 0.148148148f * jx * jx) - soff) * ja;
				if constexpr (FAST) mb[cm + n] = jt + tmp;
				else mb[widx & (MAIN_LEN - 1)] = jt + tmp;
				widx++;
				y = bi + body * jt + JET_H2 * joff * bi * bi; // bore, jet sound at the mouth hole, jet asymmetry
			} else {
				// lattice / bell: 2-pole lowpass, reflection
				const real yl = bb0 * bi + lp;
				lp = 2.0f * bb0 * bi - ba1 * yl + lp2;
				lp2 = bb0 * bi - ba2 * yl;
				const real am = -g * yl;
				real V, bm = 0.0f;
				if (KIND == KIND_CONE) {
					const int j0 = (int)d2;
					const real fj = d2 - (real)j0;
					const uint32_t c0 = widx - (uint32_t)j0;
					const real s0 = FAST ? sb[cs + n + os - j0] : sb[c0 & (SHORT_LEN - 1)], s1 = FAST ? sb[cs + n + os - j0 - 1] : sb[(c0 - 1u) & (SHORT_LEN - 1)];
					ls += cbs * (s0 + fj * (s1 - s0) - ls);
					bm = -gs * ls;
					V = am + bm;
				} else {
					V = 2.0f * am;
				}
				// reed displacement from the previous pressure difference, then the flow
				const real x = rb * (pm1 - gm1) + r1 * x1 + r2 * x2;
				x2 = x1;
				x1 = x;
				const real W = zeta * rmax(1.0f + x, 0.0f);
				const real X = drv - V;
				const real uabs = 0.5f * W * (fsqrt(W * W + 4.0f * fabsf(X)) - W);
				const real u = X >= 0.0f ? uabs : -uabs;
				const real p = V + u;
				pm1 = p;
				gm1 = drv;
				if (KIND == KIND_CONE) {
					if constexpr (FAST) {
						mb[cm + n] = bm + u;
						sb[cs + n] = am + u;
					} else {
						mb[widx & (MAIN_LEN - 1)] = bm + u;
						sb[widx & (SHORT_LEN - 1)] = am + u;
					}
				} else {
					if constexpr (FAST) mb[cm + n] = am + u;
					else mb[widx & (MAIN_LEN - 1)] = am + u;
				}
				widx++;
				y = (bi + am) + body * bi; // radiated through the lattice + some internal wave
			}
			const real yo = og * (y + kb * (y - yp));
			yp = y;
			y = yo;
			mix[n] += y;
			peak = rmax(peak, fabsf(y));
		}
	};
	if (fast) run(std::true_type{});
	else run(std::false_type{});
	s.widx = widx;
	s.rng = rng;
	s.lp = lp;
	s.lp2 = lp2;
	s.ls = ls;
	s.dcx = dcx;
	s.dcy = dcy;
	s.x1 = x1;
	s.x2 = x2;
	s.pm1 = pm1;
	s.gm1 = gm1;
	s.tn = tn;
	s.yp = yp;
	return peak;
}

static inline void process_voice(Engine& e, int v) {
	VoiceState& s = e.vs[v];
	const awva::NoteControls c = e.vc[v];
	const Params& q = e.ps;
	real pres = clampr(c.pressure, 0.0f, 1.5f);
	if (!(pres == pres)) pres = 0.0f;
	real vp = clampr(c.pressure_rate - 0.02f, 0.0f, 1.0f);
	if (!(vp == vp)) vp = 0.0f;
	const real vplev = vp / (0.3f + vp);
	bool wake = false;
	if (!s.active) {
		if (pres <= 0.0f) {
			s.note_prev = c.pitch;
			return;
		}
		s.active = 1;
		s.quiet = 0;
		wake = true;
	}
	// a slur changes the fingering over GLIDE_S (note_glide.h: at once, the reed slaps shut once)
	real note = s.glide.next(c.pitch, pres > 0.0f, e.glide_n);
	if (!(note == note)) note = 60.0f;
	note = clampr(note, NOTE_MIN, NOTE_MAX);
	const bool jump = fabsf(note - s.note_prev) >= 1.0f;
	s.note_prev = note;

	const real sr = e.sr;
	const int kind = e.kind;
	// life (micro-randomness, never a vibrato): every note draws its own small offsets; the flicker
	// noises are one-pole filtered uniform noise of unit spread (~10 ms and ~300 ms pitch, ~120 ms
	// level, ~80 ms brightness, ~50 ms breath noise). Off (life 0): the voice as without it.
	const real life = clampr(q.life, 0.0f, 1.0f);
	const bool life_on = life > 0.0f;
	real l_cents = 0.0f, l_br = 0.0f, l_lv = 1.0f, l_nz = 1.0f, l_kp = 1.0f, l_tg = 1.0f;
	if (life_on) {
		uint32_t r = s.lrng;
		auto nz = [&r]() {
			r = r * 1664525u + 1013904223u;
			return (real)(int32_t)r * (1.0f / 2147483648.0f);
		};
		if (jump || wake) {
			s.lpo = 3.0f * nz();  // cents
			s.lbo = 0.15f * nz(); // embouchure brightness
			s.lkp = 1.0f + 0.3f * nz();
			s.ltg = 1.0f + 0.25f * nz();
			s.lnn = 1.0f + 0.3f * nz();
		}
		const real dt = BLOCK * e.inv_sr;
		s.lpo *= 1.0f - dt * 10.0f; // settles in ~0.1 s
		const auto flick = [&](real& x, real tau) {
			const real a = rmin(dt / tau, 1.0f);
			x = clampr(x + a * (1.732f * fsqrt(2.0f / a) * nz() - x), -3.0f, 3.0f);
		};
		flick(s.lnf, 0.01f);
		flick(s.lns, 0.3f);
		flick(s.llv, 0.12f);
		flick(s.lbr, 0.08f);
		flick(s.lnz, 0.05f);
		s.lrng = r;
		l_cents = life * (s.lpo + 1.6f * s.lnf + 1.2f * s.lns);
		l_br = life * (s.lbo + 0.06f * s.lbr);
		l_lv = 1.0f + 0.05f * life * s.llv;
		l_nz = rmax(1.0f + life * (s.lnn - 1.0f + 0.35f * s.lnz), 0.0f);
		l_kp = 1.0f + life * (s.lkp - 1.0f);
		l_tg = 1.0f + life * (s.ltg - 1.0f);
	}
	// breath smoothing (attack), tilt, tongue, growl
	s.ps += (life_on ? rmin(e.kp * l_kp, 1.0f) : e.kp) * (pres - s.ps);
	if (s.ps < 1e-6f) s.ps = 0.0f;
	const real ps = s.ps;
	s.ty += 0.03f * (clampr(c.timbre, -1.0f, 1.0f) - s.ty);
	const real ty = s.ty;
	s.tongue = rmax(s.tongue * 0.94f, vplev); // ~16 ms
	if (s.tongue < 1e-6f) s.tongue = 0.0f;
	const real tg = s.tongue;
	const real gr_t = clampr((ps - q.growl_from) * 3.0f, 0.0f, 1.0f) + e.shake;
	s.growl += 0.05f * (rmin(gr_t, 1.2f) - s.growl);
	if (s.growl < 1e-6f) s.growl = 0.0f;
	const real ebr = life_on ? q.tilt_bright * ty + l_br : q.tilt_bright * ty; // embouchure brightness -1..1
	const real sub = q.tilt_breath * rmax(-ty, 0.0f);                          // subtone / covered amount 0..1

	// pitch (with the automatic vibrato) and the fundamental
	const real vibs = q.vib * sin_turns(e.vph + 0.13f * (real)v);
	const real nt = life_on ? note + vibs + 0.01f * l_cents : note + vibs;
	const real P = (sr * (1.0f / 440.0f)) * pow2((69.0f - nt) * (1.0f / 12.0f));
	const real f0 = 440.0f * pow2((nt - 69.0f) * (1.0f / 12.0f));
	const real isr = e.inv_sr;
	const real P_2pi = P * 0.15915494f; // 1 / w0
	const real w0 = 6.2831853f / P;
	const real sw = sin_poly(w0), cw = cos_poly(w0);

	BlockCoef b;
	real tau;
	// loop filter cutoff: lattice, opening with breath and forward embouchure, at least 2 f0
	real fc = q.lattice * pow2(q.lat_breath * (ps - 0.5f) + 0.5f * ebr - 0.3f * sub);
	if (kind == KIND_JET) {
		// at least 3 f0 (its 2nd and 3rd harmonics), at most 7 f0: a jet half a period long drives
		// the 3rd mode as well as the 1st, and a loop that passes it as freely overblows the low notes
		fc = rmin(rmax(fc, 3.0f * f0), 7.0f * f0);
		const real cp = pow2(-fc * (6.2831853f * 1.442695f) * isr);
		b.cbs = 1.0f - cp;
		b.gs = 0.995f; // DC blocker pole
		// phase delays at f0: one-pole lowpass (lag) and DC blocker (lead)
		const real t_lp = atan_pos(cp * sw / (1.0f - cp * cw)) * P_2pi;
		const real ph_dc = atan2_r(sw, 1.0f - cw) - atan2_r(b.gs * sw, 1.0f - b.gs * cw);
		tau = t_lp - ph_dc * P_2pi;
		b.bb0 = b.ba1 = b.ba2 = 0.0f;
	} else {
		fc = rmax(fc, 2.0f * f0);
		const real K = awva::tan0(rmin(fc * 3.1415927f * isr, 1.2f));
		const real bn = 1.0f / (1.0f + 1.4142136f * K + K * K);
		b.bb0 = K * K * bn;
		b.ba1 = 2.0f * (K * K - 1.0f) * bn;
		b.ba2 = (1.0f - 1.4142136f * K + K * K) * bn;
		const real c2w = 2.0f * cw * cw - 1.0f, s2w = 2.0f * sw * cw;
		tau = 1.0f + atan2_r(-(b.ba1 * sw + b.ba2 * s2w), 1.0f + b.ba1 * cw + b.ba2 * c2w) * P_2pi;
		b.cbs = 1.0f;
		b.gs = 0.997f;
	}
	// release: once the breath is gone, the bore dies out in ~0.1 s whatever its length
	const real rel = clampr(1.0f - ps * 20.0f, 0.0f, 1.0f) * (9.97f / 0.1f) * e.inv_sr;
	b.g = clampr(q.loss, 0.5f, 0.999f);

	// reed (mass-spring, centered differences, stable for omega*T < 2)
	{
		const real zeta = clampr(q.reed_open, 0.05f, 1.0f) * (1.0f + 0.25f * ebr) * (1.0f - 0.15f * sub);
		const real fr = rmax(clampr(q.reed_freq, 500.0f, 12000.0f), 6.0f * f0);
		const real wr = rmin(fr * 6.2831853f * isr, 1.4f);
		const real qr = clampr(q.reed_damp, 0.1f, 2.0f) * (1.0f - 0.2f * ebr);
		const real iw = 1.0f / wr;
		const real A = iw * iw, B = 0.5f * qr * iw;
		const real ia0 = 1.0f / (A + B);
		b.zeta = zeta;
		b.rb = ia0;
		b.r1 = (2.0f * A - 1.0f) * ia0;
		b.r2 = -(A - B) * ia0;
	}

	// delay lengths: loop delay + filter phase delays + correction = period
	const real corr = tuning_corr(e.instrument, nt) * e.corr_sr;
	real D1, D2 = 1.0f;
	if (kind == KIND_CYL) {
		D1 = clampr(0.5f * P - tau - corr, 2.0f, (real)(MAIN_LEN - 5));
	} else if (kind == KIND_CONE) {
		// apex: fixed length (its share at the lowest note is q.apex), at most 45% of the period
		const real ap = clampr(q.apex, 0.05f, 0.5f);
		const real beta = clampr(ap * e.p_low * f0 * isr, ap, 0.45f); // p_low / P
		const real cps = pow2(-clampr(q.apex_lp, 1.0f, 30.0f) * f0 * (6.2831853f * 1.442695f) * isr);
		b.cbs = 1.0f - cps;
		const real taus = atan_pos(cps * sw / (1.0f - cps * cw)) * P_2pi;
		D2 = clampr(beta * P - taus, 1.0f, (real)(SHORT_LEN - 2));
		D1 = clampr(P - D2 - taus - tau - corr, 2.0f, (real)(MAIN_LEN - 5));
		b.gs = 0.997f * pow2(-rel * D2);
	} else {
		D1 = clampr(P - tau - corr, 2.0f, (real)(MAIN_LEN - 5));
		D2 = clampr(clampr(q.jet, 0.05f, 1.0f) * P, 1.0f, (real)(SHORT_LEN - 2));
	}
	b.g *= pow2(-rel * D1);
	if (jump || wake) {
		s.d1 = D1;
		s.d2 = D2;
	}
	b.d1 = s.d1;
	b.dd1 = (D1 - s.d1) * (1.0f / BLOCK);
	b.d2 = s.d2;
	b.dd2 = (D2 - s.d2) * (1.0f / BLOCK);
	s.d1 = D1;
	s.d2 = D2;

	// breath drive with tongue kick; growl and breath vibrato as a multiplier
	const real gam_t = rmin(drive_of(q, ps, kind, e.inv_p0, e.inv_1mp0) * (1.0f + (life_on ? q.tongue * l_tg : q.tongue) * tg) * (1.0f - 0.08f * sub), kind == KIND_JET ? 1.4f : 0.94f);
	b.gam = s.gam;
	b.dgam = (gam_t - s.gam) * (1.0f / BLOCK);
	s.gam = gam_t;
	// growl: an irregular throat flutter (~15 Hz; rate and depth wander over ~0.15-0.25 s, every
	// cycle its own depth and length, now and then a burst) dips the drive, and a drive modulation
	// at half the pitch (up to 200 Hz) pulls alternate periods apart (a subharmonic), its depth
	// random per throat cycle
	real gmod = 0.0f;
	if (s.growl > 0.0f) {
		const real bdt = BLOCK * isr;
		uint32_t gr = s.grng;
		auto nz = [&gr]() {
			gr = gr * 1664525u + 1013904223u;
			return (real)(int32_t)gr * (1.0f / 2147483648.0f);
		};
		s.gn = clampr(s.gn + 0.0027f * (14.0f * nz() - s.gn), -1.0f, 1.0f);
		s.gd = clampr(s.gd + 0.0045f * (11.0f * nz() - s.gd), -1.0f, 1.0f);
		s.gb *= 1.0f - 6.5f * bdt;
		s.gph += 15.0f * (1.0f + 0.3f * s.gn) * s.gcr * bdt;
		if (s.gph >= 1.0f) {
			s.gph -= (real)(int)s.gph;
			s.gcd = 0.75f + 0.25f * nz();
			s.gcr = 1.0f + 0.3f * nz();
			s.gpz = 0.75f + 0.25f * nz();
			if (nz() > 0.88f) s.gb = 1.0f;
		}
		s.gsph += clampr(0.5f * f0, 25.0f, 200.0f) * bdt;
		s.gsph -= (real)(int)s.gsph;
		s.grng = gr;
		const real gdc = rmin(clampr(1.0f + 0.4f * s.gd, 0.3f, 1.4f) * s.gcd * (1.0f + 0.5f * s.gb), 1.4f);
		const real w = 0.5f + 0.5f * sin_turns(s.gph);
		gmod = -1.1f * gdc * w * w + 0.7f * gdc * s.gpz * sin_turns(s.gsph);
	}
	const real gm_t = 1.0f + q.growl * s.growl * gmod + q.vib_breath * sin_turns(e.vph + 0.13f * (real)v + 0.25f);
	b.gm = s.gm;
	b.dgm = (gm_t - s.gm) * (1.0f / BLOCK);
	s.gm = gm_t;
	// output gain: physics, optionally scaled with breath
	const real psc = clampr(ps, 0.0f, 1.3f);
	real og_t = q.level * ((1.0f - q.dyn_gain) + q.dyn_gain * psc);
	if (life_on) og_t *= l_lv;
	b.og = s.og;
	b.dog = (og_t - s.og) * (1.0f / BLOCK);
	s.og = og_t;
	// noise: turbulence on the drive (lowpass ~2 kHz); audible breath: rises with pres^2 up to
	// the speaking pressure, then settles to a quieter part of the tone; tongue "t" burst
	b.turb = q.turb * (1.0f + 2.0f * sub) + 0.1f * tg;
	b.ct = 0.25f;
	const real nr = clampr(ps * e.inv_p0, 0.0f, 1.0f);
	const real speaking = clampr((ps - clampr(q.speak, 0.02f, 0.6f)) * 4.0f, 0.0f, 1.0f);
	b.bo = 0.02f * (q.breath * nr * nr * (1.0f - 0.6f * speaking) * (1.0f + 2.0f * sub) * (0.5f + 0.5f * psc) + q.tongue_noise * tg * nr);
	if (life_on) b.bo *= l_nz;
	b.body = q.body;
	b.kb = clampr(q.br_breath, 0.0f, 8.0f) * psc * psc;
	b.joff = clampr(q.jet_off, 0.0f, 0.9f);
	b.soff = b.joff * (1.0f - 0.148148148f * b.joff * b.joff);
	b.ja = JET_AMP;
	b.jn = (kind == KIND_JET) ? 4.0f * b.turb : 0.0f;

	const real peak = kind == KIND_CYL ? run_voice<KIND_CYL>(e, v, b) : kind == KIND_CONE ? run_voice<KIND_CONE>(e, v, b) :
	                                                                                        run_voice<KIND_JET>(e, v, b);

	// safety: reset a voice whose state went non-finite or absurd
	if (!(fabsf(s.lp) + fabsf(s.x1) + fabsf(s.pm1) + fabsf(s.dcy) + peak < 1000.0f)) {
		reset_voice(e, v, true);
		s.note_prev = note;
		return;
	}
	if (pres <= 0.0f && peak < 2e-5f) {
		if (++s.quiet > (int)(P * (1.0f / BLOCK)) + 3) s.active = 0;
	} else {
		s.quiet = 0;
	}
}

[[nodiscard]] static inline int active_voices(const Engine& e) {
	int n = 0;
	for (int v = 0; v < e.nvoices; v++) n += e.vs[v].active;
	return n;
}

[[nodiscard]] static inline awva::SvfCoef svf_hz(real hz, real sr, real Q) {
	return awva::svf_from_g(awva::tan0(clampr(hz, 20.0f, 0.4f * sr) * 3.1415927f / sr), Q);
}

// ---------------------------------------------------------------------------------------------
// Live parameters

struct ParamEntry {
	awva::ParamDesc d;
	real Params::*m;
};

static constexpr ParamEntry param_table[] = {
    {{"level", "breath", 0.1f, 4.0f, awva::ParamKind::continuous}, &Params::level},
    {{"speak", "breath", 0.03f, 0.5f, awva::ParamKind::continuous}, &Params::speak},      // pressure where the tone speaks
    {{"dyn", "breath", 0.4f, 1.5f, awva::ParamKind::continuous}, &Params::dyn},           // drive at full pressure (reeds <= 0.94)
    {{"attack", "breath", 0.5f, 60.0f, awva::ParamKind::continuous}, &Params::attack},    // breath smoothing (ms)
    {{"dyn_gain", "breath", 0.0f, 1.0f, awva::ParamKind::continuous}, &Params::dyn_gain}, // loudness also scaled by breath
    {{"tongue", "artic", 0.0f, 1.0f, awva::ParamKind::continuous}, &Params::tongue},      // pressure kick of a fast strike
    {{"tongue_noise", "artic", 0.0f, 2.0f, awva::ParamKind::continuous}, &Params::tongue_noise},
    {{"reed_open", "reed", 0.08f, 0.7f, awva::ParamKind::continuous}, &Params::reed_open}, // embouchure looseness
    {{"reed_freq", "reed", 1500.0f, 8000.0f, awva::ParamKind::continuous}, &Params::reed_freq},
    {{"reed_damp", "reed", 0.2f, 1.5f, awva::ParamKind::continuous}, &Params::reed_damp},
    {{"loss", "bore", 0.9f, 0.995f, awva::ParamKind::continuous}, &Params::loss},
    {{"lattice", "bore", 300.0f, 6000.0f, awva::ParamKind::continuous}, &Params::lattice},  // tone-hole cutoff (Hz)
    {{"lat_breath", "bore", 0.0f, 1.5f, awva::ParamKind::continuous}, &Params::lat_breath}, // octaves brighter per pressure
    {{"apex", "bore", 0.15f, 0.5f, awva::ParamKind::continuous}, &Params::apex},            // cone apex share (lowest note; the default 0.5, the DSP clamps to 0.5)
    {{"apex_lp", "bore", 1.5f, 20.0f, awva::ParamKind::continuous}, &Params::apex_lp},
    {{"jet", "bore", 0.4f, 0.6f, awva::ParamKind::continuous}, &Params::jet},         // flute jet delay (of period)
    {{"jet_off", "bore", 0.0f, 0.8f, awva::ParamKind::continuous}, &Params::jet_off}, // flute jet offset
    {{"body", "tone", 0.0f, 3.0f, awva::ParamKind::continuous}, &Params::body},       // (the flute: 3)
    {{"rad_hp", "tone", 20.0f, 2000.0f, awva::ParamKind::continuous}, &Params::rad_hp},
    {{"bright", "tone", 500.0f, 16000.0f, awva::ParamKind::continuous}, &Params::bright},
    {{"br_breath", "tone", 0.0f, 8.0f, awva::ParamKind::continuous}, &Params::br_breath},
    {{"f1_hz", "tone", 150.0f, 5000.0f, awva::ParamKind::continuous}, &Params::f1_hz},
    {{"f1_db", "tone", -12.0f, 15.0f, awva::ParamKind::continuous}, &Params::f1_db},
    {{"f2_hz", "tone", 300.0f, 8000.0f, awva::ParamKind::continuous}, &Params::f2_hz},
    {{"f2_db", "tone", -12.0f, 15.0f, awva::ParamKind::continuous}, &Params::f2_db},
    {{"f_q", "tone", 0.5f, 6.0f, awva::ParamKind::continuous}, &Params::f_q},
    {{"breath", "noise", 0.0f, 3.0f, awva::ParamKind::continuous}, &Params::breath}, // audible breath
    {{"turb", "noise", 0.0f, 0.15f, awva::ParamKind::continuous}, &Params::turb},    // turbulence on the drive
    {{"noise_hz", "noise", 300.0f, 8000.0f, awva::ParamKind::continuous}, &Params::noise_hz},
    {{"tilt_bright", "expr", 0.0f, 1.0f, awva::ParamKind::continuous}, &Params::tilt_bright},
    {{"tilt_breath", "expr", 0.0f, 1.0f, awva::ParamKind::continuous}, &Params::tilt_breath},
    {{"growl", "expr", 0.0f, 0.8f, awva::ParamKind::continuous}, &Params::growl},
    {{"growl_from", "expr", 0.5f, 1.5f, awva::ParamKind::continuous}, &Params::growl_from},
    {{"vib", "mod", 0.0f, 0.6f, awva::ParamKind::continuous}, &Params::vib}, // automatic vibrato, semitones
    {{"vib_hz", "mod", 2.0f, 8.0f, awva::ParamKind::continuous}, &Params::vib_hz},
    {{"vib_breath", "mod", 0.0f, 0.3f, awva::ParamKind::continuous}, &Params::vib_breath},
    {{"life", "mod", 0.0f, 1.0f, awva::ParamKind::continuous}, &Params::life},         // micro-randomness
    {{"instrument", "bore", 0.0f, 8.0f, awva::ParamKind::steps}, &Params::instrument}, // instruments[]: kind, lowest note, tuning
};
static constexpr int NUM_LIVE_PARAMS = sizeof(param_table) / sizeof(param_table[0]);
static_assert(NUM_LIVE_PARAMS == NPARAMS, "every Params field is exposed once");

static inline void process(Engine& e, real* __restrict__ output) {
	// smooth the live parameters towards the written values (~50 ms time constant: bore and reed
	// edits then glide the pitch of a held note by a few cents instead of stepping it; the glide
	// arrives, awva::glide_param)
	for (const ParamEntry& r : param_table) e.ps.*r.m = awva::glide_param(e.ps.*r.m, e.p.*r.m, 0.012f, r.d);
	const Params& q = e.ps;
	const real sr = e.sr;
	e.kp = 1.0f - pow2(-1442.7f * BLOCK * e.inv_sr / clampr(q.attack, 0.3f, 100.0f));
	e.inv_1mp0 = 1.0f / (1.0f - clampr(q.speak, 0.02f, 0.6f));
	e.inv_p0 = 1.0f / clampr(q.speak, 0.02f, 0.6f);
	e.p_low = (sr * (1.0f / 440.0f)) * pow2((69.0f - e.low_note) * (1.0f / 12.0f));
	const real sh = clampr((e.g.motion.accel_g - 1.15f) * 0.8f, 0.0f, 1.0f);
	e.shake += (sh > e.shake ? 0.2f : 0.02f) * (sh - e.shake);
	if (!(e.shake == e.shake)) e.shake = 0.0f;
	e.vph += clampr(q.vib_hz, 0.1f, 12.0f) * (BLOCK / sr);
	e.vph -= (real)(int)e.vph;

	for (int n = 0; n < BLOCK; n++) e.mix[n] = e.nmix[n] = 0.0f;
	for (int v = 0; v < e.nvoices; v++) process_voice(e, v);

	// shared output stage: radiation highpass, brightness lowpass, formants, breath bus
	const real chp = 1.0f - pow2(-clampr(q.rad_hp, 10.0f, 5000.0f) * (6.2831853f * 1.442695f) / sr);
	const real clp = 1.0f - pow2(-clampr(q.bright, 200.0f, 20000.0f) * (6.2831853f * 1.442695f) / sr);
	const real fq = clampr(q.f_q, 0.3f, 8.0f);
	const awva::SvfCoef c1 = svf_hz(q.f1_hz, sr, fq), c2 = svf_hz(q.f2_hz, sr, fq);
	const real g1 = (db2lin(clampr(q.f1_db, -24.0f, 24.0f)) - 1.0f) * c1.k;
	const real g2 = (db2lin(clampr(q.f2_db, -24.0f, 24.0f)) - 1.0f) * c2.k;
	const awva::SvfCoef cn = svf_hz(q.noise_hz, sr, 0.7f);
	real hp = e.hp_s, lpo = e.lp_s, a1 = e.f1s.ic1, a2 = e.f1s.ic2, b1 = e.f2s.ic1, b2 = e.f2s.ic2;
	real n1 = e.nbs.ic1, n2 = e.nbs.ic2;
	real s1 = e.dc1, xp = e.dcx;
	for (int n = 0; n < BLOCK; n++) {
		real x = e.mix[n];
		x = fabsf(x) < 1e4f ? x : 0.0f; // also catches NaN
		hp += chp * (x - hp);
		const real xr = x - hp;  // radiation highpass
		lpo += clp * (xr - lpo); // brightness lowpass
		real v1, v2, w1, w2, u1, u2;
		SVF_TICK(lpo, a1, a2, c1, v1, v2);
		SVF_TICK(lpo, b1, b2, c2, w1, w2);
		real nb = e.nmix[n];
		nb = fabsf(nb) < 1e4f ? nb : 0.0f;
		SVF_TICK(nb, n1, n2, cn, u1, u2);
		(void)v2;
		(void)w2;
		(void)u2;
		const real y = lpo + g1 * v1 + g2 * w1 + cn.k * u1;
		// DC blocker (~10 Hz) and soft limit above 4 (asymptote 8)
		s1 = 0.99871f * s1 + y - xp;
		xp = y;
		const real a = fabsf(s1);
		const real o = a > 4.0f ? 4.0f + (a - 4.0f) / (1.0f + 0.25f * (a - 4.0f)) : a;
		output[n] = clampr(s1 < 0.0f ? -o : o, -8.0f, 8.0f);
	}
	// NaN guards; exact 0 when silent
	if (!(fabsf(hp) + fabsf(lpo) + fabsf(a1) + fabsf(a2) + fabsf(b1) + fabsf(b2) + fabsf(n1) + fabsf(n2) + fabsf(s1) < 1e6f)) {
		hp = lpo = a1 = a2 = b1 = b2 = n1 = n2 = s1 = xp = 0.0f;
	}
	if (fabsf(hp) + fabsf(lpo) + fabsf(a1) + fabsf(a2) + fabsf(b1) + fabsf(b2) + fabsf(n1) + fabsf(n2) + fabsf(s1) + fabsf(xp) < 1e-9f) {
		hp = lpo = a1 = a2 = b1 = b2 = n1 = n2 = s1 = xp = 0.0f;
	}
	e.hp_s = hp;
	e.lp_s = lpo;
	e.f1s = {a1, a2};
	e.f2s = {b1, b2};
	e.nbs = {n1, n2};
	e.dc1 = s1;
	e.dcx = xp;
}


} // namespace wind

namespace awva {
template <> struct EngineParams<wind::Engine> {
	static constexpr int count = wind::NUM_LIVE_PARAMS;
	static constexpr const ParamDesc* desc(int i) { return (i >= 0 && i < count) ? &wind::param_table[i].d : nullptr; }
	static float* ptr(wind::Engine& e, int i) { return (i >= 0 && i < count) ? &(e.p.*(wind::param_table[i].m)) : nullptr; }
	static void changed(wind::Engine& e, int i) { // the DSP follows e.p through its smoothed copy
		if (i >= 0 && i < count && wind::param_table[i].m == &wind::Params::instrument) wind::instrument_changed(e);
	}
};
} // namespace awva


// ---------------------------------------------------------------------------------------------
// The factory presets: presets/wind/NN-<name>.preset.json, the files themselves (presets/wind.inc,
// tools/presets_inc.cmake), each read at compile time; a file that isn't every parameter in range
// doesn't compile (awva/preset.h factory).

namespace wind {
#include "presets/wind.inc"
static_assert(NUM_FACTORY_PRESETS == NUM_PRESETS, "presets/wind/: a file per preset");

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
} // namespace wind

#endif // VOICE_WIND_H_
