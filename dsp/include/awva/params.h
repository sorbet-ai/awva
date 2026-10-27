// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// Live parameters: what a parameter is (ParamDesc), the per-engine table (EngineParams<Engine>,
// which an engine with parameters specializes) and the one rule for writing a value (write_param:
// every host uses it).
#ifndef AWVA_PARAMS_H_
#define AWVA_PARAMS_H_

#include <stdint.h>

namespace awva {

// Live-editable parameters. An engine with parameters specializes
// awva::EngineParams<its Engine>; the default here means "no parameters".
// ptr() returns the live value, changed() is called (in the synth thread) after it was written,
// so the engine can recompute derived values. Values are clamped to [min, max] before writing.
// What a parameter's values mean; the number is the kind's stable code.
enum class ParamKind : uint8_t {
	continuous = 0, // any value in [min, max]
	steps = 1,      // integers (writes are rounded)
	toggle = 2,     // on/off: min or max
};

// A parameter for people: its label ("Cutoff"), its hover text (a sentence: what it does, for a
// MIDI 2.0 or MPE host's tooltip) and its icon (a concept of assets/icons, optionally with a
// modifier: "release+pedal", docs/icons.md). The source is param_text/<engine>.json; an engine's
// table writes AWVA_TEXT(engine, name) for them (dsp/include/awva/param_text/<engine>.inc, written
// from it by tools/param_text.cmake). A build with AWVA_PARAM_TEXT 0 (an embedded host that shows
// no text) has none of the strings: the three are NULL, the layout is the same. The text is never
// part of a preset or the schema: rewording it changes no stored value.
#ifndef AWVA_PARAM_TEXT
#define AWVA_PARAM_TEXT 1
#endif
#if AWVA_PARAM_TEXT
#define AWVA_TEXT(engine, name) AWVA_TEXT_##engine##_##name
#else
#define AWVA_TEXT(engine, name) nullptr, nullptr, nullptr
#endif
#define AWVA_NO_TEXT nullptr, nullptr, nullptr // (a parameter without text yet: the effects')

struct ParamDesc {
	const char* name;  // short id, e.g. "cutoff"
	const char* group; // UI group, e.g. "filter"
	float min, max;
	ParamKind kind;
	const char* label; // for people, e.g. "Cutoff" (NULL with AWVA_PARAM_TEXT 0)
	const char* hover; // what it does, one or two sentences (NULL with AWVA_PARAM_TEXT 0)
	const char* icon;  // "concept" or "concept+modifier" (NULL with AWVA_PARAM_TEXT 0)
};

template <class E> struct EngineParams {
	static constexpr int count = 0;
	[[nodiscard]] static constexpr const ParamDesc* desc(int) { return nullptr; }
	[[nodiscard]] static float* ptr(E&, int) { return nullptr; }
	static void changed(E&, int) {}
};

// A write as every host applies it: NaN is ignored, the value is clamped to [min, max] and
// rounded unless the parameter is continuous. Returns false (nothing written) for NaN or a missing
// parameter; the caller then calls EngineParams<E>::changed().
[[nodiscard]] inline bool write_param(const ParamDesc* d, float* p, float value) {
	if (!d || !p || value != value) return false;
	float v = value < d->min ? d->min : (value > d->max ? d->max : value);
	if (d->kind != ParamKind::continuous) v = (float)(int)(v + (v >= 0 ? 0.5f : -0.5f));
	*p = v;
	return true;
}

// One block of a parameter's glide from a towards its written value t: the fraction k of the way
// (an engine's glide speed), but at least kGlideStep of the declared range, and onto t once that
// reaches it. The exponential alone never arrives: it stops a few ulps short and a slow one is
// still audibly off long after the edit (a self-oscillating bore or string hears 0.2%). With the
// floor a glide over the whole range arrives within 1/kGlideStep + ln(1 / (k kGlideStep)) / k blocks
// (a 0.006 glide: 470 blocks, 0.31 s); while the fraction moves more than the floor, a step is the
// exponential's, bit for bit. A NaN target is ignored; a NaN value takes the target.
inline constexpr float kGlideStep = 1e-3f;
// the same for any value that glides over a known range (a value derived from parameters)
[[nodiscard]] inline float glide_value(float a, float t, float k, float range) {
	if (t != t) return a;
	const float diff = t - a;
	if (diff != diff) return t;
	const float floor = kGlideStep * range;
	if (diff <= floor && diff >= -floor) return t;
	const float step = k * diff;
	if (step < floor && step > -floor) return a + (diff > 0.0f ? floor : -floor);
	return a + step;
}
[[nodiscard]] inline float glide_param(float a, float t, float k, const ParamDesc& d) { return glide_value(a, t, k, d.max - d.min); }

} // namespace awva

#endif // AWVA_PARAMS_H_
