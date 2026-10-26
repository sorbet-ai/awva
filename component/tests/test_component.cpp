// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
//
// The components from their libraries (component/CMakeLists.txt) against the engines linked in:
// they load, their descriptor and manifest are the engine's, process_voices() renders bit for bit
// what the engine renders (the scripted performances and the key recordings, through every
// preset of accordion and trumpet), and process() with UMP renders bit for bit the SDK's MIDI
// side (MidiVoices) driving the engine on the same 2 ms integer-frame step grid.
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

#include "doctest/doctest.h"
#include "nsc/load.h"
#include "nsc/no_denormals.h"
#include "awva_sdk/engines.h"
#include "awva_sdk/midi_voices.h"
#include "awva_sdk/performance.h"
#include "awva_sdk/smf.h"
#include "awva_sdk/presets.h"

namespace {

namespace sdk = awva::sdk;
namespace dsp = awva;
constexpr int SR = 48000, NV = 10;

std::string lib_path(const char* id) {
#if defined(_WIN32)
	return std::string(NSC_COMPONENT_DIR) + "/" + id + ".dll";
#elif defined(__APPLE__)
	return std::string(NSC_COMPONENT_DIR) + "/" + id + ".dylib";
#else
	return std::string(NSC_COMPONENT_DIR) + "/" + id + ".so";
#endif
}

struct Loaded {
	nsc_library lib{};
	nsc_instance in{};
	explicit Loaded(const char* id) {
		REQUIRE(nsc_load(lib_path(id).c_str(), &lib) == 0);
		REQUIRE(nsc_instance_new(lib.c, &in) == 0);
	}
	~Loaded() {
		nsc_instance_free(&in);
		nsc_unload(&lib);
	}
	[[nodiscard]] const nsc_component& c() const { return *lib.c; }
};

bool same(const std::vector<float>& a, const std::vector<float>& b) {
	return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

// one engine through a player: linked in and from its library (process_voices), seconds long
// (one player for both: the scripted performances keep state of their own between calls)
void render_both(const char* id, int preset, sdk::Player& player, double seconds) {
	const sdk::EngineEntry* e = sdk::find_engine(id);
	REQUIRE(e != nullptr);
	sdk::Engine linked(*e);
	linked.init(SR, NV);
	Loaded dll(id);
	dll.c().init(dll.in.state, dll.in.fast, dll.in.large, SR, NV);
	if (preset >= 0) {
		linked.set_preset(preset);
		dll.c().set_preset(dll.in.state, preset);
	}
	std::vector<dsp::VoiceControls> va(NV, dsp::voice_at_rest), vb(NV, dsp::voice_at_rest);
	dsp::GlobalControls ga = dsp::globals_at_rest, gb = dsp::globals_at_rest;
	const int blocks = static_cast<int>(seconds * SR / NSC_BLOCK);
	std::vector<float> a(static_cast<size_t>(blocks) * NSC_BLOCK), b(a.size());
	for (int k = 0; k < blocks; k++) {
		player.block(k, va.data(), ga);
		for (auto& c : va) dsp::track_key(c);
		vb = va;
		gb = ga;
		float* oa[1] = {&a[static_cast<size_t>(k) * NSC_BLOCK]};
		float* ob[1] = {&b[static_cast<size_t>(k) * NSC_BLOCK]};
		{
			const nsc::ScopedNoDenormals nd; // (as the library runs it: no_denormals.h)
			linked.process(va.data(), ga, oa);
		}
		dll.c().process_voices(dll.in.state, reinterpret_cast<const nsc_voice*>(vb.data()), reinterpret_cast<const nsc_globals*>(&gb), ob);
	}
	INFO(id << " preset " << preset << " " << player.describe());
	CHECK(same(a, b));
	double e2 = 0;
	for (float x : a) e2 += static_cast<double>(x) * x;
	CHECK(e2 > 0.0); // (it played)
}

std::vector<std::string> takes() {
	std::vector<std::string> t;
	for (const auto& f : std::filesystem::directory_iterator(AWVA_REC_DIR))
		if (f.path().extension() == ".csv") t.push_back(f.path().string());
	return t;
}

} // namespace

TEST_CASE("every engine's library loads, and its descriptor and manifest are the engine's") {
	for (const sdk::EngineEntry* e : sdk::engines()) {
		Loaded dll(e->info->name);
		const nsc_desc& d = *dll.c().desc;
		INFO(e->info->name);
		CHECK(d.magic == NSC_MAGIC);
		CHECK(d.abi == NSC_ABI);
		CHECK(std::strcmp(d.id, e->info->name) == 0);
		CHECK(std::string(d.uid) == std::string("com.sorbet-ai.awva.") + e->info->name);
		CHECK(d.num_params == e->info->num_params + 1); // (the engine's, then the output level)
		CHECK(std::strcmp(d.params[d.num_params - 1].name, "output") == 0);
		CHECK(d.min_rate == 11025);
		CHECK(d.max_rate == 192000);
		CHECK(d.num_presets == e->info->num_presets);
		CHECK(d.fast_bytes == e->info->fast_bytes);
		CHECK(d.large_bytes == e->info->large_bytes);
		CHECK(d.state_bytes >= e->info->state_bytes);
		for (int i = 0; i < e->info->num_params; i++) {
			CHECK(std::strcmp(d.params[i].name, e->param_desc(i)->name) == 0);
			CHECK(dll.c().param_index(d.params[i].id) == i);
		}
		CHECK(dll.c().param_index(0x12345678u) == -1);
		for (int i = 0; i < d.num_presets; i++) CHECK(std::strcmp(d.presets[i].name, e->preset_name(i)) == 0);
		CHECK(std::filesystem::exists(std::string(NSC_COMPONENT_DIR) + "/" + d.id + ".json"));
	}
}

TEST_CASE("accordion and trumpet from their libraries render bit for bit as linked in (every preset, the performances, the takes)") {
	for (const char* id : {"accordion", "trumpet"}) {
		const sdk::EngineEntry* e = sdk::find_engine(id);
		REQUIRE(e != nullptr);
		const int presets = e->info->num_presets > 0 ? e->info->num_presets : 1;
		for (int p = 0; p < presets; p++) {
			for (const char* perf : {"default", "stale", id}) {
				std::string err;
				auto pa = sdk::scripted_player(perf, NV, SR, &err);
				if (!pa) continue; // (no performance of that name)
				render_both(id, e->info->num_presets > 0 ? p : -1, *pa, 6.0);
			}
		}
		for (const std::string& t : takes()) {
			std::string err;
			auto pa = sdk::key_player(t.c_str(), NV, SR, &err);
			REQUIRE(pa != nullptr);
			render_both(id, -1, *pa, 8.0);
		}
	}
}

namespace {

// a MIDI 2.0 performance: notes with velocities, pressure, bends, CC74, releases (frame, two words)
struct Ev {
	uint64_t frame;
	uint32_t w0, w1;
};
std::vector<Ev> ump_performance() {
	std::vector<Ev> v;
	const int keys[4] = {60, 64, 67, 72};
	for (int n = 0; n < 4; n++) {
		const uint8_t ch = static_cast<uint8_t>(1 + n);
		const uint64_t on = 2400u * static_cast<uint64_t>(n) + 13u * static_cast<uint64_t>(n);
		v.push_back({on, 0x40900000u | ch << 16 | static_cast<uint32_t>(keys[n]) << 8, (0x6000u + 0x1000u * static_cast<uint32_t>(n)) << 16});
		for (int k = 1; k < 40; k++) {
			const uint64_t f = on + 480u * static_cast<uint64_t>(k) + 7u;
			v.push_back({f, 0x40d00000u | ch << 16, static_cast<uint32_t>(0x20000000u + 0x02000000u * static_cast<uint32_t>(k % 20))});
			v.push_back({f + 3, 0x40e00000u | ch << 16, static_cast<uint32_t>(0x80000000u + 0x00400000u * static_cast<uint32_t>(k))});
			v.push_back({f + 5, 0x40b04a00u | ch << 16, static_cast<uint32_t>(0x10000000u * static_cast<uint32_t>(k % 16))});
		}
		v.push_back({on + 24000u, 0x40800000u | ch << 16 | static_cast<uint32_t>(keys[n]) << 8, 0x40000000u});
	}
	std::sort(v.begin(), v.end(), [](const Ev& a, const Ev& b) { return a.frame < b.frame; });
	return v;
}

std::vector<float> render_ump(const char* id, bool legato, bool from_library) {
	const std::vector<Ev> perf = ump_performance();
	const int blocks = 2 * SR / NSC_BLOCK;
	std::vector<float> out(static_cast<size_t>(blocks) * NSC_BLOCK);
	if (from_library) {
		Loaded dll(id);
		dll.c().init(dll.in.state, dll.in.fast, dll.in.large, SR, NV);
		if (legato) REQUIRE(dll.c().mode(dll.in.state, NSC_MODE_LEGATO, 1) == 1);
		size_t k = 0;
		for (int b = 0; b < blocks; b++) {
			std::vector<nsc_event> ev;
			const uint64_t start = static_cast<uint64_t>(b) * NSC_BLOCK;
			for (; k < perf.size() && perf[k].frame < start + NSC_BLOCK; k++)
				ev.push_back({static_cast<int32_t>(perf[k].frame - start), -1, {perf[k].w0, perf[k].w1, 0, 0}});
			float* o[1] = {&out[start]};
			dll.c().process(dll.in.state, ev.data(), static_cast<int32_t>(ev.size()), o);
		}
		return out;
	}
	// the reference: the SDK's MidiVoices and the engine linked in, the same step grid
	sdk::Engine e(*sdk::find_engine(id));
	e.init(SR, NV);
	auto mv = std::make_unique<sdk::MidiVoices>(NV);
	mv->set_legato(legato);
	std::vector<dsp::VoiceControls> vc(NV, dsp::voice_at_rest);
	size_t k = 0;
	uint64_t steps = 0;
	for (int b = 0; b < blocks; b++) {
		const uint64_t start = static_cast<uint64_t>(b) * NSC_BLOCK;
		for (; (steps * SR + 499) / 500 <= start; steps++) {
			const uint64_t fs = (steps * SR + 499) / 500;
			for (; k < perf.size() && perf[k].frame <= fs; k++) (void)mv->ump2(perf[k].w0, perf[k].w1);
			mv->step();
		}
		for (int v = 0; v < NV; v++) {
			const dsp::VoiceControls& m = mv->voices()[v];
			vc[v].note = m.note;
			vc[v].pres = m.pres;
			vc[v].vpres = m.vpres;
			vc[v].but_x = m.but_x;
			vc[v].but_y = m.but_y;
			dsp::track_key(vc[v]);
		}
		float* o[1] = {&out[start]};
		{
			const nsc::ScopedNoDenormals nd; // (as the library runs it)
			e.process(vc.data(), mv->globals(), o);
		}
	}
	return out;
}

} // namespace

TEST_CASE("UMP in through the component's own MIDI side, bit for bit the SDK's (polyphonic and mono legato)") {
	for (const char* id : {"accordion", "trumpet", "va"}) {
		for (bool legato : {false, true}) {
			INFO(id << (legato ? " legato" : " poly"));
			const std::vector<float> a = render_ump(id, legato, false), b = render_ump(id, legato, true);
			CHECK(same(a, b));
			double e2 = 0;
			for (float x : b) e2 += static_cast<double>(x) * x;
			CHECK(e2 > 0.0);
		}
	}
}

TEST_CASE("parameters by index and id, the write rule, reset and sample rates") {
	Loaded dll("accordion");
	const nsc_component& c = dll.c();
	CHECK(c.init(dll.in.state, dll.in.fast, dll.in.large, 8000, NV) == -1); // (outside min_rate .. max_rate)
	REQUIRE(c.init(dll.in.state, dll.in.fast, dll.in.large, 44100, NV) == 0);
	const nsc_desc& d = *c.desc;
	REQUIRE(d.num_params > 0);
	const float before = c.param(dll.in.state, 0);
	CHECK(c.set_param(dll.in.state, 0, std::nanf("")) == 0); // NaN ignored
	CHECK(c.param(dll.in.state, 0) == before);
	CHECK(c.set_param(dll.in.state, d.num_params, 1.0f) == 0);
	CHECK(c.set_param(dll.in.state, 0, d.params[0].max) == 1);
	CHECK(c.param(dll.in.state, 0) == d.params[0].max);
	float buf[NSC_BLOCK];
	float* o[1] = {buf};
	const nsc_event on = {0, -1, {0x40903c00u, 0xc0000000u, 0, 0}};
	for (int b = 0; b < 200; b++) c.process(dll.in.state, b == 0 ? &on : nullptr, b == 0 ? 1 : 0, o);
	CHECK(c.active_voices(dll.in.state) > 0);
	c.reset(dll.in.state);
	CHECK(c.active_voices(dll.in.state) == 0);
	CHECK(c.param(dll.in.state, 0) == d.params[0].max); // (kept)
}

namespace {

// the default performance with the instrument moving: shaken (acc_abs, 6 Hz, 1-3 s), tilted and
// turned (acc_x/y/z and rot_x/y/z, slow), and the pedal pressed and released (2-4 s)
class Moving final : public sdk::Player {
public:
	explicit Moving(std::unique_ptr<sdk::Player> p) :
	    p_(std::move(p)) {}
	void block(int b, dsp::VoiceControls* vc, dsp::GlobalControls& g) override {
		p_->block(b, vc, g);
		const double t = static_cast<double>(b) * NSC_BLOCK / SR;
		const double s = std::sin(2.0 * 3.14159265358979 * 6.0 * t);
		g.acc_abs = static_cast<float>(t > 1.0 && t < 3.0 ? 1.0 + 1.3 * s * s : 1.0);
		g.acc_x = static_cast<float>(0.3 * std::sin(1.1 * t));
		g.acc_y = static_cast<float>(0.2 * std::sin(0.7 * t));
		g.acc_z = static_cast<float>(-1.0 + 0.1 * std::sin(0.5 * t));
		g.rot_x = static_cast<float>(0.4 * std::sin(1.7 * t));
		g.rot_y = static_cast<float>(0.3 * std::sin(1.3 * t));
		g.rot_z = static_cast<float>(0.2 * std::sin(0.9 * t));
		const double pd = t < 2.0 ? 0.0 : (t < 3.0 ? t - 2.0 : (t < 4.0 ? 4.0 - t : 0.0));
		g.pedal = static_cast<float>(3.0 / 16 + (13.0 / 16) * pd);
	}
	[[nodiscard]] double seconds() const override { return p_->seconds(); }
	[[nodiscard]] std::string describe() const override { return "moving (shake, tilt, turn, pedal) " + p_->describe(); }

private:
	std::unique_ptr<sdk::Player> p_;
};

// one engine through a player, linked in: its output
std::vector<float> render_linked(const char* id, int preset, sdk::Player& player, double seconds) {
	sdk::Engine linked(*sdk::find_engine(id));
	linked.init(SR, NV);
	if (preset >= 0) linked.set_preset(preset);
	std::vector<dsp::VoiceControls> v(NV, dsp::voice_at_rest);
	dsp::GlobalControls g = dsp::globals_at_rest;
	const int blocks = static_cast<int>(seconds * SR / NSC_BLOCK);
	std::vector<float> a(static_cast<size_t>(blocks) * NSC_BLOCK);
	for (int k = 0; k < blocks; k++) {
		player.block(k, v.data(), g);
		for (auto& c : v) dsp::track_key(c);
		float* o[1] = {&a[static_cast<size_t>(k) * NSC_BLOCK]};
		linked.process(v.data(), g, o);
	}
	return a;
}

} // namespace

TEST_CASE("every engine's library renders bit for bit as linked in with the globals moving (shake, motion, pedal)") {
	for (const sdk::EngineEntry* e : sdk::engines()) {
		const char* id = e->info->name;
		const int np = e->info->num_presets;
		for (int p : {0, np > 0 ? np - 1 : 0}) {
			const int preset = np > 0 ? p : -1;
			std::string err;
			Moving moving(sdk::scripted_player("default", NV, SR, &err));
			render_both(id, preset, moving, 5.0);
			// were the globals live? (reported: the contract checks that every declared input counts)
			if (e->info->inputs & (dsp::in_shake | dsp::in_accel | dsp::in_rotation | dsp::in_pedal)) {
				auto still = sdk::scripted_player("default", NV, SR, &err);
				Moving moved(sdk::scripted_player("default", NV, SR, &err));
				const bool moves = !same(render_linked(id, preset, *still, 5.0), render_linked(id, preset, moved, 5.0));
				const std::string what = std::string(id) + " preset " + std::to_string(preset) + (moves ? ": the globals change its sound" : ": the globals make no difference here");
				MESSAGE(what);
			}
			if (np <= 1) break;
		}
	}
}

TEST_CASE("declared defaults: every parameter's default is its value after init()") {
	for (const sdk::EngineEntry* e : sdk::engines()) {
		Loaded dll(e->info->name);
		dll.c().init(dll.in.state, dll.in.fast, dll.in.large, SR, NV);
		const nsc_desc& d = *dll.c().desc;
		for (int i = 0; i < d.num_params; i++) {
			INFO(e->info->name << " " << d.params[i].name);
			const float v = dll.c().param(dll.in.state, i);
			CHECK(std::bit_cast<uint32_t>(v) == std::bit_cast<uint32_t>(d.params[i].def)); // bit for bit
		}
	}
}

TEST_CASE("ended: a note's host id comes back once it has ended (its tail, or the engine idle), not before") {
	Loaded dll("va");
	const nsc_component& c = dll.c();
	c.init(dll.in.state, dll.in.fast, dll.in.large, SR, NV);
	float buf[NSC_BLOCK];
	float* o[1] = {buf};
	int32_t ids[8];
	const nsc_event on[2] = {{0, 101, {0x40903c00u, 0xc0000000u, 0, 0}}, {5, 102, {0x40914000u, 0xc0000000u, 0, 0}}};
	c.process(dll.in.state, on, 2, o);
	for (int b = 0; b < 100; b++) c.process(dll.in.state, nullptr, 0, o);
	CHECK(c.ended(dll.in.state, ids, 8) == 0);                       // (held)
	const nsc_event off = {0, -1, {0x40803c00u, 0x40000000u, 0, 0}}; // note 60 off
	c.process(dll.in.state, &off, 1, o);
	CHECK(c.ended(dll.in.state, ids, 8) == 0); // (released: its tail)
	int got = 0;
	for (int b = 0; b < 48000 * 12 / NSC_BLOCK && got == 0; b++) {
		c.process(dll.in.state, nullptr, 0, o);
		got = c.ended(dll.in.state, ids, 8);
	}
	REQUIRE(got == 1);
	CHECK(ids[0] == 101);
	// the other: a new note on its key takes it at once
	const nsc_event again = {0, 103, {0x40914000u, 0xc0000000u, 0, 0}};
	c.process(dll.in.state, &again, 1, o);
	for (int b = 0; b < 10; b++) c.process(dll.in.state, nullptr, 0, o);
	REQUIRE(c.ended(dll.in.state, ids, 8) == 1);
	CHECK(ids[0] == 102);
}

namespace {

// the notes' host ids that come back: ended() drained after every block, as the guarantee asks
struct Ids {
	std::vector<int> count; // by id
	explicit Ids(int n) :
	    count(static_cast<size_t>(n), 0) {}
	int drain(const nsc_component& c, void* state) {
		int32_t ids[64];
		int got = 0, n = 0;
		do {
			n = c.ended(state, ids, 64);
			for (int i = 0; i < n; i++) {
				REQUIRE(ids[i] >= 0);
				REQUIRE(ids[i] < static_cast<int>(count.size()));
				count[static_cast<size_t>(ids[i])]++;
			}
			got += n;
		} while (n == 64);
		return got;
	}
	[[nodiscard]] bool each_once() const {
		return std::all_of(count.begin(), count.end(), [](int k) { return k == 1; });
	}
};

nsc_event ump_note(uint32_t frame, bool on, int i, int32_t id) {
	const uint32_t ch = static_cast<uint32_t>(i % 16), key = static_cast<uint32_t>(i / 16);
	return {static_cast<int32_t>(frame), id, {(on ? 0x40900000u : 0x40800000u) | ch << 16 | key << 8, on ? 0xc0000000u : 0x40000000u, 0, 0}};
}

} // namespace

TEST_CASE("ended: 200 overlapping notes on 10 voices, more than the ids tracked at once: every id back exactly once") {
	Loaded dll("va"); // (its tail is long: the released notes stay tracked, the table overflows)
	const nsc_component& c = dll.c();
	c.init(dll.in.state, dll.in.fast, dll.in.large, SR, NV);
	constexpr int N = 200, EVERY = 6, HOLD = 600; // a note every 6 blocks, held 600: 100 at once
	static_assert(N > NSC_MAX_NOTE_IDS && HOLD / EVERY > NSC_MAX_NOTE_IDS);
	Ids back(N);
	float buf[NSC_BLOCK];
	float* o[1] = {buf};
	int early = 0;
	const int last = (N - 1) * EVERY + HOLD;
	for (int b = 0; b <= last + 15 * SR / NSC_BLOCK; b++) {
		std::vector<nsc_event> ev;
		if (b % EVERY == 0 && b / EVERY < N) ev.push_back(ump_note(static_cast<uint32_t>(b % NSC_BLOCK), true, b / EVERY, b / EVERY));
		if (b >= HOLD && (b - HOLD) % EVERY == 0 && (b - HOLD) / EVERY < N) ev.push_back(ump_note(3, false, (b - HOLD) / EVERY, -1));
		c.process(dll.in.state, ev.data(), static_cast<int32_t>(ev.size()), o);
		const int got = back.drain(c, dll.in.state);
		if (b < HOLD) early += got;
		if (b > last && c.active_voices(dll.in.state) == 0 && back.each_once()) break;
	}
	CHECK(early > 0); // (the overflow: the oldest ended before any release)
	CHECK(back.each_once());
	CHECK(back.drain(c, dll.in.state) == 0);
}

TEST_CASE("ended: Note Ons the queue can't take come back at once, and the rest once each") {
	Loaded dll("va");
	const nsc_component& c = dll.c();
	c.init(dll.in.state, dll.in.fast, dll.in.large, SR, NV);
	constexpr int Q = 1024, MORE = 500; // the queue's size, then more than it takes
	Ids back(Q + MORE);
	float buf[NSC_BLOCK];
	float* o[1] = {buf};
	std::vector<nsc_event> ev;
	ev.reserve(Q);
	for (int i = 0; i < Q; i++) ev.push_back(ump_note(31, true, i, i)); // block 0, frame 31: waits for the step at 96
	c.process(dll.in.state, ev.data(), Q, o);
	CHECK(back.drain(c, dll.in.state) == 0);
	ev.clear();
	for (int i = Q; i < Q + MORE; i++) ev.push_back(ump_note(31, true, i, i)); // block 1: the queue is full
	c.process(dll.in.state, ev.data(), MORE, o);
	CHECK(back.drain(c, dll.in.state) == MORE); // (they will never sound)
	for (int b = 2; b < 8; b++) {
		c.process(dll.in.state, nullptr, 0, o); // the step at 96 takes the 1024: all but 64 end at once
		back.drain(c, dll.in.state);
	}
	ev.clear();
	for (int i = 0; i < Q; i++) ev.push_back(ump_note(0, false, i, -1));
	c.process(dll.in.state, ev.data(), Q, o);
	for (int b = 0; b < 15 * SR / NSC_BLOCK && !back.each_once(); b++) {
		back.drain(c, dll.in.state);
		c.process(dll.in.state, nullptr, 0, o);
	}
	back.drain(c, dll.in.state);
	CHECK(back.each_once());
}

TEST_CASE("the output level: at its default the samples are the engine's, at 0.5 half of them") {
	Loaded a("ep"), b("ep");
	REQUIRE(a.c().init(a.in.state, a.in.fast, a.in.large, SR, NV) == 0);
	REQUIRE(b.c().init(b.in.state, b.in.fast, b.in.large, SR, NV) == 0);
	const int out = a.c().param_index(a.c().desc->params[a.c().desc->num_params - 1].id); // ("output")
	REQUIRE(out == a.c().desc->num_params - 1);
	CHECK(b.c().set_param(b.in.state, out, 0.5f) == 1);
	const nsc_event on = {0, -1, {0x40903c00u, 0xc0000000u, 0, 0}};
	float x[NSC_BLOCK], y[NSC_BLOCK];
	float* ox[1] = {x};
	float* oy[1] = {y};
	bool sounded = false;
	for (int k = 0; k < 400; k++) {
		a.c().process(a.in.state, k == 0 ? &on : nullptr, k == 0 ? 1 : 0, ox);
		b.c().process(b.in.state, k == 0 ? &on : nullptr, k == 0 ? 1 : 0, oy);
		for (int n = 0; n < NSC_BLOCK; n++) {
			REQUIRE(y[n] == x[n] * 0.5f);
			sounded |= x[n] != 0.0f;
		}
	}
	CHECK(sounded);
}

TEST_CASE("a late event (a negative frame, before its block) lands where it would have on time") {
	Loaded a("va"), b("va");
	REQUIRE(a.c().init(a.in.state, a.in.fast, a.in.large, 44100, NV) == 0);
	REQUIRE(b.c().init(b.in.state, b.in.fast, b.in.large, 44100, NV) == 0);
	// a note at frame 40 at 44.1 kHz (step 1 is at frame 89): in time, at frame 8 of block 1, or
	// late, at frame -24 of block 2
	const nsc_event in_time = {8, -1, {0x40903c00u, 0xc0000000u, 0, 0}};
	const nsc_event late = {-24, -1, {0x40903c00u, 0xc0000000u, 0, 0}};
	std::vector<float> x(NSC_BLOCK * 300), y(x.size());
	for (int k = 0; k < 300; k++) {
		float* ox[1] = {&x[static_cast<size_t>(k) * NSC_BLOCK]};
		float* oy[1] = {&y[static_cast<size_t>(k) * NSC_BLOCK]};
		a.c().process(a.in.state, k == 1 ? &in_time : nullptr, k == 1 ? 1 : 0, ox);
		b.c().process(b.in.state, k == 2 ? &late : nullptr, k == 2 ? 1 : 0, oy);
	}
	CHECK(x == y);
}

namespace {
// presets/<engine>/<NN>-*.preset.json, "" if none
std::string factory_file(const char* engine, int i) {
	char nn[16];
	std::snprintf(nn, sizeof nn, "%02d-", i);
	const std::filesystem::path d = std::filesystem::path(AWVA_PRESETS_DIR) / engine;
	if (!std::filesystem::is_directory(d)) return "";
	for (const auto& f : std::filesystem::directory_iterator(d))
		if (f.path().filename().string().rfind(nn, 0) == 0) {
			std::ifstream in(f.path(), std::ios::binary);
			std::stringstream ss;
			ss << in.rdbuf();
			return ss.str();
		}
	return "";
}
} // namespace

// load_preset / save_preset: the preset files through the ABI. Each factory file loads exact, to
// the values set_preset gives the engine linked in; what a component saves the SDK loads exact;
// another engine's preset and a text that isn't one are refused with nothing changed. (The
// component's own parameter, output, is not a preset's.)
TEST_CASE("presets as text through the ABI: the factory files, saved and loaded, refused") {
	for (const sdk::EngineEntry* e : sdk::engines()) {
		if (e->info->num_params <= 0) continue;
		INFO(e->info->name);
		Loaded dll(e->info->name);
		const nsc_component& c = dll.c();
		c.init(dll.in.state, dll.in.fast, dll.in.large, SR, NV);
		for (int i = 0; i < e->info->num_presets; i++) {
			CAPTURE(i);
			const std::string json = factory_file(e->info->name, i);
			REQUIRE(!json.empty());
			nsc_preset_report r{};
			REQUIRE(c.load_preset(dll.in.state, json.data(), static_cast<uint32_t>(json.size()), &r) == 1);
			CHECK(r.loaded == 1);
			CHECK(r.exact == 1);
			CHECK(r.clamped + r.skipped + r.missing == 0);
			CHECK(r.error == nullptr);
			CHECK(c.preset(dll.in.state) == -1); // (a preset by name)
			sdk::Engine linked(*e);
			linked.init(SR, NV);
			linked.set_preset(i);
			for (int k = 0; k < e->info->num_params; k++) {
				const float a = c.param(dll.in.state, k), b = *linked.param(k);
				CHECK(std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b));
			}
		}
		// edited, saved by the component, loaded by the SDK: exact, the same values
		for (int k = 0; k < e->info->num_params; k += 3) {
			const nsc_param& p = c.desc->params[k];
			c.set_param(dll.in.state, k, p.min + 0.37f * (p.max - p.min));
		}
		std::vector<char> out(8192);
		const uint32_t n = c.save_preset(dll.in.state, "edited", out.data(), static_cast<uint32_t>(out.size()));
		REQUIRE(n > 0);
		CHECK(std::strlen(out.data()) == n);
		CHECK(c.save_preset(dll.in.state, "edited", out.data(), 64) == 0); // (didn't fit)
		REQUIRE(c.save_preset(dll.in.state, "edited", out.data(), static_cast<uint32_t>(out.size())) == n);
		sdk::Engine linked(*e);
		linked.init(SR, NV);
		const sdk::PresetReport rep = sdk::load_preset_json(linked, std::string_view(out.data(), n));
		REQUIRE(rep.loaded);
		CHECK(rep.exact);
		for (int k = 0; k < e->info->num_params; k++) {
			const float a = c.param(dll.in.state, k), b = *linked.param(k);
			CHECK(std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b));
		}
		// refused: another engine's, and not a preset; nothing changed
		const std::string other = std::strcmp(e->info->name, "va") == 0 ? factory_file("ep", 0) : factory_file("va", 0);
		const float before = c.param(dll.in.state, 0);
		nsc_preset_report r{};
		CHECK(c.load_preset(dll.in.state, other.data(), static_cast<uint32_t>(other.size()), &r) == 0);
		CHECK(r.loaded == 0);
		REQUIRE(r.error != nullptr);
		CHECK(std::string(r.error) == "another engine's preset");
		const char junk[] = "{\"format\": \"awva-preset/1\", \"params\": {\"x\": }";
		CHECK(c.load_preset(dll.in.state, junk, sizeof junk - 1, &r) == 0);
		CHECK(r.error != nullptr);
		CHECK(c.load_preset(dll.in.state, nullptr, 0, nullptr) == 0);
		CHECK(c.param(dll.in.state, 0) == before);
	}
}

TEST_CASE("a late frame in the very first block is frame 0 (nothing before the start)") {
	Loaded a("va"), b("va");
	REQUIRE(a.c().init(a.in.state, a.in.fast, a.in.large, SR, NV) == 0);
	REQUIRE(b.c().init(b.in.state, b.in.fast, b.in.large, SR, NV) == 0);
	const nsc_event at0 = {0, -1, {0x40903c00u, 0xc0000000u, 0, 0}};
	const nsc_event early = {-32, -1, {0x40903c00u, 0xc0000000u, 0, 0}};
	std::vector<float> x(NSC_BLOCK * 200), y(x.size());
	for (int k = 0; k < 200; k++) {
		float* ox[1] = {&x[static_cast<size_t>(k) * NSC_BLOCK]};
		float* oy[1] = {&y[static_cast<size_t>(k) * NSC_BLOCK]};
		a.c().process(a.in.state, k == 0 ? &at0 : nullptr, k == 0 ? 1 : 0, ox);
		b.c().process(b.in.state, k == 0 ? &early : nullptr, k == 0 ? 1 : 0, oy);
	}
	CHECK(x == y);
}

TEST_CASE("every engine at 11025 Hz (min_rate): finite, within its memory regions; the peak, recorded") {
	constexpr int RATE = 11025, GUARD = 4096;
	for (const sdk::EngineEntry* e : sdk::engines()) {
		nsc_library lib{};
		REQUIRE(nsc_load(lib_path(e->info->name).c_str(), &lib) == 0);
		const nsc_desc& d = *lib.c->desc;
		// the regions with a guard after each: a write past one shows there
		std::vector<std::vector<uint8_t>> mem;
		void* region[3];
		const uint32_t sizes[3] = {d.state_bytes, d.fast_bytes, d.large_bytes};
		for (int r = 0; r < 3; r++) {
			mem.emplace_back(sizes[r] + GUARD + 64, uint8_t{0});
			uint8_t* base = mem.back().data();
			region[r] = base + (64 - reinterpret_cast<uintptr_t>(base) % 64) % 64; // (64-byte aligned)
			std::memset(static_cast<uint8_t*>(region[r]) + sizes[r], 0xa5, GUARD);
		}
		REQUIRE(lib.c->init(region[0], region[1], region[2], RATE, NV) == 0);
		// notes across the range, struck, pressed, bent, released (8 s)
		float peak = 0.0f;
		bool finite = true;
		float buf[NSC_BLOCK];
		float* o[1] = {buf};
		const int blocks = 8 * RATE / NSC_BLOCK;
		for (int b = 0; b < blocks; b++) {
			std::vector<nsc_event> ev;
			if (b % 40 == 0 && b < blocks - 400) {
				const uint32_t key = 36u + static_cast<uint32_t>((b / 40) * 5 % 60), ch = static_cast<uint32_t>(1 + (b / 40) % 8);
				ev.push_back({0, -1, {0x40900000u | ch << 16 | key << 8, 0xf0000000u, 0, 0}});
				ev.push_back({4, -1, {0x40d00000u | ch << 16, 0xffffffffu, 0, 0}});
				ev.push_back({8, -1, {0x40e00000u | ch << 16, 0x90000000u, 0, 0}});
			}
			if (b % 40 == 30 && b < blocks - 400) {
				const uint32_t key = 36u + static_cast<uint32_t>((b / 40) * 5 % 60), ch = static_cast<uint32_t>(1 + (b / 40) % 8);
				ev.push_back({0, -1, {0x40800000u | ch << 16 | key << 8, 0x80000000u, 0, 0}});
			}
			lib.c->process(region[0], ev.data(), static_cast<int32_t>(ev.size()), o);
			for (float s : buf) {
				finite = finite && std::isfinite(s);
				peak = std::fmax(peak, std::fabs(s));
			}
		}
		bool guards = true;
		for (int r = 0; r < 3; r++)
			for (int i = 0; i < GUARD; i++) guards = guards && static_cast<uint8_t*>(region[r])[sizes[r] + i] == 0xa5;
		MESSAGE("11025 Hz: " << std::string(e->info->name) << " peak " << peak << (finite ? "" : " NON-FINITE") << (guards ? "" : " OUT OF ITS REGIONS"));
		CHECK(finite);
		CHECK(guards);
		nsc_unload(&lib);
	}
}

namespace {

// a MIDI 1.0 performance with its events off the step grid and off block starts: MPE notes on
// member channels 2..5 with bends, channel pressure and CC74, a master-channel note with poly
// pressure, the pedal (seconds)
std::vector<sdk::MidiEvent> late_performance() {
	std::vector<sdk::MidiEvent> ev;
	auto add = [&](double t, int a, int b, int c, int len = 3) {
		ev.push_back({t, {static_cast<uint8_t>(a), static_cast<uint8_t>(b), static_cast<uint8_t>(c)}, static_cast<uint8_t>(len)});
	};
	const int keys[4] = {53, 57, 60, 64};
	for (int n = 0; n < 4; n++) {
		const int ch = 1 + n;
		const double on = 0.1 + 0.37 * n + 0.00013 * n, off = on + 1.2;
		add(on, 0xb0 | ch, 74, 64);
		add(on, 0x90 | ch, keys[n], 70 + 12 * n);
		for (int k = 1; k < 48; k++) {
			const double t = on + 0.0247 * k;
			if (t >= off) break;
			add(t, 0xd0 | ch, static_cast<int>(40 + 60 * std::sin(0.11 * k + n)) & 0x7f, 0, 2);
			const int bend = 8192 + static_cast<int>(600 * std::sin(0.2 * k));
			add(t + 0.0041, 0xe0 | ch, bend & 0x7f, (bend >> 7) & 0x7f);
			if (k % 3 == 0) add(t + 0.0093, 0xb0 | ch, 74, (64 + 4 * k) & 0x7f);
		}
		add(off, 0x80 | ch, keys[n], 40);
	}
	add(1.9, 0xb0, 64, 127);
	add(2.0, 0x90, 48, 90);
	for (int k = 0; k < 30; k++) add(2.05 + 0.0301 * k, 0xa0, 48, (20 + 2 * k) & 0x7f);
	add(2.9, 0x80, 48, 0);
	add(3.1, 0xb0, 64, 0);
	std::stable_sort(ev.begin(), ev.end(), [](const sdk::MidiEvent& a, const sdk::MidiEvent& b) { return a.t < b.t; });
	return ev;
}

// awva_render --midi: the file through the SDK's MIDI player and the engine linked in (with the
// library's FTZ guard: the renderer's own output differs from it only below -600 dBFS)
std::vector<float> render_midi_file(const char* id, const std::string& path, int sr, int frames) {
	std::string err;
	auto player = sdk::midi_player(path.c_str(), NV, sr, &err);
	REQUIRE(player != nullptr);
	sdk::Engine e(*sdk::find_engine(id));
	e.init(sr, NV);
	std::vector<dsp::VoiceControls> vc(NV, dsp::voice_at_rest);
	dsp::GlobalControls g = dsp::globals_at_rest;
	std::vector<float> out(static_cast<size_t>(frames));
	for (int b = 0; b * NSC_BLOCK < frames; b++) {
		player->block(b, vc.data(), g);
		for (dsp::VoiceControls& c : vc) dsp::track_key(c);
		float buf[NSC_BLOCK];
		float* o[1] = {buf};
		{
			const nsc::ScopedNoDenormals nd; // (as the library runs it)
			e.process(vc.data(), g, o);
		}
		for (int i = 0; i < NSC_BLOCK && b * NSC_BLOCK + i < frames; i++) out[static_cast<size_t>(b * NSC_BLOCK + i)] = buf[i];
	}
	return out;
}

// a plugin-like host: host blocks of `block` frames (0: random 1..1024), the component's 32-frame
// blocks rendered when their first frame is due, with the events seen so far; an event seen after
// its block was rendered goes to the next one with its negative frame (late: true), or at that
// block's frame 0 (late: false, what a host without negative frames could do)
std::vector<float> render_host(const char* id, const std::vector<sdk::MidiEvent>& perf, int sr, int frames, int block, bool late) {
	Loaded dll(id);
	const nsc_component& c = dll.c();
	c.init(dll.in.state, dll.in.fast, dll.in.large, sr, NV);
	struct Ev {
		uint64_t frame;
		uint32_t w0;
	};
	std::vector<Ev> seen; // events seen, not yet given
	std::vector<float> out(static_cast<size_t>(frames));
	float buf[NSC_BLOCK] = {};
	int pos = NSC_BLOCK;
	uint64_t blocks = 0;
	size_t k = 0;
	uint32_t rng = 12345;
	for (uint64_t at = 0; at < static_cast<uint64_t>(frames);) {
		rng = rng * 1664525u + 1013904223u;
		const uint64_t n = std::min<uint64_t>(block > 0 ? static_cast<uint64_t>(block) : 1 + (rng >> 8) % 1024, frames - at);
		for (; k < perf.size(); k++) { // (the events of this host block: at their frame, as awva_render places them)
			const uint64_t f = perf[k].t <= 0 ? 0u : static_cast<uint64_t>(std::llround(perf[k].t * sr));
			if (f >= at + n) break;
			const uint8_t* m = perf[k].bytes;
			seen.push_back({f, 0x20000000u | static_cast<uint32_t>(m[0]) << 16 | static_cast<uint32_t>(m[1]) << 8 | (perf[k].len > 2 ? m[2] : 0u)});
		}
		for (uint64_t i = at; i < at + n; i++) {
			if (pos == NSC_BLOCK) {
				const uint64_t b0 = blocks * NSC_BLOCK;
				std::vector<nsc_event> ev;
				size_t taken = 0;
				for (; taken < seen.size() && seen[taken].frame < b0 + NSC_BLOCK; taken++) {
					const int64_t f = static_cast<int64_t>(seen[taken].frame) - static_cast<int64_t>(b0);
					ev.push_back({static_cast<int32_t>(late || f >= 0 ? f : 0), -1, {seen[taken].w0, 0, 0, 0}});
				}
				seen.erase(seen.begin(), seen.begin() + static_cast<std::ptrdiff_t>(taken));
				float* o[1] = {buf};
				c.process(dll.in.state, ev.data(), static_cast<int32_t>(ev.size()), o);
				blocks++;
				pos = 0;
			}
			out[i] = buf[pos++];
		}
		at += n;
	}
	return out;
}

} // namespace

TEST_CASE("ended: 200 overlapping notes on 10 voices, more than the ids tracked at once: every id back exactly once") {
	Loaded dll("va"); // (its tail is long: the released notes stay tracked, the table overflows)
	const nsc_component& c = dll.c();
	c.init(dll.in.state, dll.in.fast, dll.in.large, SR, NV);
	constexpr int N = 200, EVERY = 6, HOLD = 600; // a note every 6 blocks, held 600: 100 at once
	static_assert(N > NSC_MAX_NOTE_IDS && HOLD / EVERY > NSC_MAX_NOTE_IDS);
	Ids back(N);
	float buf[NSC_BLOCK];
	float* o[1] = {buf};
	int early = 0;
	const int last = (N - 1) * EVERY + HOLD;
	for (int b = 0; b <= last + 15 * SR / NSC_BLOCK; b++) {
		std::vector<nsc_event> ev;
		if (b % EVERY == 0 && b / EVERY < N) ev.push_back(ump_note(static_cast<uint32_t>(b % NSC_BLOCK), true, b / EVERY, b / EVERY));
		if (b >= HOLD && (b - HOLD) % EVERY == 0 && (b - HOLD) / EVERY < N) ev.push_back(ump_note(3, false, (b - HOLD) / EVERY, -1));
		c.process(dll.in.state, ev.data(), static_cast<int32_t>(ev.size()), o);
		const int got = back.drain(c, dll.in.state);
		if (b < HOLD) early += got;
		if (b > last && c.active_voices(dll.in.state) == 0 && back.each_once()) break;
	}
	CHECK(early > 0); // (the overflow: the oldest ended before any release)
	CHECK(back.each_once());
	CHECK(back.drain(c, dll.in.state) == 0);
}

TEST_CASE("ended: Note Ons the queue can't take come back at once, and the rest once each") {
	Loaded dll("va");
	const nsc_component& c = dll.c();
	c.init(dll.in.state, dll.in.fast, dll.in.large, SR, NV);
	constexpr int Q = 1024, MORE = 500; // the queue's size, then more than it takes
	Ids back(Q + MORE);
	float buf[NSC_BLOCK];
	float* o[1] = {buf};
	std::vector<nsc_event> ev;
	for (int i = 0; i < Q; i++) ev.push_back(ump_note(31, true, i, i)); // block 0, frame 31: waits for the step at 96
	c.process(dll.in.state, ev.data(), Q, o);
	CHECK(back.drain(c, dll.in.state) == 0);
	ev.clear();
	for (int i = Q; i < Q + MORE; i++) ev.push_back(ump_note(31, true, i, i)); // block 1: the queue is full
	c.process(dll.in.state, ev.data(), MORE, o);
	CHECK(back.drain(c, dll.in.state) == MORE); // (they will never sound)
	for (int b = 2; b < 8; b++) {
		c.process(dll.in.state, nullptr, 0, o); // the step at 96 takes the 1024: all but 64 end at once
		back.drain(c, dll.in.state);
	}
	ev.clear();
	for (int i = 0; i < Q; i++) ev.push_back(ump_note(0, false, i, -1));
	c.process(dll.in.state, ev.data(), Q, o);
	for (int b = 0; b < 15 * SR / NSC_BLOCK && !back.each_once(); b++) {
		back.drain(c, dll.in.state);
		c.process(dll.in.state, nullptr, 0, o);
	}
	back.drain(c, dll.in.state);
	CHECK(back.each_once());
}

TEST_CASE("late events: a plugin host at any block size takes every event at awva_render's step, bit for bit (44.1 and 48 kHz)") {
	const std::vector<sdk::MidiEvent> perf = late_performance();
	const std::vector<uint8_t> file = sdk::make_midi(perf);
	for (const int sr : {44100, 48000}) {
		const std::string path = (std::filesystem::temp_directory_path() / ("awva_late_events_" + std::to_string(sr) + ".mid")).string();
		{
			FILE* f = std::fopen(path.c_str(), "wb");
			REQUIRE(f != nullptr);
			REQUIRE(std::fwrite(file.data(), 1, file.size(), f) == file.size());
			std::fclose(f);
		}
		const int frames = sr * 4;
		const sdk::MidiFile parsed = sdk::read_midi_file(path.c_str()); // (the file's times: on its tick grid)
		REQUIRE(parsed.error.empty());
		for (const char* id : {"accordion", "trumpet", "va"}) {
			const std::vector<float> ref = render_midi_file(id, path, sr, frames);
			double e2 = 0;
			for (float x : ref) e2 += static_cast<double>(x) * x;
			CHECK(e2 > 0.0);
			for (const int block : {1, 33, 256, 0}) {
				INFO(id << " at " << sr << " Hz, host blocks " << block);
				CHECK(same(render_host(id, parsed.events, sr, frames, block, true), ref));
			}
			// with a late event at the next block's frame 0 instead, at 44.1 kHz (steps between block
			// starts) some land a step late: what the negative frames are for
			if (sr == 44100) CHECK(!same(render_host(id, parsed.events, sr, frames, 33, false), ref));
		}
		std::filesystem::remove(path);
	}
}
