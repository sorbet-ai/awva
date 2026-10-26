// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// MIDI in on the PC (midi_voices.h: the mapping of awva/midi_in.h, with this host's
// voices) and the MIDI file reader (smf.h).
#include <array>
#include <cmath>

#include "doctest/doctest.h"
#include "awva_sdk/midi_voices.h"
#include "awva_sdk/smf.h"

namespace sdk = awva::sdk;

namespace {

// the voice playing a note (pres > 0 at the note's pitch), -1 if none
int voice_of(const sdk::MidiVoices& m, float note) {
	for (int v = 0; v < m.num_voices(); v++)
		if (m.voices()[v].pressure > 0.0f && std::fabs(m.voices()[v].pitch - note) < 1e-3f) return v;
	return -1;
}

// messages, then one 2 ms step
void send(sdk::MidiVoices& m, std::initializer_list<std::array<uint8_t, 3>> msgs) {
	for (const auto& x : msgs) REQUIRE(m.message(x[0], x[1], x[2]));
	m.step();
}

} // namespace

TEST_CASE("midi in: a note on and off, as midi_in.h maps them") {
	sdk::MidiVoices m(10);
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
	sdk::MidiVoices m(10);
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
	sdk::MidiVoices m(2);
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

TEST_CASE("midi in: MIDI 2.0 per-note y (Assignable Per-Note Controller 4, or Registered 74) on one channel, which CC74 then leaves alone") {
	sdk::MidiVoices m(4);
	const auto ump = [&m](uint32_t w0, uint32_t w1) { REQUIRE(m.ump2(w0, w1)); };
	ump(0x40903c00u, 0xc0000000u); // note 60, channel 1
	ump(0x40904000u, 0xc0000000u); // note 64, channel 1
	ump(0x40103c04u, 0xc0000000u); // APNC 4 (y) of note 60: +0.5
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
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define AWVA_TEST_ASAN 1
#endif
#endif
#if (defined(__unix__) || defined(__APPLE__)) && !defined(__SANITIZE_ADDRESS__) && !defined(AWVA_TEST_ASAN)
#include <pthread.h>

#include <memory>
#include <vector>

#include "awva_sdk/timed_midi.h"

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
	static_assert(sizeof(sdk::MidiVoices) > 4096, "the state a temporary would put on the stack");
	auto midi = std::make_unique<sdk::TimedMidi>(10, 48000);
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
	CHECK(used < 1024); // (a temporary MidiVoices alone is sizeof(MidiVoices))
	int32_t ids[4];
	CHECK(midi->ended(ids, 4, 0, true) == 1); // (and it did reset: the note's id ended)
}
#endif
