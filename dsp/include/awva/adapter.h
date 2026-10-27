// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// Adapter<Traits>: a voice engine written in the voice_<name>.h shape as a awva::VoiceEngine,
// and EffectAdapter<Traits> for an effect in the fx.h shape. The engine header stays as it is; a
// traits struct (engines/<name>.h) names its functions and declares what the engine reads, costs
// and needs:
//
//   struct traits::<name> {
//       AWVA_ENGINE_FUNCTIONS(<name>)  // Engine, init(e, sr, voices), process(e, out), active_voices(e)
//       AWVA_ENGINE_PRESETS(<name>)    // (engines with presets) num_presets, preset_name, set_preset
//       using StreamMemory = ...;      // the engine's regions (engine.h Region), or NoMemory; Engine has
//       using BulkMemory = ...;        //   a pointer to each it uses: StreamMemory* stream, BulkMemory* bulk
//       static constexpr const char* name = "<name>", *summary = "...";
//       static constexpr const char* short_name = "..."; // 1 to 6 characters (required)
//       static constexpr uint32_t led = 0xrrggbb;   // its colour (required, not 0)
//       static constexpr uint32_t inputs = in_pressure | ...;
//       static constexpr int max_voices = ...;      // the voices it can run
//       static constexpr float tail_seconds = ...;  // silent and idle this long after the last release
//       static constexpr Cost cost = {...};         // instructions per voice and frame (COST_REFERENCE)
//       static constexpr Heroes heroes = {{"cutoff", ...}};                        // optional, up to 8
//       static constexpr PresetHeroes preset_heroes[] = {{"pad", {{...}}}, ...};  // optional
//       static constexpr Conformance conformance = {...};          // optional: the checks' tolerances
//       static constexpr PresetTuning preset_tuning[] = {{"flute", {...}}};  // optional (engine.h)
//       static constexpr RateTolerance rate_tolerances[] = {{192000, ...}};  // optional (engine.h)
//       static constexpr int min_rate = 22050;     // optional: the lowest rate it is made for (11025)
//   };
//
// The engine's struct Engine has `awva::InstrumentControls g; awva::NoteControls vc[MAX_VOICES];
// int nvoices;`, and, with presets, `int preset`. Live parameters come from awva::EngineParams<Engine>.
#ifndef AWVA_ADAPTER_H_
#define AWVA_ADAPTER_H_

#include <cstddef>
#include <type_traits>

#include "engine.h"

#define AWVA_ENGINE_FUNCTIONS(ns) \
	using Engine = ::ns::Engine; \
	static_assert(std::is_same_v<::ns::real, float>, "the contract's samples are float"); \
	static void init(Engine& e, int sample_rate, int voices) { ::ns::init(e, sample_rate, voices); } \
	static void process(Engine& e, float* out) { ::ns::process(e, out); } \
	[[nodiscard]] static int active_voices(const Engine& e) { return ::ns::active_voices(e); }

#define AWVA_ENGINE_PRESETS(ns) \
	static constexpr int num_presets = ::ns::NUM_PRESETS; \
	[[nodiscard]] static constexpr const char* preset_name(int i) { return ::ns::preset_name(i); } \
	static void set_preset(Engine& e, int i) { ::ns::set_preset(e, i); } \
	static void params_loaded(Engine& e, int i) { ::ns::params_loaded(e, i); } \
	[[nodiscard]] static const char* factory_text(int i) { return ::ns::factory_json[i]; }

namespace awva {

namespace detail {
constexpr int length(const char* s) {
	int n = 0;
	while (s[n]) n++;
	return n;
}

// the traits' title, if they have one
template <class T> constexpr const char* title() {
	if constexpr (requires { T::title; }) return T::title;
	return nullptr;
}
// the traits' min_rate, if they have one: an engine unstable or out of tune below some rate says so
template <class T> constexpr int min_rate() {
	if constexpr (requires { T::min_rate; }) return T::min_rate;
	return EngineInfo{}.min_rate;
}
// the traits' mono family, if they say (finger otherwise: a fingered wind or brass)
template <class T> constexpr MonoFamily mono() {
	if constexpr (requires { T::mono; }) return T::mono;
	return MonoFamily::finger;
}

template <class Native, class StreamMemory, class BulkMemory>
inline void attach(Native& e, StreamMemory& f, BulkMemory& l) {
	if constexpr (std::is_same_v<StreamMemory, NoMemory>) (void)f;
	else e.stream = &f;
	if constexpr (std::is_same_v<BulkMemory, NoMemory>) (void)l;
	else e.bulk = &l;
}

// the live parameters of a native engine, saved and written back (reset() keeps them)
template <class Native> struct SavedParams {
	using P = awva::EngineParams<Native>;
	float v[P::count > 0 ? P::count : 1];
	void save(Native& e) {
		for (int i = 0; i < P::count; i++) v[i] = *P::ptr(e, i);
	}
	void restore(Native& e) const {
		for (int i = 0; i < P::count; i++) {
			*P::ptr(e, i) = v[i];
			P::changed(e, i);
		}
	}
};

// the optional hero declarations of a traits struct
template <class T> constexpr Heroes heroes() {
	if constexpr (requires { T::heroes; }) return T::heroes;
	else return {};
}
template <class T> constexpr const PresetHeroes* preset_heroes() {
	if constexpr (requires { T::preset_heroes; }) return T::preset_heroes;
	else return nullptr;
}
template <class T> constexpr int num_preset_heroes() {
	if constexpr (requires { T::preset_heroes; }) return static_cast<int>(std::extent_v<decltype(T::preset_heroes)>);
	else return 0;
}

// the optional conformance declarations of a traits struct (engine.h: Conformance, PresetTuning)
template <class T> constexpr Conformance conformance() {
	Conformance c{};
	if constexpr (requires { T::conformance; }) c = T::conformance;
	if constexpr (requires { T::preset_tuning; }) {
		c.preset_tuning = T::preset_tuning;
		c.num_preset_tuning = static_cast<int>(std::extent_v<decltype(T::preset_tuning)>);
	}
	if constexpr (requires { T::rate_tolerances; }) {
		c.rate_tolerances = T::rate_tolerances;
		c.num_rate_tolerances = static_cast<int>(std::extent_v<decltype(T::rate_tolerances)>);
	}
	return c;
}

template <class T> constexpr int num_presets() {
	if constexpr (requires { T::num_presets; }) return T::num_presets;
	else return 0;
}

// the size of an adapter: the native engine and the sample rate (EngineInfo::regions.hot)
template <class Native> constexpr size_t state_bytes() {
	constexpr size_t a = alignof(Native) > alignof(int) ? alignof(Native) : alignof(int);
	return (sizeof(Native) + sizeof(int) + a - 1) / a * a;
}

// the regions of an adapter of Native with its traits' memory: sizes and alignment
template <class Native, class Stream, class Bulk> constexpr Regions regions() {
	constexpr size_t h = alignof(Native) > alignof(int) ? alignof(Native) : alignof(int);
	constexpr size_t a = h > alignof(Stream) ? h : alignof(Stream);
	return {state_bytes<Native>(), std::is_empty_v<Stream> ? 0 : sizeof(Stream), std::is_empty_v<Bulk> ? 0 : sizeof(Bulk),
	    a > alignof(Bulk) ? a : alignof(Bulk)};
}

} // namespace detail

template <class T> struct Adapter {
	using Traits = T;
	using Native = typename T::Engine;
	using StreamMemory = typename T::StreamMemory;
	using BulkMemory = typename T::BulkMemory;
	using Params = awva::EngineParams<Native>;
	static constexpr bool has_presets = requires { T::num_presets; };
	// the traits' init_writes_memory (engine.h), false if they don't say
	static constexpr bool init_writes_memory = awva::init_writes_memory<T>;
	static_assert(PlainData<Native>, "an engine is plain data (it lives in zero-filled hot memory, no constructors run)");


	Native e;
	int sample_rate;

	void init(StreamMemory& f, BulkMemory& l, int sr, int voices) {
		detail::attach(e, f, l);
		T::init(e, sr, voices);
		sample_rate = sr;
	}

	void reset() {
		detail::SavedParams<Native> saved;
		saved.save(e);
		const int p = preset();
		T::init(e, sample_rate, e.nvoices);
		if constexpr (has_presets) T::set_preset(e, p);
		saved.restore(e);
	}

	void process(const NoteControls* vc, const InstrumentControls& g, float* const* out) {
		e.g = g;
		for (int v = 0; v < e.nvoices; v++) e.vc[v] = vc[v];
		T::process(e, out[0]);
		for (int n = 0; n < BLOCK; n++) out[0][n] *= OUTPUT_SCALE; // (nominal +-1: engine.h)
	}

	[[nodiscard]] int active_voices() const { return T::active_voices(e); }
	[[nodiscard]] int voices() const { return e.nvoices; }

	[[nodiscard]] static constexpr int num_presets() { return detail::num_presets<T>(); }
	[[nodiscard]] static constexpr const char* preset_name(int i) {
		if constexpr (has_presets) return i >= 0 && i < T::num_presets ? T::preset_name(i) : nullptr;
		else return (void)i, nullptr;
	}
	// factory preset i's file (presets/<engine>/NN-<name>.preset.json), its text as compiled in
	// (nullptr: no such preset). A host that calls it links the texts in (about 1.5 KB a preset).
	[[nodiscard]] static const char* factory_text(int i) {
		if constexpr (has_presets) return i >= 0 && i < T::num_presets ? T::factory_text(i) : nullptr;
		else return (void)i, nullptr;
	}
	void set_preset(int i) {
		if constexpr (has_presets) T::set_preset(e, i);
		else (void)i;
	}
	[[nodiscard]] int preset() const {
		if constexpr (has_presets) return e.preset;
		else return -1;
	}

	// every parameter's value at once (values[num_params], in their order), as a preset does: no
	// glide from the last values where the engine glides a preset in, the preset index (-1: a
	// preset by name, preset.h). The values are taken as they are (the caller clamps them).
	void load_params(const float* values, int index) {
		for (int k = 0; k < Params::count; k++) *Params::ptr(e, k) = values[k];
		if constexpr (has_presets) {
			T::params_loaded(e, index);
		} else {
			(void)index;
			for (int k = 0; k < Params::count; k++) Params::changed(e, k);
		}
	}

	static constexpr int num_params = Params::count;
	[[nodiscard]] static constexpr const ParamDesc* param_desc(int i) { return Params::desc(i); }
	[[nodiscard]] float* param(int i) { return Params::ptr(e, i); }
	[[nodiscard]] bool set_param(int i, float v) {
		if (!awva::write_param(Params::desc(i), Params::ptr(e, i), v)) return false;
		Params::changed(e, i);
		return true;
	}
	static constexpr EngineInfo info = {.name = T::name,
	    .summary = T::summary,
	    .channels = 1,
	    .inputs = T::inputs,
	    .max_voices = T::max_voices,
	    .tail_seconds = T::tail_seconds,
	    .cost = T::cost,
	    .regions = detail::regions<Native, typename T::StreamMemory, typename T::BulkMemory>(),
	    .num_presets = detail::num_presets<T>(),
	    .num_params = awva::EngineParams<typename T::Engine>::count,
	    .heroes = detail::heroes<T>(),
	    .preset_heroes = detail::preset_heroes<T>(),
	    .num_preset_heroes = detail::num_preset_heroes<T>(),
	    .title = detail::title<T>(),
	    .conformance = detail::conformance<T>(),
	    .min_rate = detail::min_rate<T>(),
	    .short_name = T::short_name,
	    .led = T::led,
	    .mono = detail::mono<T>()};
	static_assert(detail::length(T::short_name) >= 1 && detail::length(T::short_name) <= 6, "an engine declares a short name of 1 to 6 characters (traits short_name)");
	static_assert(T::led != 0 && T::led <= 0xffffffu, "an engine declares its colour (traits led, 0xrrggbb, not 0)");
};


// An effect in the fx.h shape: struct traits::<name> {
//     using Engine; using StreamMemory; using BulkMemory;
//     static constexpr const char* name, *summary; static constexpr int inputs, outputs;
//     static constexpr Cost cost;   // instructions per frame (COST_REFERENCE)
//     static void init(Engine&, int sample_rate); static void process(Engine&, const float* const* in, float* const* out);
// };
template <class T> struct EffectAdapter {
	using Traits = T;
	using Native = typename T::Engine;
	using StreamMemory = typename T::StreamMemory;
	using BulkMemory = typename T::BulkMemory;
	using Params = awva::EngineParams<Native>;
	static_assert(PlainData<Native>, "an effect is plain data");


	Native e;
	int sample_rate;

	void init(StreamMemory& f, BulkMemory& l, int sr) {
		detail::attach(e, f, l);
		T::init(e, sr);
		sample_rate = sr;
	}
	void reset() {
		detail::SavedParams<Native> saved;
		saved.save(e);
		T::init(e, sample_rate);
		saved.restore(e);
	}
	// in and out at the output's scale (nominal +-1); inside, the effect runs at the engines' own
	// scale (its drive, its limiter and its delay line's quantization are set for it): x 1 / OUTPUT_SCALE
	// in, x OUTPUT_SCALE out, both exact
	void process(const float* const* in, float* const* out) {
		float buf[T::inputs][BLOCK];
		const float* ins[T::inputs];
		for (int c = 0; c < T::inputs; c++) {
			for (int n = 0; n < BLOCK; n++) buf[c][n] = in[c][n] * (1.0f / OUTPUT_SCALE);
			ins[c] = buf[c];
		}
		T::process(e, ins, out);
		for (int c = 0; c < T::outputs; c++)
			for (int n = 0; n < BLOCK; n++) out[c][n] *= OUTPUT_SCALE;
	}

	static constexpr int num_params = Params::count;
	[[nodiscard]] static const ParamDesc* param_desc(int i) { return Params::desc(i); }
	[[nodiscard]] float* param(int i) { return Params::ptr(e, i); }
	[[nodiscard]] bool set_param(int i, float v) {
		if (!awva::write_param(Params::desc(i), Params::ptr(e, i), v)) return false;
		Params::changed(e, i);
		return true;
	}
	static constexpr EffectInfo info = {T::name, T::summary, T::inputs, T::outputs, T::cost,
	    detail::regions<Native, typename T::StreamMemory, typename T::BulkMemory>(), awva::EngineParams<typename T::Engine>::count,
	    detail::heroes<T>()};
};


} // namespace awva

#endif // AWVA_ADAPTER_H_
