// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
//
// awva_conformance: the component ABI's conformance checks (nsc.h; docs/conformance.md) on any
// component library, awva's own or another's. It needs nothing but the library and nsc.h:
//
//   awva_conformance <library> [--known=TAG,...] [--expect=HASH] [doctest options]
//
//   --known=TAG,...  checks known to fail on this component (a check's tag, below): reported as
//                    warnings, not failures, until the component is fixed
//   --expect=HASH    the standard performance's render hash (the "hash" check prints it)
//   doctest options  -tc=<check>, -r=junit for a CI's report, --list-test-cases, ...
//
// The checks (tag: what it guarantees):
//   descriptor   the descriptor is complete and consistent: ids, names, ranges, kinds, defaults
//                inside their ranges, presets, heroes that name parameters, param_index
//   rates        init() takes every rate in min_rate .. max_rate and refuses (-1) the others; at
//                each, a note is finite, bounded, sounds and is silent and idle within its tail
//   init         silent and idle with no input; a note sounds
//   reset        back to silence, the parameters and the preset kept; then it plays exactly as
//                a fresh instance with them
//   parameters   the declared defaults are the values after init(), bit for bit; writes clamp,
//                round by kind, ignore NaN, refuse an index out of range
//   presets      each can be set and reads back; switching while notes sound stays finite
//   determinism  the same events give the same samples; two instances share nothing
//   blocks       a host's block size makes no difference: blocks of 1, 7, 33, 256 frames and of
//                random sizes, with late events at their negative frames, render the samples of
//                events given on time, bit for bit
//   note_ids     every Note On's id comes back from ended() exactly once: through voice
//                stealing, re-struck keys and more notes than the descriptor's max_note_ids (note_ids),
//                through preset switches (note_ids.presets) and through reset() (note_ids.reset)
//   output       (a component with the parameter "output") at 1 the samples are untouched, at
//                0.5 and 2 exactly half and double
//   silence      after the tail no subnormal sample (a subnormal tail costs a host time for
//                nothing heard) and nothing above -120 dBFS; whether it is exactly 0, reported
//   memory       random playing, parameter writes and presets at the lowest and highest rate:
//                finite, bounded, and no byte written outside the three regions
//   voices       process_voices(): silent at rest, a pressed voice sounds, deterministic
//   hash         the standard performance's render hash, FNV-1a 64 of the float bits (as
//                awva_render's), printed; with --expect, checked
#define DOCTEST_CONFIG_IMPLEMENT
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "doctest/doctest.h"
#include "nsc/load.h"

namespace {

nsc_library g_lib{};
std::string g_known, g_expect;

[[nodiscard]] const nsc_component& comp() { return *g_lib.c; }
[[nodiscard]] const nsc_desc& desc() { return *g_lib.c->desc; }
[[nodiscard]] int nvoices() { return std::max(1, std::min(10, static_cast<int>(desc().max_voices))); }
[[nodiscard]] int channels() { return std::max(1, static_cast<int>(desc().channels)); }
[[nodiscard]] std::string id() { return desc().id ? desc().id : "?"; }
// the rate the checks play at (48 kHz if the component takes it)
[[nodiscard]] int base_rate() { return std::clamp(48000, static_cast<int>(desc().min_rate), static_cast<int>(desc().max_rate)); }

constexpr float SILENT = 1e-4f; // the render checks' silence
constexpr float PEAK = 32.0f;   // a blow-up (nominal level is about +-1, peaks up to +-4)

// every preset, or -1 for a component without presets
[[nodiscard]] std::vector<int> presets() {
	std::vector<int> p;
	for (int i = 0; i < desc().num_presets; i++) p.push_back(i);
	if (p.empty()) p.push_back(-1);
	return p;
}

[[nodiscard]] bool known_failure(std::string_view check) {
	for (size_t at = 0; at <= g_known.size();) {
		const size_t end = std::min(g_known.find(',', at), g_known.size());
		if (std::string_view(g_known).substr(at, end - at) == check) return true;
		at = end + 1;
	}
	return false;
}
// CONFORM(check, expression): CHECK, or for a known failure (--known) a warning that says so
#define CONFORM(check, ...) \
	do { \
		if (known_failure(check)) { \
			const bool conform_ok_ = static_cast<bool>(__VA_ARGS__); \
			WARN_MESSAGE(conform_ok_, "known failure [" << check << "] (--known)"); \
			if (conform_ok_) MESSAGE("known failure [" << check << "] passes here"); \
		} else { \
			CHECK(__VA_ARGS__); \
		} \
	} while (0)

[[nodiscard]] uint64_t fnv64(const std::vector<float>& x) {
	uint64_t h = 1469598103934665603ull;
	for (float v : x) {
		uint32_t u;
		std::memcpy(&u, &v, 4);
		for (int k = 0; k < 4; k++) h = (h ^ ((u >> (8 * k)) & 0xffu)) * 1099511628211ull;
	}
	return h;
}

[[nodiscard]] bool same_bits(const std::vector<float>& a, const std::vector<float>& b) {
	return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

// --- an instance in the test's own regions ------------------------------------------------------

// The three regions, 64-byte aligned and zero-filled, each with a guard before and after it: a
// byte written outside a region shows in its guards.
class Instance {
public:
	static constexpr size_t GUARD = 1024;
	static constexpr uint8_t PATTERN = 0xa5;
	Instance() {
		const uint32_t sizes[3] = {desc().state_bytes, desc().fast_bytes, desc().large_bytes};
		for (int r = 0; r < 3; r++) {
			size_[r] = sizes[r];
			mem_[r].assign(size_[r] + 2 * GUARD + 64, PATTERN);
			uint8_t* base = mem_[r].data() + GUARD;
			region_[r] = base + (64 - reinterpret_cast<uintptr_t>(base) % 64) % 64;
			std::memset(region_[r], 0, size_[r]);
		}
	}
	Instance(const Instance&) = delete;
	Instance& operator=(const Instance&) = delete;
	~Instance() = default;
	Instance(Instance&&) = delete;
	Instance& operator=(Instance&&) = delete;

	[[nodiscard]] void* state() const { return region_[0]; }
	int init(int rate, int voices = nvoices()) { return comp().init(region_[0], region_[1], region_[2], rate, voices); }
	[[nodiscard]] bool guards_intact() const {
		for (int r = 0; r < 3; r++) {
			const uint8_t* lo = mem_[r].data();
			const uint8_t* hi = region_[r] + size_[r];
			for (const uint8_t* p = lo; p < region_[r]; p++)
				if (*p != PATTERN) return false;
			for (const uint8_t* p = hi; p < mem_[r].data() + mem_[r].size(); p++)
				if (*p != PATTERN) return false;
		}
		return true;
	}

private:
	std::vector<uint8_t> mem_[3];
	uint8_t* region_[3]{};
	size_t size_[3]{};
};

// --- events and performances ---------------------------------------------------------------------

struct Ev {
	int64_t frame;
	uint32_t w0, w1;
	int32_t note_id;
};

constexpr uint32_t ump2(uint32_t status, uint32_t ch, uint32_t index = 0) { return 0x40000000u | status << 20 | ch << 16 | index << 8; }

// The standard performance at a rate: MIDI 2.0 notes on channels 1..8 with their velocities,
// pressure, pitch bends and CC74 (the y), a key struck again while held, MIDI 1.0 notes in UMP
// (MPE), one with a Note On of velocity 0 for its Note Off, the sustain pedal; every Note On with
// an id (from *next_id). The events are off the 2 ms grid and off block starts; the last release
// is at 3.2 s.
[[nodiscard]] std::vector<Ev> performance(int rate, int32_t* next_id) {
	std::vector<Ev> v;
	const auto at = [rate](double s) { return static_cast<int64_t>(std::llround(s * rate)); };
	const uint32_t keys[8] = {48, 55, 60, 64, 67, 71, 74, 79};
	for (uint32_t n = 0; n < 8; n++) {
		const uint32_t ch = 1 + n;
		const double on = 0.013 + 0.29 * n, off = on + 1.2 - 0.07 * n;
		v.push_back({at(on), ump2(0x9, ch, keys[n]), (0x5000u + 0x1400u * n) << 16, (*next_id)++});
		for (int k = 1; on + 0.0113 * k < off; k++) {
			const double t = on + 0.0113 * k;
			v.push_back({at(t), ump2(0xd, ch), static_cast<uint32_t>(0x30000000u + 0x01000000u * static_cast<uint32_t>((k * 7 + n) % 64)), -1});
			v.push_back({at(t + 0.0031), ump2(0xe, ch), static_cast<uint32_t>(0x80000000u + static_cast<int32_t>(0x02000000 * std::sin(0.31 * k + n))), -1});
			if (k % 3 == 0) v.push_back({at(t + 0.0057), ump2(0xb, ch, 74), static_cast<uint32_t>(0x40000000u + 0x00800000u * static_cast<uint32_t>(k % 64)), -1});
		}
		v.push_back({at(off), ump2(0x8, ch, keys[n]), (0x3000u + 0x0800u * n) << 16, -1});
	}
	// a key struck again while it sounds (its first note is taken by the second)
	v.push_back({at(0.5011), ump2(0x9, 1, keys[0]), 0xe000u << 16, (*next_id)++});
	// MPE in MIDI 1.0 (UMP type 2): a chord on channels 10..12, channel pressure, the pedal
	const auto m1 = [](uint32_t status, uint32_t d1, uint32_t d2) { return 0x20000000u | status << 16 | d1 << 8 | d2; };
	v.push_back({at(2.3), m1(0xb0, 64, 127), 0, -1});
	for (uint32_t n = 0; n < 3; n++) {
		const uint32_t ch = 9 + n;
		v.push_back({at(2.4 + 0.017 * n), m1(0x90 | ch, 52 + 4 * n, 90 + 10 * n), 0, (*next_id)++});
		for (int k = 0; k < 20; k++) v.push_back({at(2.45 + 0.023 * k + 0.003 * n), m1(0xd0 | ch, static_cast<uint32_t>(30 + 4 * k), 0), 0, -1});
		v.push_back({at(2.95 + 0.01 * n), n == 1 ? m1(0x90 | ch, 52 + 4 * n, 0) : m1(0x80 | ch, 52 + 4 * n, 40), 0, -1});
	}
	v.push_back({at(3.2), m1(0xb0, 64, 0), 0, -1});
	std::stable_sort(v.begin(), v.end(), [](const Ev& a, const Ev& b) { return a.frame < b.frame; });
	return v;
}

// the ids that came back from ended(), drained after every process() as the guarantee asks
struct NoteIds {
	std::vector<int> count; // by id
	long unknown = 0;
	explicit NoteIds(int n) :
	    count(static_cast<size_t>(n), 0) {}
	void drain(void* state) {
		int32_t buf[64];
		int n = 0;
		do {
			n = comp().ended(state, buf, 64);
			for (int i = 0; i < n; i++) {
				if (buf[i] >= 0 && buf[i] < static_cast<int>(count.size())) count[static_cast<size_t>(buf[i])]++;
				else unknown++;
			}
		} while (n == 64);
	}
	[[nodiscard]] int missing() const { return static_cast<int>(std::count(count.begin(), count.end(), 0)); }
	[[nodiscard]] int twice() const {
		return static_cast<int>(std::count_if(count.begin(), count.end(), [](int k) { return k > 1; }));
	}
	[[nodiscard]] bool each_once() const { return unknown == 0 && missing() == 0 && twice() == 0; }
};

struct Rendered {
	std::vector<float> out; // every block's channels, one after the other
	float peak = 0.0f;
	long nonfinite = 0;
	int64_t last_loud = -1; // the last frame above SILENT
	[[nodiscard]] bool finite() const { return nonfinite == 0; }
};

// A host: blocks of `host_block` frames (0: random sizes 1..1024). It renders each of the
// component's blocks when that block's first frame is due, with the events it has seen so far;
// an event seen after its block was rendered goes to the next one at its negative frame. Host
// blocks of 32 give every event on time. before(block) runs before each process() (a preset, a
// parameter, a reset); ids, if given, drains ended() after each.
Rendered render(Instance& in, const std::vector<Ev>& perf, int64_t frames, int host_block = 32, NoteIds* ids = nullptr,
    const std::function<void(int64_t)>& before = {}) {
	Rendered r;
	const int nc = channels();
	std::vector<float> buf(static_cast<size_t>(nc) * NSC_BLOCK);
	std::vector<float*> o(static_cast<size_t>(nc));
	for (int c = 0; c < nc; c++) o[static_cast<size_t>(c)] = &buf[static_cast<size_t>(c) * NSC_BLOCK];
	std::vector<Ev> seen;
	size_t k = 0;
	int64_t block = 0;
	uint32_t rng = 12345u;
	for (int64_t at = 0; at < frames;) {
		rng = rng * 1664525u + 1013904223u;
		const int64_t n = std::min<int64_t>(host_block > 0 ? host_block : 1 + (rng >> 8) % 1024, frames - at);
		for (; k < perf.size() && perf[k].frame < at + n; k++) seen.push_back(perf[k]);
		for (; block * NSC_BLOCK < at + n; block++) {
			const int64_t b0 = block * NSC_BLOCK;
			std::vector<nsc_event> ev;
			size_t taken = 0;
			for (; taken < seen.size() && seen[taken].frame < b0 + NSC_BLOCK; taken++)
				ev.push_back({static_cast<int32_t>(seen[taken].frame - b0), seen[taken].note_id, {seen[taken].w0, seen[taken].w1, 0, 0}});
			seen.erase(seen.begin(), seen.begin() + static_cast<std::ptrdiff_t>(taken));
			if (before) before(block);
			comp().process(in.state(), ev.data(), static_cast<int32_t>(ev.size()), o.data());
			if (ids) ids->drain(in.state());
			for (int i = 0; i < nc * NSC_BLOCK; i++) {
				const float x = buf[static_cast<size_t>(i)];
				if (!std::isfinite(x)) r.nonfinite++;
				else {
					r.peak = std::max(r.peak, std::fabs(x));
					if (std::fabs(x) > SILENT) r.last_loud = b0 + i % NSC_BLOCK;
				}
			}
			r.out.insert(r.out.end(), buf.begin(), buf.end());
		}
		at += n;
	}
	return r;
}

[[nodiscard]] int64_t frames_of(double seconds, int rate) { return static_cast<int64_t>(seconds * rate); }
// the standard performance's length: its last release, the tail, half a second
[[nodiscard]] double perf_seconds() { return 3.2 + static_cast<double>(desc().tail_seconds) + 0.5; }

// a fresh instance at a rate with a preset (-1: none), initialised
void start(Instance& in, int rate, int preset) {
	REQUIRE(in.init(rate) == 0);
	if (preset >= 0) comp().set_preset(in.state(), preset);
}

[[nodiscard]] std::vector<Ev> standard(int rate) {
	int32_t ids = 0;
	return performance(rate, &ids);
}

// random events: notes on and off on 16 channels and many keys, pressure, bends, CC74, the pedal
[[nodiscard]] std::vector<Ev> random_events(uint32_t seed, int rate, double seconds) {
	std::vector<Ev> v;
	uint32_t s = seed;
	const auto next = [&s]() {
		s = s * 1664525u + 1013904223u;
		return s;
	};
	const int64_t frames = frames_of(seconds, rate);
	for (int64_t f = 0; f < frames; f += 1 + static_cast<int64_t>(next() % 97)) {
		const uint32_t u = next() % 100, ch = next() % 16, key = 20 + next() % 90;
		if (u < 12) v.push_back({f, ump2(0x9, ch, key), next() & 0xffff0000u, -1});
		else if (u < 24) v.push_back({f, ump2(0x8, ch, key), next() & 0xffff0000u, -1});
		else if (u < 55) v.push_back({f, ump2(0xd, ch), next(), -1});
		else if (u < 75) v.push_back({f, ump2(0xe, ch), next(), -1});
		else if (u < 92) v.push_back({f, ump2(0xb, ch, 74), next(), -1});
		else v.push_back({f, ump2(0xb, ch, 64), next() % 2 ? 0xffffffffu : 0u, -1});
	}
	return v;
}

// one random parameter value: mostly in range, often the extremes, sometimes far outside or NaN
[[nodiscard]] float random_value(uint32_t u, const nsc_param& p) {
	const float span = p.max - p.min, x = static_cast<float>(u >> 8) * (1.0f / 16777216.0f);
	switch (u % 7) {
	case 0: return p.min;
	case 1: return p.max;
	case 2: return p.min - 10.0f * span - 1.0f;
	case 3: return p.max + 10.0f * span + 1.0f;
	case 4: return std::nanf("");
	default: return p.min + x * span;
	}
}

} // namespace

TEST_CASE("descriptor: complete and consistent") {
	const nsc_desc& d = desc();
	CAPTURE(id());
	CHECK(d.magic == NSC_MAGIC);
	CHECK(d.abi == NSC_ABI);
	REQUIRE(d.short_name != nullptr);
	CHECK(std::strlen(d.short_name) >= 1);
	CHECK(std::strlen(d.short_name) <= 6);
	for (const char* s : {d.id, d.uid, d.name, d.short_name, d.summary, d.version}) {
		REQUIRE(s != nullptr);
		CHECK(std::strlen(s) > 0);
	}
	CHECK((d.channels == 1 || d.channels == 2));
	CHECK(d.led != 0); // every engine has a colour
	CHECK(d.led <= 0xffffffu);
	CHECK(d.max_voices >= 1);
	CHECK(d.tail_seconds > 0.0f);
	CHECK(d.tail_seconds <= 30.0f);
	CHECK(d.min_rate > 0);
	CHECK(d.min_rate <= d.max_rate);
	CHECK(d.num_params >= 0);
	CHECK(d.num_presets >= 0);
	CHECK((d.num_params == 0 || d.params != nullptr));
	CHECK((d.num_presets == 0 || d.presets != nullptr));
	std::set<std::string> names;
	std::set<uint32_t> ids;
	for (int i = 0; i < d.num_params; i++) {
		const nsc_param& p = d.params[i];
		CAPTURE(i);
		REQUIRE(p.name != nullptr);
		CAPTURE(std::string(p.name));
		CHECK(std::strlen(p.name) > 0);
		CHECK(p.group != nullptr);
		CHECK(names.insert(p.name).second);
		CHECK(ids.insert(p.id).second);
		CHECK(std::isfinite(p.min));
		CHECK(std::isfinite(p.max));
		CHECK(p.min <= p.max);
		CHECK((p.kind >= NSC_PARAM_CONTINUOUS && p.kind <= NSC_PARAM_TOGGLE));
		CHECK(p.def >= p.min);
		CHECK(p.def <= p.max);
		if (p.kind != NSC_PARAM_CONTINUOUS) {
			CHECK(p.min == std::round(p.min));
			CHECK(p.max == std::round(p.max));
			CHECK(p.def == std::round(p.def));
		}
		CHECK(comp().param_index(p.id) == i);
	}
	uint32_t unused = 0x9e3779b9u;
	while (ids.count(unused)) unused++;
	CHECK(comp().param_index(unused) == -1);
	const auto check_heroes = [&](const char* const* h) {
		bool ended = false;
		std::set<std::string> seen;
		for (int k = 0; k < NSC_MAX_HEROES; k++) {
			if (!h[k]) {
				ended = true;
				continue;
			}
			CAPTURE(std::string(h[k]));
			CHECK(!ended); // (no gaps)
			CHECK(names.count(h[k]) == 1);
			CHECK(seen.insert(h[k]).second);
		}
	};
	check_heroes(d.heroes);
	std::set<std::string> preset_names;
	for (int i = 0; i < d.num_presets; i++) {
		CAPTURE(i);
		REQUIRE(d.presets[i].name != nullptr);
		CHECK(std::strlen(d.presets[i].name) > 0);
		CHECK(preset_names.insert(d.presets[i].name).second);
		check_heroes(d.presets[i].heroes);
	}
	MESSAGE(id() << " (" << d.uid << ", " << d.version << "): " << d.num_presets << " presets, " << d.num_params << " parameters, rates " << d.min_rate << " .. "
	             << d.max_rate << ", tail " << d.tail_seconds << " s, regions " << d.state_bytes << " + " << d.fast_bytes << " + " << d.large_bytes << " B");
}

TEST_CASE("rates: init() takes min_rate .. max_rate and refuses the others, and at each a note sounds and ends within the tail") {
	const nsc_desc& d = desc();
	{
		Instance in;
		if (d.min_rate > 1) CHECK(in.init(d.min_rate - 1) == -1);
		CHECK(in.init(d.max_rate + 1) == -1);
	}
	std::set<int> rates = {d.min_rate, d.max_rate};
	for (int r : {11025, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000})
		if (r >= d.min_rate && r <= d.max_rate) rates.insert(r);
	const float tail = d.tail_seconds;
	for (int rate : rates) {
		CAPTURE(id());
		CAPTURE(rate);
		Instance in;
		REQUIRE(in.init(rate) == 0);
		// a chord, pressed and bent, released at 1 s
		std::vector<Ev> v;
		for (uint32_t n = 0; n < 3; n++) {
			v.push_back({frames_of(0.01 * n, rate), ump2(0x9, 1 + n, 57 + 5 * n), 0xa000u << 16, -1});
			v.push_back({frames_of(0.3, rate), ump2(0xd, 1 + n), 0x90000000u, -1});
			v.push_back({frames_of(0.5, rate), ump2(0xe, 1 + n), 0x88000000u, -1});
			v.push_back({frames_of(1.0, rate), ump2(0x8, 1 + n, 57 + 5 * n), 0x80000000u, -1});
		}
		const Rendered r = render(in, v, frames_of(1.0 + tail + 0.5, rate));
		CHECK(r.finite());
		CHECK(r.peak > 1e-3f);
		CHECK(r.peak < PEAK);
		const double silent_after = static_cast<double>(r.last_loud + 1) / rate - 1.0;
		CAPTURE(silent_after);
		CHECK(silent_after <= tail);
		CHECK(comp().active_voices(in.state()) == 0);
		CHECK(in.guards_intact());
	}
}

TEST_CASE("init: silent and idle with no input, a note sounds") {
	for (int p : presets()) {
		CAPTURE(id());
		CAPTURE(p);
		Instance in;
		start(in, base_rate(), p);
		const Rendered idle = render(in, {}, frames_of(0.5, base_rate()));
		CHECK(idle.finite());
		CHECK(idle.peak < 1e-5f);
		CHECK(comp().active_voices(in.state()) == 0);
		const Rendered note = render(in, {{0, ump2(0x9, 0, 60), 0xb000u << 16, -1}}, frames_of(1.0, base_rate()));
		CHECK(note.finite());
		CHECK(note.peak > 1e-3f);
		CHECK(note.peak < PEAK);
	}
}

TEST_CASE("reset: back to silence, the parameters and the preset kept, then as a fresh instance") {
	const nsc_desc& d = desc();
	const int rate = base_rate();
	const std::vector<Ev> perf = standard(rate);
	for (int p : {presets().front(), presets().back()}) {
		CAPTURE(id());
		CAPTURE(p);
		Instance in;
		start(in, rate, p);
		// live edits: every parameter to a point between its value and its maximum
		std::vector<float> want;
		for (int i = 0; i < d.num_params; i++) {
			const float v = comp().param(in.state(), i);
			comp().set_param(in.state(), i, v + 0.37f * (d.params[i].max - v));
			want.push_back(comp().param(in.state(), i));
		}
		(void)render(in, perf, frames_of(1.0, rate));
		comp().reset(in.state());
		CHECK(comp().active_voices(in.state()) == 0);
		CHECK(comp().preset(in.state()) == p);
		for (int i = 0; i < d.num_params; i++) {
			CAPTURE(std::string(d.params[i].name));
			CHECK(comp().param(in.state(), i) == want[static_cast<size_t>(i)]);
		}
		const Rendered quiet = render(in, {}, frames_of(0.2, rate));
		CHECK(quiet.peak < 1e-5f);
		const Rendered a = render(in, perf, frames_of(perf_seconds(), rate));
		Instance f;
		start(f, rate, p);
		for (int i = 0; i < d.num_params; i++) comp().set_param(f.state(), i, want[static_cast<size_t>(i)]);
		(void)render(f, {}, frames_of(0.2, rate));
		const Rendered b = render(f, perf, frames_of(perf_seconds(), rate));
		CHECK(same_bits(a.out, b.out));
		CHECK(in.guards_intact());
	}
}

TEST_CASE("parameters: the defaults are the values after init(), writes clamp, round and ignore NaN") {
	const nsc_desc& d = desc();
	Instance in;
	REQUIRE(in.init(base_rate()) == 0);
	for (int i = 0; i < d.num_params; i++) {
		const nsc_param& p = d.params[i];
		CAPTURE(id());
		CAPTURE(std::string(p.name));
		const float v0 = comp().param(in.state(), i);
		uint32_t a, b;
		std::memcpy(&a, &v0, 4);
		std::memcpy(&b, &p.def, 4);
		CHECK(a == b); // (bit for bit)
		CHECK(comp().set_param(in.state(), i, std::nanf("")) == 0);
		CHECK(comp().param(in.state(), i) == v0);
		CHECK(comp().set_param(in.state(), i, p.max + 1000.0f) == 1);
		CHECK(comp().param(in.state(), i) == p.max);
		CHECK(comp().set_param(in.state(), i, p.min - 1000.0f) == 1);
		CHECK(comp().param(in.state(), i) == p.min);
		if (p.kind != NSC_PARAM_CONTINUOUS && p.max - p.min >= 1.0f) {
			CHECK(comp().set_param(in.state(), i, p.min + 0.6f) == 1);
			CHECK(comp().param(in.state(), i) == p.min + 1.0f);
		}
		comp().set_param(in.state(), i, v0);
		CHECK(comp().param(in.state(), i) == v0);
	}
	CHECK(comp().set_param(in.state(), d.num_params, 0.0f) == 0);
	CHECK(comp().set_param(in.state(), -1, 0.0f) == 0);
	// every preset's values are inside the ranges
	for (int p : presets()) {
		if (p >= 0) comp().set_preset(in.state(), p);
		for (int i = 0; i < d.num_params; i++) {
			CAPTURE(p);
			CAPTURE(std::string(d.params[i].name));
			const float v = comp().param(in.state(), i);
			CHECK(v >= d.params[i].min);
			CHECK(v <= d.params[i].max);
		}
	}
}

TEST_CASE("presets: each is set and reads back, switching while notes sound stays finite and bounded") {
	const int np = desc().num_presets;
	Instance in;
	REQUIRE(in.init(base_rate()) == 0);
	if (np == 0) {
		CHECK(comp().preset(in.state()) == -1);
		return;
	}
	CHECK(comp().preset(in.state()) >= 0);
	CHECK(comp().preset(in.state()) < np);
	for (int i = 0; i < np; i++) {
		comp().set_preset(in.state(), i);
		CHECK(comp().preset(in.state()) == i);
	}
	const int rate = base_rate();
	const std::vector<Ev> perf = standard(rate);
	const int64_t every = frames_of(0.1, rate) / NSC_BLOCK;
	int next = 0;
	const Rendered r = render(in, perf, frames_of(perf_seconds(), rate), 32, nullptr, [&](int64_t b) {
		if (b % every == 0) comp().set_preset(in.state(), next++ % np);
	});
	CAPTURE(id());
	CHECK(r.finite());
	CHECK(r.peak < PEAK);
	CHECK(in.guards_intact());
}

TEST_CASE("determinism: the same events give the same samples, two instances share nothing") {
	const int rate = base_rate();
	const std::vector<Ev> perf = standard(rate);
	const std::vector<Ev> other = random_events(7u, rate, 3.0);
	const int64_t frames = frames_of(perf_seconds(), rate);
	for (int p : {presets().front(), presets().back()}) {
		CAPTURE(id());
		CAPTURE(p);
		std::vector<float> first, second, alone_b;
		{
			Instance a;
			start(a, rate, p);
			first = render(a, perf, frames).out;
		}
		{
			Instance a;
			start(a, rate, p);
			second = render(a, perf, frames).out;
		}
		{
			Instance b;
			start(b, rate, p);
			alone_b = render(b, other, frames).out;
		}
		CHECK(same_bits(first, second));
		// two at once, block by block: each as alone
		Instance a, b;
		start(a, rate, p);
		start(b, rate, p);
		std::vector<float> both_a, both_b;
		size_t ka = 0, kb = 0;
		const int nc = channels();
		std::vector<float> buf(static_cast<size_t>(nc) * NSC_BLOCK);
		std::vector<float*> o(static_cast<size_t>(nc));
		for (int c = 0; c < nc; c++) o[static_cast<size_t>(c)] = &buf[static_cast<size_t>(c) * NSC_BLOCK];
		for (int64_t b0 = 0; b0 < frames; b0 += NSC_BLOCK) {
			const auto block = [&](Instance& in, const std::vector<Ev>& v, size_t& k, std::vector<float>& out) {
				std::vector<nsc_event> ev;
				for (; k < v.size() && v[k].frame < b0 + NSC_BLOCK; k++) ev.push_back({static_cast<int32_t>(v[k].frame - b0), v[k].note_id, {v[k].w0, v[k].w1, 0, 0}});
				comp().process(in.state(), ev.data(), static_cast<int32_t>(ev.size()), o.data());
				out.insert(out.end(), buf.begin(), buf.end());
			};
			block(a, perf, ka, both_a);
			block(b, other, kb, both_b);
		}
		CHECK(same_bits(first, both_a));
		CHECK(same_bits(alone_b, both_b));
	}
}

TEST_CASE("blocks: any host block size, late events at their negative frames, renders the on-time samples bit for bit") {
	for (int rate : {44100, 48000}) {
		if (rate < desc().min_rate || rate > desc().max_rate) continue;
		const std::vector<Ev> perf = standard(rate);
		const int64_t frames = frames_of(perf_seconds(), rate);
		Instance ref;
		start(ref, rate, presets().back());
		const std::vector<float> on_time = render(ref, perf, frames, 32).out;
		for (int host : {1, 7, 33, 256, 0}) {
			CAPTURE(id());
			CAPTURE(rate);
			CAPTURE(host);
			Instance in;
			start(in, rate, presets().back());
			CHECK(same_bits(render(in, perf, frames, host).out, on_time));
		}
	}
}

namespace {
// the standard performance three times over with every Note On's id, with before(block) acting on
// the instance; then released and drained until every id is back or the tail has long passed
NoteIds note_ids_through(Instance& in, int rate, const std::function<void(int64_t)>& before) {
	int32_t next = 0;
	std::vector<Ev> all;
	for (int round = 0; round < 3; round++) {
		std::vector<Ev> v = performance(rate, &next);
		const int64_t shift = frames_of(1.7 * round, rate); // (overlapping rounds: more notes than voices)
		for (Ev& e : v) e.frame += shift;
		all.insert(all.end(), v.begin(), v.end());
	}
	// a burst of more notes than the component tracks (max_note_ids), held, then released
	for (int i = 0; i < desc().max_note_ids + 20; i++) {
		const auto ch = static_cast<uint32_t>(i % 16), key = static_cast<uint32_t>(30 + i / 16);
		all.push_back({frames_of(5.5, rate) + 3 * i, ump2(0x9, ch, key), 0x8000u << 16, next++});
		all.push_back({frames_of(6.5, rate) + 5 * i, ump2(0x8, ch, key), 0x8000u << 16, -1});
	}
	std::stable_sort(all.begin(), all.end(), [](const Ev& a, const Ev& b) { return a.frame < b.frame; });
	NoteIds ids(next);
	(void)render(in, all, frames_of(7.0 + desc().tail_seconds + 1.0, rate), 32, &ids, before);
	ids.drain(in.state());
	return ids;
}
} // namespace

TEST_CASE("note_ids: every Note On's id comes back exactly once (stealing, re-struck keys, more notes than are tracked)") {
	Instance in;
	REQUIRE(in.init(base_rate()) == 0);
	const NoteIds ids = note_ids_through(in, base_rate(), {});
	CAPTURE(id());
	CAPTURE(ids.missing());
	CAPTURE(ids.twice());
	CAPTURE(ids.unknown);
	CONFORM("note_ids", ids.each_once());
}

TEST_CASE("note_ids.presets: every id comes back exactly once while the presets change under the notes") {
	if (desc().num_presets == 0) return;
	Instance in;
	REQUIRE(in.init(base_rate()) == 0);
	const int64_t every = frames_of(0.37, base_rate()) / NSC_BLOCK;
	int next = 0;
	const NoteIds ids = note_ids_through(in, base_rate(), [&](int64_t b) {
		if (b % every == 0) comp().set_preset(in.state(), next++ % desc().num_presets);
	});
	CAPTURE(id());
	CAPTURE(ids.missing());
	CAPTURE(ids.twice());
	CAPTURE(ids.unknown);
	CONFORM("note_ids.presets", ids.each_once());
}

TEST_CASE("note_ids.reset: every id comes back exactly once when reset() comes while notes sound") {
	Instance in;
	REQUIRE(in.init(base_rate()) == 0);
	const int64_t at = frames_of(1.0, base_rate()) / NSC_BLOCK, again = frames_of(6.0, base_rate()) / NSC_BLOCK;
	const NoteIds ids = note_ids_through(in, base_rate(), [&](int64_t b) {
		if (b == at || b == again) comp().reset(in.state());
	});
	CAPTURE(id());
	CAPTURE(ids.missing());
	CAPTURE(ids.twice());
	CAPTURE(ids.unknown);
	CONFORM("note_ids.reset", ids.each_once());
}

TEST_CASE("output: at 1 the samples are untouched, at 0.5 and 2 exactly half and double") {
	int out = -1;
	for (int i = 0; i < desc().num_params; i++)
		if (std::strcmp(desc().params[i].name, "output") == 0) out = i;
	if (out < 0) {
		MESSAGE(id() << ": no parameter \"output\"");
		return;
	}
	const int rate = base_rate();
	const std::vector<Ev> perf = standard(rate);
	const int64_t frames = frames_of(3.0, rate);
	const auto with = [&](float level, bool touched) {
		Instance in;
		REQUIRE(in.init(rate) == 0);
		if (touched) comp().set_param(in.state(), out, 0.73f); // (and back: an untouched level)
		comp().set_param(in.state(), out, level);
		return render(in, perf, frames).out;
	};
	const std::vector<float> one = with(1.0f, false);
	CHECK(desc().params[out].def == 1.0f);
	CHECK(same_bits(with(1.0f, true), one));
	const std::vector<float> half = with(0.5f, false), twice = with(2.0f, false);
	bool exact = true, sounded = false;
	for (size_t n = 0; n < one.size(); n++) {
		exact = exact && half[n] == one[n] * 0.5f && twice[n] == one[n] * 2.0f;
		sounded = sounded || one[n] != 0.0f;
	}
	CAPTURE(id());
	CHECK(exact);
	CHECK(sounded);
}

TEST_CASE("silence: after the tail no subnormal sample and nothing above -120 dBFS") {
	const int rate = base_rate();
	const std::vector<Ev> perf = standard(rate);
	const double tail = desc().tail_seconds;
	const int64_t from = frames_of(3.2 + tail, rate), frames = frames_of(3.2 + tail + 0.5, rate);
	int not_zero = 0;
	for (int p : presets()) {
		CAPTURE(id());
		CAPTURE(p);
		Instance in;
		start(in, rate, p);
		const Rendered r = render(in, perf, frames);
		long nonzero = 0, subnormal = 0;
		float worst = 0.0f;
		const int nc = channels();
		for (size_t i = static_cast<size_t>(from / NSC_BLOCK) * NSC_BLOCK * static_cast<size_t>(nc); i < r.out.size(); i++) {
			const float x = r.out[i];
			nonzero += x != 0.0f;
			subnormal += x != 0.0f && std::fabs(x) < 1.17549435e-38f;
			worst = std::max(worst, std::fabs(x));
		}
		CAPTURE(nonzero);
		CAPTURE(subnormal);
		CAPTURE(worst);
		CONFORM("silence", subnormal == 0);
		CONFORM("silence", worst < 1e-6f); // (-120 dBFS)
		if (nonzero > 0) not_zero++;
	}
	MESSAGE(id() << ": " << not_zero << " of " << presets().size() << " presets leave samples after the tail that are not exactly 0");
}

TEST_CASE("memory: random playing, parameters and presets at the lowest and highest rate stay finite, bounded and in the regions") {
	const nsc_desc& d = desc();
	for (int rate : {static_cast<int>(d.min_rate), base_rate(), static_cast<int>(d.max_rate)}) {
		CAPTURE(id());
		CAPTURE(rate);
		Instance in;
		REQUIRE(in.init(rate) == 0);
		const std::vector<Ev> v = random_events(static_cast<uint32_t>(rate), rate, 4.0);
		uint32_t s = 99u;
		const Rendered r = render(in, v, frames_of(4.5, rate), 0, nullptr, [&](int64_t b) {
			s = s * 1664525u + 1013904223u;
			if (d.num_params > 0 && s % 4 == 0) {
				const int i = static_cast<int>((s >> 8) % static_cast<uint32_t>(d.num_params));
				comp().set_param(in.state(), i, random_value(s * 2654435761u, d.params[i]));
			}
			if (d.num_presets > 0 && b % 400 == 0) comp().set_preset(in.state(), static_cast<int>((s >> 4) % static_cast<uint32_t>(d.num_presets)));
		});
		CHECK(r.finite());
		CHECK(r.peak < 2.0f * PEAK); // (extreme parameters may be loud: only blow-ups fail)
		CHECK(in.guards_intact());
	}
}

TEST_CASE("voices: process_voices() is silent at rest, a pressed voice sounds, the same controls give the same samples") {
	const int rate = base_rate();
	const auto run = [&](bool press) {
		Instance in;
		REQUIRE(in.init(rate) == 0);
		std::vector<nsc_voice> v(static_cast<size_t>(nvoices()), nsc_voice{69.0f, 0, 0, 0, 0, 0, 0});
		const nsc_globals g = {0.0f, 1.0f, {0.0f, 0.0f, 0.0f}};
		const int nc = channels();
		std::vector<float> buf(static_cast<size_t>(nc) * NSC_BLOCK), all;
		std::vector<float*> o(static_cast<size_t>(nc));
		for (int c = 0; c < nc; c++) o[static_cast<size_t>(c)] = &buf[static_cast<size_t>(c) * NSC_BLOCK];
		for (int b = 0; b < static_cast<int>(frames_of(1.0, rate) / NSC_BLOCK); b++) {
			if (press) { // a key pressed over 20 ms, held
				nsc_voice& c = v[0];
				const float np = std::min(0.7f, c.pressure + 0.05f);
				c.pressure_rate = np - c.pressure;
				c.pressure = np;
				c.pitch = 60.0f;
				c.gate = 1;
				c.velocity = 0.5f;
			}
			comp().process_voices(in.state(), v.data(), &g, o.data());
			all.insert(all.end(), buf.begin(), buf.end());
		}
		CHECK(in.guards_intact());
		return all;
	};
	float rest = 0.0f, pressed = 0.0f;
	const std::vector<float> a = run(false), b = run(true);
	for (float x : a) rest = std::max(rest, std::fabs(x));
	for (float x : b) pressed = std::max(pressed, std::fabs(x));
	CAPTURE(id());
	CHECK(rest < 1e-5f);
	CHECK(pressed > 1e-3f);
	CHECK(std::isfinite(pressed));
	CHECK(same_bits(b, run(true)));
}

TEST_CASE("hash: the standard performance's render hash (FNV-1a 64 of the float bits)") {
	const int rate = base_rate();
	Instance in;
	REQUIRE(in.init(rate) == 0);
	const Rendered r = render(in, standard(rate), frames_of(perf_seconds(), rate));
	char hex[17];
	std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(fnv64(r.out)));
	MESSAGE(id() << " at " << rate << " Hz, " << nvoices() << " voices, its default preset: hash " << hex);
	if (!g_expect.empty()) CHECK(g_expect == hex);
}

int main(int argc, char** argv) {
	std::string library;
	bool query = false;
	std::vector<char*> rest;
	for (int i = 0; i < argc; i++) {
		const std::string a = argv[i];
		if (i > 0 && a.rfind("--known=", 0) == 0) g_known = a.substr(8);
		else if (i > 0 && a.rfind("--expect=", 0) == 0) g_expect = a.substr(9);
		else if (i > 0 && !a.empty() && a[0] != '-' && library.empty()) library = a;
		else {
			if (a.find("list") != std::string::npos || a.find("count") != std::string::npos || a == "-h" || a == "--help" || a == "-?") query = true;
			rest.push_back(argv[i]);
		}
	}
	doctest::Context ctx;
	ctx.applyCommandLine(static_cast<int>(rest.size()), rest.data());
	if (library.empty()) {
		if (query) return ctx.run();
		std::fprintf(stderr, "usage: awva_conformance <component library> [--known=TAG,...] [--expect=HASH] [doctest options]\n");
		return 2;
	}
	const int loaded = nsc_load(library.c_str(), &g_lib);
	if (loaded != 0) {
		std::fprintf(stderr, "awva_conformance: %s: %s\n", library.c_str(), loaded == -1 ? "no such library" : loaded == -2 ? "no nsc_entry" :
		                                                                                                                      "not a component of this ABI");
		return 2;
	}
	const int r = ctx.run();
	nsc_unload(&g_lib);
	return r;
}
