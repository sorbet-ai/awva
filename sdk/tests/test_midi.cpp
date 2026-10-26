// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// MIDI in on the PC (note_mapper.h: the mapping of awva/ump.h, with this host's
// voices) and the MIDI file reader (smf.h).
#include <array>
#include <bit>
#include <cmath>
#include <memory>

#include "doctest/doctest.h"
#include "awva_sdk/note_mapper.h"
#include "awva_sdk/smf.h"
#include "awva_sdk/timed_midi.h"

namespace sdk = awva::sdk;

namespace {
// two voices' controls, field by field and bit for bit (not memcmp: the struct has padding)
[[nodiscard]] bool same_controls(const awva::NoteControls& a, const awva::NoteControls& b) {
	const auto bits = [](float x) { return std::bit_cast<uint32_t>(x); };
	return bits(a.pitch) == bits(b.pitch) && bits(a.pressure) == bits(b.pressure) && bits(a.pressure_rate) == bits(b.pressure_rate) &&
	    bits(a.timbre) == bits(b.timbre) && bits(a.velocity) == bits(b.velocity) && bits(a.release) == bits(b.release) && a.gate == b.gate;
}
} // namespace
using Mapper = sdk::TimedMidi::Mapper; // (the build's sizes)

namespace {

// the voice playing a note (pres > 0 at the note's pitch), -1 if none
template <class M> int voice_of_n(const M& m, float note) {
	for (int v = 0; v < m.num_voices(); v++)
		if (m.voices()[v].pressure > 0.0f && std::fabs(m.voices()[v].pitch - note) < 1e-3f) return v;
	return -1;
}
int voice_of(const Mapper& m, float note) {
	for (int v = 0; v < m.num_voices(); v++)
		if (m.voices()[v].pressure > 0.0f && std::fabs(m.voices()[v].pitch - note) < 1e-3f) return v;
	return -1;
}

// messages, then one control step
void send(Mapper& m, std::initializer_list<std::array<uint8_t, 3>> msgs) {
	for (const auto& x : msgs) REQUIRE(m.message(x[0], x[1], x[2]));
	m.step();
}

} // namespace

TEST_CASE("midi in: a note on and off, as the decoder (ump.h) maps them") {
	Mapper m(10);
	send(m, {{0x90, 60, 100}}); // master channel: not an MPE member
	const int v = voice_of(m, 60.0f);
	REQUIRE(v == 0);
	const awva::NoteControls& c = m.voices()[v];
	CHECK(c.pressure_rate == doctest::Approx(100.0f / 127));
	CHECK(c.pressure == doctest::Approx(100.0f / 127)); // until pressure comes, the velocity
	send(m, {});                                        // the strike fades
	CHECK(c.pressure_rate == doctest::Approx(0.6f * 100.0f / 127));
	CHECK(c.pressure == doctest::Approx(100.0f / 127));
	send(m, {{0xd0, 0, 0}}); // channel pressure 0: held notes stay above 0
	CHECK(c.pressure == doctest::Approx(1.0f / 127));
	send(m, {{0xd0, 64, 0}});
	CHECK(c.pressure == doctest::Approx(64.0f / 127));
	send(m, {{0x80, 60, 40}});
	CHECK(c.pressure == 0.0f);
	CHECK(c.pressure_rate == doctest::Approx(-40.0f / 127));
	CHECK(m.notes_held() == 0);
}

TEST_CASE("midi in: MPE members bend, press and tilt their own notes") {
	Mapper m(10);
	send(m, {{0x91, 60, 90}, {0x92, 64, 90}}); // member channels 2 and 3
	const int a = voice_of(m, 60.0f), b = voice_of(m, 64.0f);
	REQUIRE(a == 0);
	REQUIRE(b == 1);
	send(m, {{0xe1, 0x00, 0x48}}); // channel 2 bend: (0x2400 - 0x2000) / 8192 * 48 = +6 semitones
	CHECK(m.voices()[a].pitch == doctest::Approx(66.0f));
	CHECK(m.voices()[b].pitch == doctest::Approx(64.0f));
	send(m, {{0xb2, 74, 127}}); // channel 3 timbre -> y
	CHECK(m.voices()[b].timbre == doctest::Approx(63.0f / 64));
	CHECK(m.voices()[a].timbre == 0.0f);
	send(m, {{0xd2, 32, 0}}); // channel 3 pressure
	CHECK(m.voices()[b].pressure == doctest::Approx(32.0f / 127));
	CHECK(m.voices()[a].pressure == doctest::Approx(90.0f / 127));
	// the bend range (RPN 0 on a member channel), then the master channel's bend: the instrument's
	send(m, {{0xb1, 101, 0}, {0xb1, 100, 0}, {0xb1, 6, 24}});
	CHECK(m.bend_range() == 24.0f);
	send(m, {{0xe0, 0x00, 0x60}}); // master: (0x3000 - 0x2000) / 8192 * 2 = +1
	CHECK(m.voices()[b].pitch == doctest::Approx(65.0f));
	// the MCM: no zone, every channel is like the master
	send(m, {{0xb0, 101, 0}, {0xb0, 100, 6}, {0xb0, 6, 0}});
	CHECK(m.members() == 0);
	send(m, {{0xb0, 64, 127}}); // sustain -> pedal
	CHECK(m.globals().sustain == 1.0f);
}

TEST_CASE("midi in: more notes than voices: a released voice first, then the quietest held one") {
	Mapper m(2);
	send(m, {{0x90, 60, 100}, {0x90, 62, 30}});
	send(m, {{0x80, 60, 0}});
	send(m, {{0x90, 64, 80}}); // takes 60's released voice
	CHECK(voice_of(m, 64.0f) == 0);
	CHECK(voice_of(m, 62.0f) == 1);
	send(m, {{0x90, 65, 100}}); // both held: steals the quieter 62
	CHECK(voice_of(m, 65.0f) == 1);
	CHECK(voice_of(m, 62.0f) < 0);
	CHECK(voice_of(m, 64.0f) == 0);
	send(m, {{0x90, 67, 10}}); // quieter than every held note: no voice
	CHECK(voice_of(m, 67.0f) < 0);
	CHECK(voice_of(m, 65.0f) == 1);
}

TEST_CASE("midi files: written and read back, tempo map applied") {
	const sdk::MidiEvent ev[] = {
	    {0.0, {0x90, 60, 100}, 3},
	    {0.5, {0xd0, 64, 0}, 2},
	    {1.0, {0x80, 60, 0}, 3},
	};
	const std::vector<uint8_t> bytes = sdk::make_midi(ev);
	const sdk::MidiFile f = sdk::parse_midi(bytes);
	REQUIRE(f.error.empty());
	CHECK(f.format == 0);
	REQUIRE(f.events.size() == 3);
	CHECK(f.events[1].t == doctest::Approx(0.5));
	CHECK(f.events[1].len == 2);
	CHECK(f.events[2].t == doctest::Approx(1.0));
	CHECK(f.events[2].bytes[0] == 0x80);
	// a tempo change to 60 bpm at 0.5 s (480 ticks): the next 480 ticks take 1 s
	std::vector<uint8_t> t = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 1, 0x01, 0xe0, 'M', 'T', 'r', 'k', 0, 0, 0, 0};
	const std::vector<uint8_t> body = {0x00, 0x90, 60, 100, 0x83, 0x60, 0xff, 0x51, 0x03, 0x0f, 0x42, 0x40, 0x83, 0x60, 0x60, 0, 0x00, 0xff, 0x2f, 0x00};
	t[21] = static_cast<uint8_t>(body.size());
	t.insert(t.end(), body.begin(), body.end());
	const sdk::MidiFile g = sdk::parse_midi(t);
	REQUIRE(g.error.empty());
	REQUIRE(g.events.size() == 2);
	CHECK(g.events[1].bytes[0] == 0x90); // running status
	CHECK(g.events[1].bytes[2] == 0);
	CHECK(g.events[1].t == doctest::Approx(1.5));
	CHECK(!sdk::parse_midi(std::vector<uint8_t>{'R', 'I', 'F', 'F'}).error.empty());
}

TEST_CASE("midi in: MIDI 2.0 per-note timbre (Registered Per-Note Controller 74, or a sensor's Assignable 2) on one channel, which CC74 then leaves alone") {
	Mapper m(4);
	const auto ump = [&m](uint32_t w0, uint32_t w1) { REQUIRE(m.ump(w0, w1)); };
	ump(0x40903c00u, 0xc0000000u); // note 60, channel 1
	ump(0x40904000u, 0xc0000000u); // note 64, channel 1
	ump(0x40103c02u, 0xc0000000u); // APNC 2 (a sensor's timbre) of note 60: +0.5
	ump(0x4000404au, 0x40000000u); // RPNC 74 (0x4a, brightness) of note 64: -0.5
	m.step();
	float y60 = 9, y64 = 9;
	for (int v = 0; v < m.num_voices(); v++) {
		if (m.voices()[v].pitch == 60.0f) y60 = m.voices()[v].timbre;
		if (m.voices()[v].pitch == 64.0f) y64 = m.voices()[v].timbre;
	}
	CHECK(y60 == doctest::Approx(0.5).epsilon(1e-6));
	CHECK(y64 == doctest::Approx(-0.5).epsilon(1e-6));
	ump(0x40b04a00u, 0xffffffffu); // CC74 of the channel: the notes keep their own
	m.step();
	for (int v = 0; v < m.num_voices(); v++) {
		if (m.voices()[v].pitch == 60.0f) CHECK(m.voices()[v].timbre == y60);
		if (m.voices()[v].pitch == 64.0f) CHECK(m.voices()[v].timbre == y64);
	}
}

// TimedMidi::reset() on a thread whose stack the test owns, painted beforehand: how deep it went is
// how much of the paint is gone. An embedded host resets an engine on a small audio-thread stack,
// so reset() must not build its kilobytes of MIDI state as a temporary. (POSIX threads, and not
// under the sanitizers, whose frames are of another size.)
// --- host-sized tables (note_mapper.h) ---------------------------------------------------------

namespace {
// how many notes a mapper of N notes holds, pressed one after another on one channel
template <int N, int V> int notes_held_after(int pressed) {
	awva::sdk::NoteMapper<N, V> m(V);
	for (int k = 0; k < pressed; k++) REQUIRE(m.message(0x90, static_cast<uint8_t>(40 + k), 100));
	m.step();
	return m.notes_held();
}
} // namespace

TEST_CASE("note mapper: its tables are the host's size, a note past them takes the oldest") {
	CHECK(notes_held_after<4, 2>(4) == 4);
	CHECK(notes_held_after<4, 2>(6) == 4); // (the oldest two taken)
	CHECK(notes_held_after<64, 16>(40) == 40);
	CHECK(notes_held_after<84, 10>(84) == 84); // (an embedded host's keys and MIDI notes)
	// with more notes than slots the oldest goes: its voice plays the newest note
	awva::sdk::NoteMapper<2, 2> m(2);
	REQUIRE(m.message(0x90, 60, 100));
	m.step();
	REQUIRE(m.message(0x90, 62, 100));
	m.step();
	REQUIRE(m.message(0x90, 64, 100));
	m.step();
	CHECK(voice_of_n(m, 60.0f) < 0);
	CHECK(voice_of_n(m, 62.0f) >= 0);
	CHECK(voice_of_n(m, 64.0f) >= 0);
	CHECK(Mapper::NOTES == 16); // (the build's default: AWVA_MIDI_NOTES)
}

// --- the control rate (timed_midi.h, host_config.h) -------------------------------------------

TEST_CASE("control rate: at 500 Hz the steps are the 2 ms grid frame for frame (44.1 and 48 kHz), and at 0 every block") {
	for (const int sr : {44100, 48000}) {
		CAPTURE(sr);
		for (uint64_t k = 0; k < 200000; k++) {
			REQUIRE(sdk::step_frame(k, sr, 500) == (k * static_cast<uint64_t>(sr) + 499u) / 500u); // (the 2 ms rule)
			REQUIRE(sdk::step_frame(k, sr, 0) == k * awva::BLOCK);
		}
		CHECK(sdk::step_seconds(sr, 500) == 0.002); // (the factors' time constants: their own step)
		CHECK(sdk::step_seconds(sr, 0) == static_cast<double>(awva::BLOCK) / sr);
	}
}

TEST_CASE("control rate: at a 2 ms step the level decay and the strike's fade are the 2 ms literals, bit for bit") {
	// a crowded, struck performance (more notes than voices: the allocator's levels decide) through
	// the mapper as constructed (the literals) and set to a 2 ms step: the same controls every step
	Mapper a(10), b(10);
	b.set_step(sdk::step_seconds(48000, 500));
	uint32_t rng = 99u;
	for (int k = 0; k < 4000; k++) {
		rng = rng * 1664525u + 1013904223u;
		if (k % 7 == 0) {
			const uint8_t note = static_cast<uint8_t>(40 + (rng >> 24) % 40), velo = static_cast<uint8_t>(1 + (rng >> 16) % 127);
			const uint8_t m[3] = {static_cast<uint8_t>((rng >> 8) % 3 == 0 ? 0x80 : 0x90), note, velo};
			REQUIRE(a.message(m, 3));
			REQUIRE(b.message(m, 3));
		}
		a.step();
		b.step();
		for (int v = 0; v < 10; v++) REQUIRE(same_controls(a.voices()[v], b.voices()[v]));
	}
}

TEST_CASE("control rate: the step factors are the correctly rounded powers, without libm (step_pow.h)") {
	namespace sp = awva::step_pow;
	// at 2 ms the factors themselves; at 48 kHz every block (y = 1/3) what the C library gives
	CHECK(sp::pow_of(0.998, sp::LN_0_998, 1.0) == 0.998);
	CHECK(sp::pow_of(0.6, sp::LN_0_6, 1.0) == 0.6);
	const double y48 = sdk::step_seconds(48000, 0) / 0.002;
	CHECK(sp::pow_of(0.998, sp::LN_0_998, y48) == std::pow(0.998, y48));
	CHECK(static_cast<float>(sp::pow_of(0.6, sp::LN_0_6, y48)) == static_cast<float>(std::pow(0.6, y48)));
	// where a C library's pow() is off by an ulp, the nearest double (computed to 80 digits)
	CHECK(sp::pow_of(0.998, sp::LN_0_998, 1.8399264029438822) == 0x1.fe1e1459bdd38p-1);
	CHECK(sp::pow_of(0.998, sp::LN_0_998, 1.389854065323141) == 0x1.fe93ccd981d4fp-1);
}

TEST_CASE("control rate: the strike fades by its time constant at any step (x0.6 per 2 ms)") {
	// at every block of 48 kHz (32 frames: a third of 2 ms), three steps fade it as one 2 ms step
	Mapper m(10);
	m.set_step(sdk::step_seconds(48000, 0));
	REQUIRE(m.message(0x90, 60, 100));
	m.step();
	const int v = voice_of(m, 60.0f);
	REQUIRE(v >= 0);
	const float strike = m.voices()[v].pressure_rate;
	for (int k = 0; k < 3; k++) m.step();
	CHECK(m.voices()[v].pressure_rate == doctest::Approx(0.6f * strike).epsilon(1e-6));
}

TEST_CASE("control rate: TimedMidi takes events at the first step at or after them, and another rate starts its grid where it is") {
	sdk::TimedMidi t(sdk::HostConfig{48000, 500, 10});
	const uint8_t on[3] = {0x90, 60, 100}, on2[3] = {0x90, 64, 100};
	(void)t.midi1(100, on, 3); // 500 Hz: the step at frame 192 takes it
	t.advance(160);
	CHECK(voice_of(t.midi(), 60.0f) < 0);
	t.advance(192);
	CHECK(voice_of(t.midi(), 60.0f) >= 0);
	t.set_control_hz(0); // every block from here: the grid starts at frame 192
	CHECK(t.control_hz() == 0);
	(void)t.midi1(200, on2, 3); // taken at 192 + 32 = 224
	t.advance(223);
	CHECK(voice_of(t.midi(), 64.0f) < 0);
	t.advance(224);
	CHECK(voice_of(t.midi(), 64.0f) >= 0);
}

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define AWVA_TEST_ASAN 1
#endif
#endif
#if (defined(__unix__) || defined(__APPLE__)) && !defined(__SANITIZE_ADDRESS__) && !defined(AWVA_TEST_ASAN)
#include <pthread.h>

#include <vector>

namespace {
// the bytes of a stack of its own that a thread running f(arg) used (its start included: the C
// library keeps its thread data at the stack's top)
size_t stack_used(void* (*f)(void*), void* arg) {
	constexpr size_t SIZE = 256 * 1024; // (above PTHREAD_STACK_MIN everywhere)
	std::vector<uint8_t> stack(SIZE, 0xa5);
	pthread_attr_t attr;
	REQUIRE(pthread_attr_init(&attr) == 0);
	REQUIRE(pthread_attr_setstack(&attr, stack.data(), SIZE) == 0);
	pthread_t t;
	REQUIRE(pthread_create(&t, &attr, f, arg) == 0);
	REQUIRE(pthread_join(t, nullptr) == 0);
	pthread_attr_destroy(&attr);
	size_t untouched = 0; // (the stack grows down: from its low end, the paint left)
	while (untouched < SIZE && stack[untouched] == 0xa5) untouched++;
	return SIZE - untouched;
}
} // namespace

TEST_CASE("timed midi: reset() uses little stack (its MIDI state rebuilt in place)") {
	static_assert(sizeof(sdk::TimedMidi::Mapper) > 4096, "the state a temporary would put on the stack");
	auto midi = std::make_unique<sdk::TimedMidi>(sdk::HostConfig{48000, 0, 10});
	const uint8_t on[3] = {0x90, 60, 100};
	(void)midi->midi1(0, on, 3, 7);
	midi->advance(96);
	const size_t start = stack_used([](void*) -> void* { return nullptr; }, nullptr);
	const size_t with_reset = stack_used(
	    [](void* m) -> void* {
		    static_cast<sdk::TimedMidi*>(m)->reset();
		    return nullptr;
	    },
	    midi.get());
	const size_t used = with_reset > start ? with_reset - start : 0;
	MESSAGE("reset(): " << used << " bytes of stack (a thread's start: " << start << ")");
	CHECK(used < 1024); // (a temporary Mapper alone is sizeof(Mapper))
	int32_t ids[4];
	CHECK(midi->ended(ids, 4, 0, true) == 1); // (and it did reset: the note's id ended)
}
#endif
