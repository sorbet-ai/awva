// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// entry_of<E>(): the EngineEntry of a awva::VoiceEngine (and effect_entry_of<F>() for an
// Effect), for the generated awva_engine_<name> libraries (sdk/CMakeLists.txt).
#ifndef AWVA_SDK_ENGINE_ENTRY_H_
#define AWVA_SDK_ENGINE_ENTRY_H_

#include "awva_sdk/engines.h"

namespace awva::sdk {

template <dsp::VoiceEngine E> [[nodiscard]] const EngineEntry& entry_of(const char* header) {
	static_assert(sizeof(E) == E::info.state_bytes, "EngineInfo::state_bytes is the engine object's size");
	// the engine and its regions in one heap object; new Box() zero-fills it (plain data)
	struct Box {
		E e;
		dsp::Memory<E> m;
	};
	static const EngineEntry entry = {
	    &E::info,
	    header,
	    []() -> void* { return new Box(); },
	    [](void* p) { delete static_cast<Box*>(p); },
	    [](void* p, int sr, int nv) {
		    Box* b = static_cast<Box*>(p);
		    b->e.init(b->m.fast, b->m.large, sr, nv);
	    },
	    [](void* p) { static_cast<Box*>(p)->e.reset(); },
	    [](void* p, const NoteControls* vc, const GlobalControls& g, float* const* out) { static_cast<Box*>(p)->e.process(vc, g, out); },
	    [](const void* p) { return static_cast<const Box*>(p)->e.active_voices(); },
	    [](const void* p) { return static_cast<const Box*>(p)->e.voices(); },
	    [](int i) { return E::preset_name(i); },
	    [](void* p, int i) { static_cast<Box*>(p)->e.set_preset(i); },
	    [](const void* p) { return static_cast<const Box*>(p)->e.preset(); },
	    [](int i) { return E::param_desc(i); },
	    [](void* p, int i) { return static_cast<Box*>(p)->e.param(i); },
	    [](void* p, int i, float v) { return static_cast<Box*>(p)->e.set_param(i, v); },
	    [](void* p, const float* v, int index) { static_cast<Box*>(p)->e.load_params(v, index); },
	};
	return entry;
}

template <dsp::Effect F> [[nodiscard]] const EffectEntry& effect_entry_of() {
	static_assert(sizeof(F) == F::info.state_bytes, "EffectInfo::state_bytes is the effect object's size");
	struct Box {
		F f;
		dsp::Memory<F> m;
	};
	static const EffectEntry entry = {
	    &F::info,
	    []() -> void* { return new Box(); },
	    [](void* p) { delete static_cast<Box*>(p); },
	    [](void* p, int sr) {
		    Box* b = static_cast<Box*>(p);
		    b->f.init(b->m.fast, b->m.large, sr);
	    },
	    [](void* p) { static_cast<Box*>(p)->f.reset(); },
	    [](void* p, const float* const* in, float* const* out) { static_cast<Box*>(p)->f.process(in, out); },
	    [](int i) { return F::param_desc(i); },
	    [](void* p, int i) { return static_cast<Box*>(p)->f.param(i); },
	    [](void* p, int i, float v) { return static_cast<Box*>(p)->f.set_param(i, v); },
	};
	return entry;
}

} // namespace awva::sdk

#endif // AWVA_SDK_ENGINE_ENTRY_H_
