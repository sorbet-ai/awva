// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
//
// nsc::component<E>(): any awva::VoiceEngine as an nsc_component (nsc.h), unchanged. The
// state region holds the engine object and the component's MIDI side (awva_sdk/midi_voices.h:
// the up-converter for MIDI 1.0 in UMP, the decoder, the voice allocator, mono legato) and its
// event queue; the fast and large regions are the engine's. process_voices() is the engine's own
// process() on the host's controls, so a host that gives the board's controls gets the board's
// samples, bit for bit.
#ifndef NSC_SHIM_H_
#define NSC_SHIM_H_

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "nsc/nsc.h"
#include "awva/engine.h"
#include "awva_sdk/midi_voices.h"

#ifndef NSC_VERSION
#define NSC_VERSION "unknown"
#endif

namespace nsc {

namespace dsp = awva;

static_assert(sizeof(nsc_voice) == sizeof(dsp::VoiceControls) && offsetof(nsc_voice, release) == offsetof(dsp::VoiceControls, release),
    "nsc_voice is dsp::VoiceControls");
static_assert(sizeof(nsc_globals) == sizeof(dsp::GlobalControls), "nsc_globals is dsp::GlobalControls");

[[nodiscard]] constexpr uint32_t fnv1a(const char* s, uint32_t h = 2166136261u) {
	for (; *s; s++) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
	return h;
}
[[nodiscard]] inline uint32_t fnv1a_word(uint32_t h, uint32_t w) {
	for (int b = 0; b < 4; b++) h = (h ^ ((w >> (8 * b)) & 0xffu)) * 16777619u;
	return h;
}

template <dsp::VoiceEngine E> struct Shim {
	static constexpr int QUEUE = 1024; // events waiting for their 2 ms step
	struct Queued {
		uint64_t frame;
		uint32_t words[4];
	};
	struct State {
		awva::sdk::MidiVoices midi;
		dsp::VoiceControls vc[16];
		dsp::GlobalControls g;
		Queued queue[QUEUE];
		int queued;
		uint64_t frame; // frames rendered by process()
		uint64_t steps; // 2 ms steps taken
		int32_t sample_rate, voices;
		E engine;
		explicit State(int nv) :
		    midi(nv), vc(), g(), queue(), queued(0), frame(0), steps(0), sample_rate(0), voices(0), engine() {}
	};
	static_assert(alignof(State) <= 64, "the regions are 64-byte aligned");
	static_assert(E::info.budget.max_voices <= 16, "16 voices at most");

	static State& st(void* p) { return *static_cast<State*>(p); }
	static const State& st(const void* p) { return *static_cast<const State*>(p); }

	static const nsc_param* params() {
		static nsc_param p[E::info.num_params > 0 ? E::info.num_params : 1];
		static const bool done = [] {
			for (int i = 0; i < E::info.num_params; i++) {
				const dsp::ParamDesc* d = E::param_desc(i);
				p[i] = {fnv1a(d->name), d->name, d->group, d->min, d->max, static_cast<int32_t>(d->kind)};
			}
			return true;
		}();
		(void)done;
		return p;
	}
	static const nsc_preset* presets() {
		static nsc_preset p[E::info.num_presets > 0 ? E::info.num_presets : 1];
		static const bool done = [] {
			for (int i = 0; i < E::info.num_presets; i++) {
				p[i].name = E::preset_name(i);
				const dsp::Heroes& h = dsp::heroes_of(E::info, p[i].name);
				for (int k = 0; k < NSC_MAX_HEROES; k++) p[i].heroes[k] = h.name[k];
			}
			return true;
		}();
		(void)done;
		return p;
	}
	static uint32_t schema() {
		uint32_t h = 2166136261u;
		const nsc_param* p = params();
		for (int i = 0; i < E::info.num_params; i++) {
			h = fnv1a_word(h, p[i].id);
			h = fnv1a_word(h, std::bit_cast<uint32_t>(p[i].min));
			h = fnv1a_word(h, std::bit_cast<uint32_t>(p[i].max));
			h = fnv1a_word(h, static_cast<uint32_t>(p[i].kind));
		}
		return h;
	}
	static const char* uid() {
		static char u[96];
		static const bool done = [] {
			const char* p = "com.sorbet-ai.awva.";
			size_t n = 0;
			for (; p[n]; n++) u[n] = p[n];
			for (const char* s = E::info.name; *s && n + 1 < sizeof u; s++) u[n++] = *s;
			u[n] = 0;
			return true;
		}();
		(void)done;
		return u;
	}
	static const nsc_desc* desc() {
		static nsc_desc d;
		static const bool done = [] {
			const dsp::EngineInfo& in = E::info;
			d.magic = NSC_MAGIC;
			d.abi = NSC_ABI;
			d.id = in.name;
			d.uid = uid();
			d.name = in.name;
			d.summary = in.summary;
			d.version = NSC_VERSION;
			d.schema_hash = schema();
			d.inputs = in.inputs;
			d.channels = in.channels;
			d.max_voices = in.budget.max_voices;
			d.instr_per_block = in.budget.instr_per_block;
			d.tail_seconds = in.budget.tail_seconds;
			d.state_bytes = static_cast<uint32_t>(sizeof(State));
			d.fast_bytes = static_cast<uint32_t>(in.fast_bytes); // (0: none; an empty region is still passed)
			d.large_bytes = static_cast<uint32_t>(in.large_bytes);
			d.stack_bytes = 0;
			d.num_params = in.num_params;
			d.num_presets = in.num_presets;
			d.params = params();
			d.presets = presets();
			for (int k = 0; k < NSC_MAX_HEROES; k++) d.heroes[k] = in.heroes.name[k];
			return true;
		}();
		(void)done;
		return &d;
	}

	static void init(void* state, void* fast, void* large, int32_t sr, int32_t nv) {
		State* s = new (state) State(nv);
		for (auto& c : s->vc) c = dsp::voice_at_rest;
		s->g = dsp::globals_at_rest;
		s->sample_rate = sr;
		s->voices = nv;
		s->engine.init(*static_cast<typename E::FastMemory*>(fast), *static_cast<typename E::LargeMemory*>(large), sr, nv);
	}
	static void reset(void* state) {
		State& s = st(state);
		const bool legato = s.midi.legato();
		new (&s.midi) awva::sdk::MidiVoices(s.voices);
		s.midi.set_legato(legato);
		for (auto& c : s.vc) c = dsp::voice_at_rest;
		s.g = dsp::globals_at_rest;
		s.queued = 0;
		s.frame = s.steps = 0;
		s.engine.reset();
	}
	static int32_t mode(void* state, int32_t which, int32_t value) {
		if (which != NSC_MODE_LEGATO) return 0;
		st(state).midi.set_legato(value != 0);
		return 1;
	}
	[[nodiscard]] static constexpr uint64_t step_frame(uint64_t k, int32_t sr) {
		return (k * static_cast<uint64_t>(sr) + 499u) / 500u;
	}
	static int words_of(uint32_t w0) {
		constexpr int n[16] = {1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4};
		return n[w0 >> 28];
	}
	static void feed(State& s, const Queued& q) {
		const uint32_t w0 = q.words[0];
		if ((w0 >> 28) == 2) {
			const uint8_t m[3] = {static_cast<uint8_t>(w0 >> 16), static_cast<uint8_t>((w0 >> 8) & 0x7f), static_cast<uint8_t>(w0 & 0x7f)};
			const int len = (m[0] & 0xf0) == 0xc0 || (m[0] & 0xf0) == 0xd0 ? 2 : 3;
			(void)s.midi.message(m, len);
		} else if ((w0 >> 28) == 4) {
			(void)s.midi.ump2(w0, q.words[1]);
		}
	}
	static void process(void* state, const nsc_event* ev, int32_t n, float* const* out) {
		State& s = st(state);
		for (int i = 0; i < n && s.queued < QUEUE; i++) {
			Queued q{s.frame + (ev[i].frame < NSC_BLOCK ? ev[i].frame : NSC_BLOCK - 1), {}};
			const int nw = words_of(ev[i].words[0]);
			for (int k = 0; k < nw; k++) q.words[k] = ev[i].words[k];
			s.queue[s.queued++] = q;
		}
		// the 2 ms steps due by this block's start, each with the events at or before its frame:
		// step k is at frame ceil(k sr / 500), integer (at 48 kHz every 96 frames, at 44.1 kHz
		// 88 or 89)
		int taken = 0;
		for (; step_frame(s.steps, s.sample_rate) <= s.frame; s.steps++) {
			const uint64_t fs = step_frame(s.steps, s.sample_rate);
			for (; taken < s.queued && s.queue[taken].frame <= fs; taken++) feed(s, s.queue[taken]);
			s.midi.step();
		}
		if (taken > 0) {
			std::memmove(s.queue, s.queue + taken, sizeof(Queued) * static_cast<size_t>(s.queued - taken));
			s.queued -= taken;
		}
		const dsp::VoiceControls* m = s.midi.voices();
		for (int v = 0; v < s.voices && v < 16; v++) {
			s.vc[v].note = m[v].note;
			s.vc[v].pres = m[v].pres;
			s.vc[v].vpres = m[v].vpres;
			s.vc[v].but_x = m[v].but_x;
			s.vc[v].but_y = m[v].but_y;
			dsp::track_key(s.vc[v]);
		}
		s.g = s.midi.globals();
		s.engine.process(s.vc, s.g, out);
		s.frame += NSC_BLOCK;
	}
	static void process_voices(void* state, const nsc_voice* vc, const nsc_globals* g, float* const* out) {
		st(state).engine.process(reinterpret_cast<const dsp::VoiceControls*>(vc), *reinterpret_cast<const dsp::GlobalControls*>(g), out);
	}
	static int32_t active_voices(const void* state) { return st(state).engine.active_voices(); }
	static int32_t voices(const void* state) { return st(state).engine.voices(); }
	static int32_t preset(const void* state) { return st(state).engine.preset(); }
	static void set_preset(void* state, int32_t i) { st(state).engine.set_preset(i); }
	static float param(const void* state, int32_t i) {
		if (i < 0 || i >= E::info.num_params) return 0.0f;
		const float* p = const_cast<E&>(st(state).engine).param(i);
		return p ? *p : 0.0f;
	}
	// the board's rule (synth_strisy.cpp apply_params): NaN ignored, steps rounded, the engine clamps
	static int32_t set_param(void* state, int32_t i, float v) {
		if (i < 0 || i >= E::info.num_params || std::isnan(v)) return 0;
		const dsp::ParamDesc* d = E::param_desc(i);
		if (d->kind != dsp::ParamKind::continuous) v = static_cast<float>(static_cast<int>(v + (v >= 0 ? 0.5f : -0.5f)));
		return st(state).engine.set_param(i, v) ? 1 : 0;
	}
	static int32_t param_index(uint32_t id) {
		const nsc_param* p = params();
		for (int i = 0; i < E::info.num_params; i++)
			if (p[i].id == id) return i;
		return -1;
	}

	static const nsc_component* get() {
		static const nsc_component c = {desc(), init, reset, mode, process, process_voices, active_voices, voices,
		    preset, set_preset, param, set_param, param_index};
		return &c;
	}
};

} // namespace nsc

// the DLL's one export, for engine E
#define NSC_DEFINE_ENTRY(E) \
	extern "C" NSC_EXPORT const nsc_component* nsc_entry(void) { return ::nsc::Shim<E>::get(); }

#endif // NSC_SHIM_H_
