// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The sound engine contract (docs/README.md): what every voice engine and every effect offers its
// host, an embedded device's synth thread or a program on the PC (sdk/: the renderer, the tests, a
// plugin). Namespace awva. No HAL, no OS, no heap, no globals:
// an engine is a plain-data object plus the memory regions its host gives it.
//
//   VoiceEngine   a polyphonic sound engine: per-voice controls in, a block of audio out
//   Effect        an audio effect: a block of audio in, a block out (fx.h, the master effects)
//
// Engines are written in the shape every voice_<name>.h has (namespace <name>: struct Engine,
// init, process, active_voices, the optional presets and live parameters); adapter.h turns that
// shape into a VoiceEngine, with a traits struct per engine in engines/<name>.h that declares
// what the engine reads, what it costs and what memory it needs.
#ifndef AWVA_ENGINE_H_
#define AWVA_ENGINE_H_

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "controls.h"
#include "params.h"

// an empty member takes no room ([[no_unique_address]]; on Windows the MSVC ABI's spelling)
#if defined(_MSC_VER)
#if __has_cpp_attribute(msvc::no_unique_address)
#define AWVA_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define AWVA_NO_UNIQUE_ADDRESS
#endif
#else
#define AWVA_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

namespace awva {

// (code that says dsp:: inside awva:: finds the contract)
namespace dsp = ::awva;

inline constexpr int BLOCK = strisy::BLOCK; // samples per process() call

using strisy::ParamDesc;
using strisy::ParamKind;

// ---------------------------------------------------------------------------------------------
// What an engine declares about itself

// The inputs an engine reads (EngineInfo::inputs). An input it doesn't declare makes no
// difference to its output (the contract tests check this). The pitch and the gate are always read.
enum Input : uint32_t {
	in_pressure = 1u << 0,      // NoteControls::pressure
	in_pressure_rate = 1u << 1, // NoteControls::pressure_rate
	in_timbre = 1u << 2,        // NoteControls::timbre
	in_velocity = 1u << 3,      // NoteControls::velocity
	in_release = 1u << 4,       // NoteControls::release
	in_sustain = 1u << 5,       // InstrumentControls::sustain
	in_accel = 1u << 6,         // InstrumentControls::motion.accel_g
	in_spin = 1u << 7,          // InstrumentControls::motion.spin
};

// Where a host puts an engine's memory. The engine object itself is always Placement::tcm.
enum class Placement : uint8_t {
	tcm,   // tightly coupled, zero wait states: the hot state (e.g. a Cortex-M7's DTCM, 128 KB)
	fast,  // FastMemory: large, touched every sample (e.g. AHB SRAM, 256 KB)
	large, // LargeMemory: large, any RAM (e.g. AXI SRAM, 512 KB)
};

// What running the engine costs (hints for hosts; budgets.max_voices is a hard limit)
struct Budget {
	int max_voices;           // voices it can run
	uint32_t instr_per_block; // worst preset, Cortex-M7 instructions per block counted under QEMU
	                          // (10 voices, the default performance); a 480 MHz M7 at 48828 Hz has
	                          // 480 MHz / 48828 Hz * 32 = 314k cycles per block
	float tail_seconds;       // after the last release it is silent and idle within this time
};

// a region an engine doesn't need
struct NoMemory {};

// The hero parameters: the live parameters that shape the sound most, by name, in display order
// (an app's main knobs; unused slots nullptr). An engine has its own list and may override it for
// presets whose character lives elsewhere.
inline constexpr int MAX_HEROES = 8;
struct Heroes {
	const char* name[MAX_HEROES];
};
struct PresetHeroes {
	const char* preset; // the preset's name (preset_name)
	Heroes heroes;
};

// What the conformance checks allow an engine (docs/conformance.md), each with a default, so an
// engine that says nothing gets the whole suite at its defaults. Its traits may set
//   static constexpr Conformance conformance = {.tuning = {.cents = 8.0f}};
// the tuning of single presets, by name:
//   static constexpr PresetTuning preset_tuning[] = {{"bell", {.cents = 2.0f, .partial = 1.0f, .why = "inharmonic: the prime is the note"}}};
// and looser pitch bounds at single sample rates (known faults, with their reasons):
//   static constexpr RateTolerance rate_tolerances[] = {{192000, 21.5f, false, "..."}};
struct Tuning {
	float cents = 10.0f;             // the sustained pitch against its note's (12-TET, A4 = 440 Hz), or an octave of it
	float low = 48.0f, high = 84.0f; // the notes it is checked on (MIDI numbers, inclusive)
	// 0: a harmonic tone, its period is the note's. Else an inharmonic one (a bell, a glass): no
	// period to measure, so the partial at this ratio of the note's frequency is, as a spectral peak
	float partial = 0.0f;
	const char* why = nullptr; // why a preset is checked otherwise than by the defaults
};
struct PresetTuning {
	const char* preset; // the preset's name (preset_name)
	Tuning tuning;
};
// a sample rate at which an engine's pitch holds a looser bound than rate_cents: a known fault,
// measured, with its reason (fixing it removes the entry; a result worse than it still fails)
struct RateTolerance {
	int rate;
	float cents;        // the pitch within this many cents of 48 kHz there
	bool may_not_sound; // it may be silent or not periodic at this rate (no pitch to compare)
	const char* why;
};
struct Conformance {
	Tuning tuning = {};
	float rate_cents = 3.0f; // the sustained pitch at every rate from min_rate to max_rate against 48 kHz
	// the peak with extreme but legal parameter writes (the fuzz, the presets going round, the
	// soak): only blow-ups fail. An engine louder there declares its measured peak with a small
	// margin and why; a peak above it still fails
	float fuzz_peak = 64.0f;
	const char* fuzz_peak_why = nullptr;
	const PresetTuning* preset_tuning = nullptr;
	int num_preset_tuning = 0;
	const RateTolerance* rate_tolerances = nullptr;
	int num_rate_tolerances = 0;
};

// the pitch bound at a rate (its tolerance, else rate_cents) and the tolerance if there is one
[[nodiscard]] constexpr const RateTolerance* rate_tolerance(const Conformance& c, int rate) {
	for (int i = 0; i < c.num_rate_tolerances; i++)
		if (c.rate_tolerances[i].rate == rate) return &c.rate_tolerances[i];
	return nullptr;
}

// the tuning a preset is checked to (its own, else the engine's; nullptr: the engine's)
[[nodiscard]] constexpr const Tuning& tuning_of(const Conformance& c, const char* preset) {
	for (int i = 0; preset && i < c.num_preset_tuning; i++) {
		const char *a = c.preset_tuning[i].preset, *b = preset;
		while (*a && *a == *b) a++, b++;
		if (*a == *b) return c.preset_tuning[i].tuning;
	}
	return c.tuning;
}

struct EngineInfo {
	const char* name;    // the engine's short name, e.g. "va"
	const char* summary; // one line for people
	int channels;        // 1: mono, 2: stereo out
	uint32_t inputs;     // Input bits
	Budget budget;
	size_t state_bytes;                // the engine object (Placement::tcm)
	size_t fast_bytes;                 // its FastMemory (0: none)
	size_t large_bytes;                // its LargeMemory (0: none)
	int num_presets;                   // 0: no presets
	int num_params;                    // live parameters
	Heroes heroes;                     // the engine's hero parameters
	const PresetHeroes* preset_heroes; // presets with their own (num_preset_heroes of them)
	int num_preset_heroes;
	const char* title = nullptr;  // a title for people ("Electric Piano"); nullptr: name
	Conformance conformance = {}; // what the conformance checks allow it (the defaults unless its traits say)
	int min_rate = 11025;         // the lowest sample rate it is made for (a component refuses lower ones)
};

// the hero parameters of a preset, by its name (the engine's own if it has none, or for nullptr)
[[nodiscard]] constexpr const Heroes& heroes_of(const EngineInfo& in, const char* preset) {
	for (int i = 0; preset && i < in.num_preset_heroes; i++) {
		const char *a = in.preset_heroes[i].preset, *b = preset;
		while (*a && *a == *b) a++, b++;
		if (*a == *b) return in.preset_heroes[i].heroes;
	}
	return in.heroes;
}

struct EffectInfo {
	const char* name;
	const char* summary;
	int inputs, outputs; // channels
	uint32_t instr_per_block;
	size_t state_bytes, fast_bytes, large_bytes;
	int num_params;
	Heroes heroes;
};

// The memory of an engine as one object, for hosts that don't place the regions separately (the
// PC); an embedded host may put FastMemory and LargeMemory in different RAMs (engine_set.h).
template <class E> struct Memory {
	AWVA_NO_UNIQUE_ADDRESS typename E::FastMemory fast;
	AWVA_NO_UNIQUE_ADDRESS typename E::LargeMemory large;
};

// plain data: zero-filled storage is a valid engine before init(); nothing runs at startup
template <class T>
concept PlainData = std::is_trivially_default_constructible_v<T> && std::is_trivially_destructible_v<T>;

// ---------------------------------------------------------------------------------------------
// The contracts

// A polyphonic sound engine.
//   init(fast, large, sample_rate, voices)   start from silence; the regions stay the engine's
//                                             until the next init (voices <= budget.max_voices)
//   reset()             back to silence, keeping the preset and the live parameter values
//   process(vc, g, out) one block: vc[voices()] controls, g the globals, out[0..channels) BLOCK
//                       samples each (full scale about +-4: hosts typically play it x0.25)
//   active_voices()     voices sounding now; voices() the count init() set
//   presets: num_presets() (static), preset_name(i), set_preset(i) (any time, between blocks),
//            preset() (-1 without presets)
//   live parameters: param_desc(i) (nullptr past the last), param(i) the live value,
//            set_param(i, v) a write by write_param()'s rule (false: not written)
template <class E>
concept VoiceEngine = PlainData<E> && PlainData<typename E::FastMemory> && PlainData<typename E::LargeMemory> &&
    requires(E& e, const E& ce, typename E::FastMemory& f, typename E::LargeMemory& l,
        const NoteControls* vc, const InstrumentControls& g, float* const* out, int i, float v) {
	    { E::info } -> std::convertible_to<const EngineInfo&>;
	    { e.init(f, l, i, i) } -> std::same_as<void>;
	    { e.reset() } -> std::same_as<void>;
	    { e.process(vc, g, out) } -> std::same_as<void>;
	    { ce.active_voices() } -> std::same_as<int>;
	    { ce.voices() } -> std::same_as<int>;
	    { E::preset_name(i) } -> std::same_as<const char*>;
	    { e.set_preset(i) } -> std::same_as<void>;
	    { ce.preset() } -> std::same_as<int>;
	    { E::param_desc(i) } -> std::same_as<const ParamDesc*>;
	    { e.param(i) } -> std::same_as<float*>;
	    { e.set_param(i, v) } -> std::same_as<bool>;
    };

// An engine whose init() writes every byte of its FastMemory and LargeMemory declares
//   static constexpr bool init_writes_memory = true;
// Then a host that hands it memory another engine used (EngineSet's shared arenas) needn't zero it
// first: the engine starts from the same state either way. sdk/tests (engine_set) proves it for
// each engine that declares it: init over any data gives the same bytes, and the same output, as
// over zeros.
template <class E> inline constexpr bool init_writes_memory = requires { requires E::init_writes_memory; };

// An audio effect: init(fast, large, sample_rate), reset(), process(in, out) with
// in[0..inputs) and out[0..outputs) BLOCK samples each, live parameters as a VoiceEngine.
template <class F>
concept Effect = PlainData<F> && PlainData<typename F::FastMemory> && PlainData<typename F::LargeMemory> &&
    requires(F& f, typename F::FastMemory& fm, typename F::LargeMemory& lm, const float* const* in,
        float* const* out, int i, float v) {
	    { F::info } -> std::convertible_to<const EffectInfo&>;
	    { f.init(fm, lm, i) } -> std::same_as<void>;
	    { f.reset() } -> std::same_as<void>;
	    { f.process(in, out) } -> std::same_as<void>;
	    { F::param_desc(i) } -> std::same_as<const ParamDesc*>;
	    { f.param(i) } -> std::same_as<float*>;
	    { f.set_param(i, v) } -> std::same_as<bool>;
    };

} // namespace awva

#endif // AWVA_ENGINE_H_
