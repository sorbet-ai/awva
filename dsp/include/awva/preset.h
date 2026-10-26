// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// A sound preset as one flat JSON file: the engine (id, version, the hash of its parameter
// schema), a title and description, the hero parameters, the parameters by name, the modes.
//
//   {
//     "format": "awva-preset/1",
//     "engine": {"id": "accordion", "version": "1.2.0", "params_hash": "7f3a9c21"},
//     "title": "Musette",
//     "description": "Three reeds, wide detune, dry.",
//     "author": "...",
//     "tags": ["accordion", "folk"],
//     "heroes": ["detune", "reeds", "bellows"],
//     "params": {"detune": 0.35, "reeds": 3, "bellows": 0.8},
//     "modes": {"mono": false}
//   }
//
// One format everywhere: the factory presets (presets/<engine>/*.preset.json, compiled into the
// engines), the SDK and the component, an embedded host's preset store. Pure: no heap,
// no libc, fixed capacities (a parse that doesn't fit fails), constexpr (the factory presets are
// read at compile time). The writer prints a float with the fewest digits that read back as the
// same float, so a preset written and read again is bit for bit the same.
//
// The reader takes JSON's syntax, flat: the top-level object, its two objects (engine, params,
// modes: string -> string/number/bool) and arrays of strings (tags, heroes). Unknown keys are
// skipped (their value may be any JSON value, nested too); strings may have the escapes \" \\ \/
// \b \f \n \r \t, and \uXXXX below 0x80 (others read as '?').
#ifndef AWVA_PRESET_H_
#define AWVA_PRESET_H_

#include <stddef.h>
#include <stdint.h>

#include <utility>

#include "params.h"

namespace awva::preset {

inline constexpr const char* FORMAT = "awva-preset/1";
inline constexpr int MAX_PARAMS = 96, MAX_TAGS = 8, MAX_HEROES = 8, MAX_MODES = 4;
inline constexpr int NAME = 24, ID = 32, VERSION = 48, TITLE = 64, TEXT = 256;

// a fixed-capacity string
template <int N> struct Str {
	char s[N] = {};
	int n = 0;
	[[nodiscard]] constexpr const char* c_str() const { return s; }
	[[nodiscard]] constexpr bool empty() const { return n == 0; }
	constexpr bool push(char c) {
		if (n + 1 >= N) return false;
		s[n++] = c;
		s[n] = 0;
		return true;
	}
	constexpr bool set(const char* t) {
		n = 0;
		s[0] = 0;
		for (; t && *t; t++)
			if (!push(*t)) return false;
		return true;
	}
	[[nodiscard]] constexpr bool operator==(const char* t) const {
		int i = 0;
		for (; t[i] && i < n; i++)
			if (s[i] != t[i]) return false;
		return i == n && t[i] == 0;
	}
};

struct Param {
	Str<NAME> name;
	float value = 0.0f;
};
struct Mode {
	Str<NAME> name;
	bool on = false;
};

struct Preset {
	Str<ID> engine;
	Str<VERSION> version;
	uint32_t params_hash = 0;
	bool has_hash = false;
	Str<TITLE> title, author;
	Str<TEXT> description;
	Str<NAME> tags[MAX_TAGS];
	int num_tags = 0;
	Str<NAME> heroes[MAX_HEROES];
	int num_heroes = 0;
	Param params[MAX_PARAMS];
	int num_params = 0;
	Mode modes[MAX_MODES];
	int num_modes = 0;

	// empty again, in place (a Preset is 5 KB: `p = Preset()` would build one on the stack first,
	// and embedded stacks may be 1 KB)
	constexpr void clear() {
		engine.set("");
		version.set("");
		params_hash = 0;
		has_hash = false;
		title.set("");
		author.set("");
		description.set("");
		num_tags = num_heroes = num_params = num_modes = 0;
	}

	// the value of parameter name, or nullptr
	[[nodiscard]] constexpr const float* param(const char* name) const {
		for (int i = 0; i < num_params; i++)
			if (params[i].name == name) return &params[i].value;
		return nullptr;
	}
	[[nodiscard]] constexpr const bool* mode(const char* name) const {
		for (int i = 0; i < num_modes; i++)
			if (modes[i].name == name) return &modes[i].on;
		return nullptr;
	}
	constexpr bool add_param(const char* name, float v) {
		if (num_params == MAX_PARAMS) return false;
		Param& p = params[num_params];
		if (!p.name.set(name)) return false;
		p.value = v;
		num_params++;
		return true;
	}
	constexpr bool add_mode(const char* name, bool on) {
		if (num_modes == MAX_MODES) return false;
		if (!modes[num_modes].name.set(name)) return false;
		modes[num_modes++].on = on;
		return true;
	}
};

// --- the parameter schema's hash: FNV-1a over each parameter's id (FNV-1a of its name), range
// and kind, in table order. Equal hashes: a preset's values apply exactly.
[[nodiscard]] constexpr uint32_t fnv1a(const char* s, uint32_t h = 2166136261u) {
	for (; *s; s++) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
	return h;
}
[[nodiscard]] constexpr uint32_t fnv1a_word(uint32_t h, uint32_t w) {
	for (int b = 0; b < 4; b++) h = (h ^ ((w >> (8 * b)) & 0xffu)) * 16777619u;
	return h;
}
[[nodiscard]] constexpr uint32_t float_bits(float f) { return __builtin_bit_cast(uint32_t, f); }
// desc(i) for i in 0 .. n-1
template <class Desc> [[nodiscard]] constexpr uint32_t schema_hash(int n, Desc desc) {
	uint32_t h = 2166136261u;
	for (int i = 0; i < n; i++) {
		const awva::ParamDesc* d = desc(i);
		h = fnv1a_word(h, fnv1a(d->name));
		h = fnv1a_word(h, float_bits(d->min));
		h = fnv1a_word(h, float_bits(d->max));
		h = fnv1a_word(h, static_cast<uint32_t>(d->kind));
	}
	return h;
}

// --- reading ---------------------------------------------------------------------------------

enum class Error : uint8_t {
	none,
	syntax,    // not JSON (or not flat where it must be)
	too_long,  // a string or a list beyond its capacity
	no_format, // "format" missing or not awva-preset/1
	no_engine, // engine.id missing
	bad_hash,  // params_hash not 8 hex digits
	bad_value, // a parameter that isn't a finite number or a boolean
};

[[nodiscard]] constexpr const char* error_name(Error e) {
	switch (e) {
	case Error::none: return "none";
	case Error::syntax: return "syntax";
	case Error::too_long: return "too_long";
	case Error::no_format: return "no_format";
	case Error::no_engine: return "no_engine";
	case Error::bad_hash: return "bad_hash";
	case Error::bad_value: return "bad_value";
	}
	return "?";
}

namespace detail {

class Reader {
public:
	constexpr Reader(const char* s, size_t n) :
	    s_(s), n_(n) {}
	Error err = Error::none;
	size_t at = 0; // where it stopped

	constexpr void ws() {
		while (at < n_ && (s_[at] == ' ' || s_[at] == '\t' || s_[at] == '\n' || s_[at] == '\r')) at++;
	}
	[[nodiscard]] constexpr bool peek(char c) {
		ws();
		return at < n_ && s_[at] == c;
	}
	constexpr bool eat(char c) {
		if (!peek(c)) return fail(Error::syntax);
		at++;
		return true;
	}
	constexpr bool fail(Error e) {
		if (err == Error::none) err = e;
		return false;
	}
	[[nodiscard]] constexpr bool done() {
		ws();
		return at == n_;
	}

	// a string into out (capacity N); out may be nullptr (skip)
	template <int N> constexpr bool string(Str<N>* out) {
		if (!eat('"')) return false;
		if (out) out->set("");
		while (at < n_) {
			char c = s_[at++];
			if (c == '"') return true;
			if (static_cast<unsigned char>(c) < 0x20) return fail(Error::syntax);
			if (c == '\\') {
				if (at >= n_) return fail(Error::syntax);
				const char e = s_[at++];
				switch (e) {
				case '"': c = '"'; break;
				case '\\': c = '\\'; break;
				case '/': c = '/'; break;
				case 'b': c = '\b'; break;
				case 'f': c = '\f'; break;
				case 'n': c = '\n'; break;
				case 'r': c = '\r'; break;
				case 't': c = '\t'; break;
				case 'u': {
					uint32_t u = 0;
					for (int k = 0; k < 4; k++) {
						if (at >= n_) return fail(Error::syntax);
						const int d = hex_digit(s_[at++]);
						if (d < 0) return fail(Error::syntax);
						u = u * 16 + static_cast<uint32_t>(d);
					}
					c = u < 0x80 ? static_cast<char>(u) : '?';
					break;
				}
				default: return fail(Error::syntax);
				}
			}
			if (out && !out->push(c)) return fail(Error::too_long);
		}
		return fail(Error::syntax);
	}
	constexpr bool skip_string() { return string<1>(nullptr); }

	// a number: JSON's syntax, as the nearest float (through double: exact for the writer's digits)
	constexpr bool number(float& out) {
		ws();
		size_t i = at;
		bool neg = false;
		if (i < n_ && s_[i] == '-') {
			neg = true;
			i++;
		}
		if (i >= n_ || !is_digit(s_[i])) return fail(Error::syntax);
		double mant = 0;
		int digits = 0, exp10 = 0;
		auto take = [&](char c, bool frac) {
			if (digits < 17) {
				mant = mant * 10 + (c - '0');
				if (mant != 0) digits++;
				if (frac) exp10--;
			} else if (!frac) {
				exp10++;
			}
		};
		if (s_[i] == '0') {
			i++;
		} else {
			while (i < n_ && is_digit(s_[i])) take(s_[i++], false);
		}
		if (i < n_ && s_[i] == '.') {
			i++;
			if (i >= n_ || !is_digit(s_[i])) return fail(Error::syntax);
			while (i < n_ && is_digit(s_[i])) take(s_[i++], true);
		}
		if (i < n_ && (s_[i] == 'e' || s_[i] == 'E')) {
			i++;
			bool eneg = false;
			if (i < n_ && (s_[i] == '+' || s_[i] == '-')) eneg = s_[i++] == '-';
			if (i >= n_ || !is_digit(s_[i])) return fail(Error::syntax);
			int e = 0;
			while (i < n_ && is_digit(s_[i])) {
				if (e < 1000) e = e * 10 + (s_[i] - '0');
				i++;
			}
			exp10 += eneg ? -e : e;
		}
		at = i;
		double v = mant;
		// powers of ten up to 1e22 are exact in double; beyond, the value is out of float's range
		if (exp10 > 0) {
			v = exp10 > 40 ? 1e300 : v * pow10(exp10);
		} else if (exp10 < 0) {
			v = exp10 < -60 ? 0.0 : (exp10 >= -22 ? v / pow10(-exp10) : v / pow10(22) / pow10(-exp10 - 22));
		}
		// above float's largest (FLT_MAX) up to half its last step rounds to it; from there on it's inf
		if (v >= 3.4028235677973366e38) return fail(Error::bad_value);
		if (v > 3.4028234663852886e38) v = 3.4028234663852886e38;
		out = static_cast<float>(neg ? -v : v);
		return true;
	}

	// a literal true / false / null
	constexpr bool literal(const char* w) {
		ws();
		size_t i = at;
		for (; *w; w++, i++)
			if (i >= n_ || s_[i] != *w) return fail(Error::syntax);
		at = i;
		return true;
	}

	// any value (skipped)
	// one value of any kind skipped, nested up to 32 deep: without recursion (an embedded host may
	// parse on 1 KB stacks, and a preset may come from anywhere)
	constexpr bool skip_value() {
		uint32_t objects = 0; // bit d: level d is an object (else an array)
		int depth = 0;
		for (;;) {
			ws();
			if (at >= n_) return fail(Error::syntax);
			const char c = s_[at];
			if (c == '{' || c == '[') {
				at++;
				if (peek(c == '{' ? '}' : ']')) {
					at++;
				} else {
					if (depth == 32) return fail(Error::syntax);
					objects = c == '{' ? objects | (1u << depth) : objects & ~(1u << depth);
					depth++;
					if (c == '{' && !(skip_string() && eat(':'))) return false;
					continue; // its first value
				}
			} else if (c == '"') {
				if (!skip_string()) return false;
			} else if (c == 't' || c == 'f' || c == 'n') {
				if (!literal(c == 't' ? "true" : (c == 'f' ? "false" : "null"))) return false;
			} else {
				float f = 0;
				if (!number(f)) return false;
			}
			// a value ended: the next one at its level, or the levels it closes
			for (;;) {
				if (depth == 0) return true;
				const bool object = ((objects >> (depth - 1)) & 1u) != 0;
				if (peek(',')) {
					at++;
					if (object && !(skip_string() && eat(':'))) return false;
					break;
				}
				if (!eat(object ? '}' : ']')) return false;
				depth--;
			}
		}
	}

	[[nodiscard]] constexpr char cur() const { return at < n_ ? s_[at] : 0; }

private:
	static constexpr bool is_digit(char c) { return c >= '0' && c <= '9'; }
	static constexpr int hex_digit(char c) {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	}
	static constexpr double pow10(int e) {
		double p = 1.0;
		for (int k = 0; k < e; k++) p *= 10.0;
		return p;
	}
	const char* s_;
	size_t n_;
};

// "key": value pairs of an object, f(r, key) reads each value
template <class F> constexpr bool object(Reader& r, F f) {
	if (!r.eat('{')) return false;
	if (r.peek('}')) {
		r.at++;
		return true;
	}
	for (;;) {
		Str<NAME> key;
		if (!r.string(&key)) {
			if (r.err == Error::too_long) {
				r.err = Error::syntax; // (a key too long for any field: not ours)
			}
			return false;
		}
		if (!r.eat(':')) return false;
		if (!f(r, key)) return false;
		if (r.peek(',')) {
			r.at++;
			continue;
		}
		return r.eat('}');
	}
}

// an array of strings into out[max], count n
template <int N> constexpr bool strings(Reader& r, Str<N>* out, int max, int& n) {
	n = 0;
	if (!r.eat('[')) return false;
	if (r.peek(']')) {
		r.at++;
		return true;
	}
	for (;;) {
		if (n == max) return r.fail(Error::too_long);
		if (!r.string(&out[n])) return false;
		n++;
		if (r.peek(',')) {
			r.at++;
			continue;
		}
		return r.eat(']');
	}
}

constexpr bool hex32(const Str<16>& s, uint32_t& out) {
	if (s.n != 8) return false;
	uint32_t v = 0;
	for (int i = 0; i < 8; i++) {
		const char c = s.s[i];
		const int d = c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
		if (d < 0) return false;
		v = v * 16 + static_cast<uint32_t>(d);
	}
	out = v;
	return true;
}

} // namespace detail

// json (n bytes) into p; Error::none when it is a preset
[[nodiscard]] constexpr Error read(const char* json, size_t n, Preset& p) {
	p.clear();
	detail::Reader r(json, n);
	bool format = false;
	const bool ok = detail::object(r, [&](detail::Reader& rr, const Str<NAME>& key) {
		if (key == "format") {
			Str<32> f;
			if (!rr.string(&f)) return false;
			format = f == FORMAT;
			return true;
		}
		if (key == "engine") {
			return detail::object(rr, [&](detail::Reader& e, const Str<NAME>& k) {
				if (k == "id") return e.string(&p.engine);
				if (k == "version") return e.string(&p.version);
				if (k == "params_hash") {
					Str<16> h;
					if (!e.string(&h)) return false;
					if (!detail::hex32(h, p.params_hash)) return e.fail(Error::bad_hash);
					p.has_hash = true;
					return true;
				}
				return e.skip_value();
			});
		}
		if (key == "title") return rr.string(&p.title);
		if (key == "description") return rr.string(&p.description);
		if (key == "author") return rr.string(&p.author);
		if (key == "tags") return detail::strings(rr, p.tags, MAX_TAGS, p.num_tags);
		if (key == "heroes") return detail::strings(rr, p.heroes, MAX_HEROES, p.num_heroes);
		if (key == "params" || key == "modes") {
			const bool params = key == "params";
			return detail::object(rr, [&](detail::Reader& e, const Str<NAME>& k) {
				float v = 0.0f;
				bool b = false;
				const char c = (e.ws(), e.cur());
				if (c == 't' || c == 'f') {
					if (!e.literal(c == 't' ? "true" : "false")) return false;
					b = c == 't';
					v = b ? 1.0f : 0.0f;
				} else if (c == '-' || (c >= '0' && c <= '9')) {
					if (!e.number(v)) return false;
					b = v != 0.0f;
				} else {
					return e.fail(Error::bad_value);
				}
				if (params) return p.add_param(k.c_str(), v) || e.fail(Error::too_long);
				return p.add_mode(k.c_str(), b) || e.fail(Error::too_long);
			});
		}
		return rr.skip_value();
	});
	if (!ok) return r.err == Error::none ? Error::syntax : r.err;
	if (!r.done()) return Error::syntax;
	if (!format) return Error::no_format;
	if (p.engine.empty()) return Error::no_engine;
	return Error::none;
}
template <size_t N> [[nodiscard]] constexpr Error read(const char (&json)[N], Preset& p) { return read(json, N - 1, p); }

// --- applying it to an engine: survival by name -----------------------------------------------

inline constexpr int MAX_REPORTED = 16; // names reported per list (the counts count them all)

// what a preset gives an engine. The parameters are matched by name, so a preset survives a new
// version of its engine: a value out of the range now is clamped (and rounded, as a write is), a
// name the engine no longer has is skipped, a parameter the preset doesn't name gets the engine's
// default; all reported. Another engine's preset is refused. No migration code anywhere.
struct Resolved {
	bool refused = false; // another engine: nothing applied
	bool exact = false;   // the same schema hash, every value as written, nothing skipped or missing
	int num_clamped = 0, num_skipped = 0, num_missing = 0;
	Str<NAME> clamped[MAX_REPORTED], skipped[MAX_REPORTED], missing[MAX_REPORTED];
};

namespace detail {
template <int N> constexpr void note(Str<NAME> (&list)[N], int& count, const char* name) {
	if (count < N) list[count].set(name);
	count++;
}
[[nodiscard]] constexpr float write_rule(const awva::ParamDesc& d, float v) {
	float x = v < d.min ? d.min : (v > d.max ? d.max : v);
	if (d.kind != awva::ParamKind::continuous) x = static_cast<float>(static_cast<int>(x + (x >= 0 ? 0.5f : -0.5f)));
	return x;
}
} // namespace detail

// p for an engine (its id, its schema hash, n parameters desc(i), their defaults[n]): values[n],
// and r what was done (in place: a Resolved is 1.4 KB)
template <class Desc> constexpr void resolve(const Preset& p, const char* engine_id, uint32_t hash, int n, Desc desc, const float* defaults,
    float* values, Resolved& r) {
	r.refused = r.exact = false;
	r.num_clamped = r.num_skipped = r.num_missing = 0;
	if (!(p.engine == engine_id)) {
		r.refused = true;
		return;
	}
	for (int i = 0; i < n; i++) {
		const awva::ParamDesc* d = desc(i);
		const float* v = p.param(d->name);
		if (!v) {
			values[i] = defaults[i];
			detail::note(r.missing, r.num_missing, d->name);
			continue;
		}
		values[i] = detail::write_rule(*d, *v);
		if (float_bits(values[i]) != float_bits(*v)) detail::note(r.clamped, r.num_clamped, d->name);
	}
	for (int k = 0; k < p.num_params; k++) {
		bool known = false;
		for (int i = 0; i < n && !known; i++) known = p.params[k].name == desc(i)->name;
		if (!known) detail::note(r.skipped, r.num_skipped, p.params[k].name.c_str());
	}
	r.exact = p.has_hash && p.params_hash == hash && r.num_clamped == 0 && r.num_skipped == 0 && r.num_missing == 0;
}
template <class Desc> [[nodiscard]] constexpr Resolved resolve(const Preset& p, const char* engine_id, uint32_t hash, int n, Desc desc,
    const float* defaults, float* values) {
	Resolved r;
	resolve(p, engine_id, hash, n, desc, defaults, values, r);
	return r;
}

// --- factory presets: a preset file read at compile time ----------------------------------------

// a factory preset of an engine with N parameters: its name (the title) and every value, in the
// engine's parameter order
template <int N> struct Factory {
	Str<NAME> name;
	float values[N] = {};
};

namespace detail {
// not defined: reached in a constant evaluation, the compile error of a factory preset file that
// isn't one, lacks a parameter, has one the engine doesn't or a value out of its range
void factory_preset_is_wrong(const char* why);
} // namespace detail

// the factory preset file json for an engine with N parameters desc(i): every parameter named
// once, each value as a write leaves it (in range, rounded by kind)
template <int N, class Desc> [[nodiscard]] consteval Factory<N> factory(const char* json, Desc desc) {
	size_t n = 0;
	while (json[n]) n++;
	Preset p;
	if (read(json, n, p) != Error::none) detail::factory_preset_is_wrong("not a preset");
	Factory<N> f;
	if (!f.name.set(p.title.c_str())) detail::factory_preset_is_wrong("a title longer than a name");
	if (p.num_params != N) detail::factory_preset_is_wrong("not every parameter, or one too many");
	for (int i = 0; i < N; i++) {
		const awva::ParamDesc* d = desc(i);
		const float* v = p.param(d->name);
		if (!v) detail::factory_preset_is_wrong("a parameter missing");
		if (float_bits(detail::write_rule(*d, *v)) != float_bits(*v)) detail::factory_preset_is_wrong("a value out of its range");
		f.values[i] = *v;
	}
	return f;
}

// The factory presets of an engine from their files' texts Json[0 .. Count): factories<P, Json,
// Count>[i], P its awva::EngineParams (count, a constexpr desc). Each preset is read in a constant
// evaluation of its own (factory_preset<P, Json, I>), within any compiler's step limit.
template <class P, const char* const* Json, int I>
inline constexpr Factory<P::count> factory_preset = factory<P::count>(Json[I], [](int k) { return P::desc(k); });

namespace detail {
template <class P, const char* const* Json, class Seq> struct Factories;
template <class P, const char* const* Json, int... I> struct Factories<P, Json, std::integer_sequence<int, I...>> {
	static constexpr Factory<P::count> at[sizeof...(I)] = {factory_preset<P, Json, I>...};
};
} // namespace detail

template <class P, const char* const* Json, int Count>
inline constexpr const Factory<P::count>* factories = detail::Factories<P, Json, std::make_integer_sequence<int, Count>>::at;

// --- writing ---------------------------------------------------------------------------------

// out[cap]: the text so far, n its length; full: something didn't fit
struct Writer {
	char* out;
	size_t cap;
	size_t n = 0;
	bool full = false;
	void put(char c) {
		if (n + 1 < cap) {
			out[n++] = c;
			out[n] = 0;
		} else {
			full = true;
		}
	}
	void put(const char* s) {
		for (; *s; s++) put(*s);
	}
	void string(const char* s) {
		put('"');
		for (; *s; s++) {
			const unsigned char c = static_cast<unsigned char>(*s);
			if (c == '"' || c == '\\') {
				put('\\');
				put(static_cast<char>(c));
			} else if (c == '\n') {
				put("\\n");
			} else if (c == '\t') {
				put("\\t");
			} else if (c < 0x20) {
				const char hx[] = "0123456789abcdef";
				put("\\u00");
				put(hx[c >> 4]);
				put(hx[c & 15]);
			} else {
				put(static_cast<char>(c));
			}
		}
		put('"');
	}
	// a float with the fewest significant digits (1..9) that read back as the same float
	void number(float v) {
		char buf[40];
		format_float(v, buf);
		put(buf);
	}
	static void format_float(float v, char* buf) {
		if (v == 0.0f) {
			const bool neg = (float_bits(v) >> 31) != 0;
			buf[0] = neg ? '-' : '0';
			buf[1] = neg ? '0' : 0;
			buf[2] = 0;
			return;
		}
		for (int digits = 1; digits <= 9; digits++) {
			format_digits(v, digits, buf);
			float back = 0.0f;
			int len = 0;
			while (buf[len]) len++;
			detail::Reader r(buf, static_cast<size_t>(len));
			if (r.number(back) && r.done() && float_bits(back) == float_bits(v)) return;
		}
	}
	// v with digits significant digits: plain decimals for 1e-5 <= |v| < 1e9, else d.ddde+XX
	static void format_digits(float f, int digits, char* buf) {
		double v = f;
		int k = 0;
		if (v < 0) {
			buf[k++] = '-';
			v = -v;
		}
		// the exponent e: 10^e <= v < 10^(e+1)
		int e = 0;
		double p = 1.0;
		while (v >= p * 10.0) {
			p *= 10.0;
			e++;
		}
		while (v < p) {
			p /= 10.0;
			e--;
		}
		// the digits as an integer: round(v / 10^(e - digits + 1))
		double scaled = v;
		const int shift = digits - 1 - e;
		for (int s = 0; s < shift; s++) scaled *= 10.0;
		for (int s = 0; s > shift; s--) scaled /= 10.0;
		uint64_t m = static_cast<uint64_t>(scaled + 0.5);
		uint64_t lim = 1;
		for (int s = 0; s < digits; s++) lim *= 10;
		if (m >= lim) { // rounded up to the next power of ten
			m /= 10;
			e++;
		}
		char d[12];
		for (int s = digits - 1; s >= 0; s--) {
			d[s] = static_cast<char>('0' + m % 10);
			m /= 10;
		}
		int nd = digits;
		while (nd > 1 && d[nd - 1] == '0') nd--; // trailing zeros
		if (e >= -5 && e < 9) {
			if (e < 0) {
				buf[k++] = '0';
				buf[k++] = '.';
				for (int z = -1; z > e; z--) buf[k++] = '0';
				for (int s = 0; s < nd; s++) buf[k++] = d[s];
			} else {
				for (int s = 0; s <= e; s++) buf[k++] = s < nd ? d[s] : '0';
				if (nd > e + 1) {
					buf[k++] = '.';
					for (int s = e + 1; s < nd; s++) buf[k++] = d[s];
				}
			}
		} else {
			buf[k++] = d[0];
			if (nd > 1) {
				buf[k++] = '.';
				for (int s = 1; s < nd; s++) buf[k++] = d[s];
			}
			buf[k++] = 'e';
			int ae = e;
			if (ae < 0) {
				buf[k++] = '-';
				ae = -ae;
			}
			char t[4];
			int nt = 0;
			do {
				t[nt++] = static_cast<char>('0' + ae % 10);
				ae /= 10;
			} while (ae);
			while (nt) buf[k++] = t[--nt];
		}
		buf[k] = 0;
	}
};

// p as JSON into out[cap] (NUL terminated), one field per line, the parameters in their order;
// the length, 0 if it didn't fit
inline size_t write(const Preset& p, char* out, size_t cap) {
	if (cap == 0) return 0;
	out[0] = 0;
	Writer w{out, cap};
	auto key = [&](const char* k, bool first = false) {
		if (!first) w.put(",\n");
		w.put("  ");
		w.string(k);
		w.put(": ");
	};
	auto list = [&](const auto* items, int n) {
		w.put('[');
		for (int i = 0; i < n; i++) {
			if (i) w.put(", ");
			w.string(items[i].c_str());
		}
		w.put(']');
	};
	w.put("{\n");
	key("format", true);
	w.string(FORMAT);
	key("engine");
	w.put("{\"id\": ");
	w.string(p.engine.c_str());
	if (!p.version.empty()) {
		w.put(", \"version\": ");
		w.string(p.version.c_str());
	}
	if (p.has_hash) {
		char h[9];
		const char hx[] = "0123456789abcdef";
		for (int i = 0; i < 8; i++) h[i] = hx[(p.params_hash >> (28 - 4 * i)) & 15];
		h[8] = 0;
		w.put(", \"params_hash\": ");
		w.string(h);
	}
	w.put('}');
	key("title");
	w.string(p.title.c_str());
	if (!p.description.empty()) {
		key("description");
		w.string(p.description.c_str());
	}
	if (!p.author.empty()) {
		key("author");
		w.string(p.author.c_str());
	}
	if (p.num_tags) {
		key("tags");
		list(p.tags, p.num_tags);
	}
	if (p.num_heroes) {
		key("heroes");
		list(p.heroes, p.num_heroes);
	}
	key("params");
	w.put("{");
	for (int i = 0; i < p.num_params; i++) {
		w.put(i ? ",\n    " : "\n    ");
		w.string(p.params[i].name.c_str());
		w.put(": ");
		w.number(p.params[i].value);
	}
	w.put(p.num_params ? "\n  }" : "}");
	if (p.num_modes) {
		key("modes");
		w.put('{');
		for (int i = 0; i < p.num_modes; i++) {
			if (i) w.put(", ");
			w.string(p.modes[i].name.c_str());
			w.put(p.modes[i].on ? ": true" : ": false");
		}
		w.put('}');
	}
	w.put("\n}\n");
	return w.full ? 0 : w.n;
}

} // namespace awva::preset

#endif // AWVA_PRESET_H_
