// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The host shell (shell.h): MIDI in, any frame count per call; the MIDI file player.
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "awva_sdk/performance.h"
#include "awva_sdk/shell.h"
#include "awva_sdk/smf.h"

namespace sdk = awva::sdk;

namespace {

// frames of the shell in calls of the given sizes (cycled), MIDI note 60 on at 0, off at 0.5 s
// (with its offset in the call that frame is in, as a plugin host gives it)
std::vector<float> render(const std::vector<int>& sizes, int frames) {
	const sdk::EngineEntry* va = sdk::find_engine("va");
	REQUIRE(va != nullptr);
	sdk::Shell s(*va, 48000);
	s.midi(0x90, 60, 100);
	std::vector<float> l(static_cast<size_t>(frames)), r(static_cast<size_t>(frames));
	bool off = false;
	for (int n = 0, k = 0; n < frames; k++) {
		const int m = std::min(sizes[static_cast<size_t>(k) % sizes.size()], frames - n);
		if (!off && n + m > 24000) {
			s.midi(0x80, 60, 64, 24000 - n);
			off = true;
		}
		s.process(&l[static_cast<size_t>(n)], &r[static_cast<size_t>(n)], m);
		n += m;
	}
	l.insert(l.end(), r.begin(), r.end());
	return l;
}

} // namespace

TEST_CASE("shell: a MIDI note sounds and ends, the call size makes no difference") {
	const int frames = 48000 * 3;
	const std::vector<float> a = render({32}, frames);
	const std::vector<float> b = render({1, 7, 64, 333, 13}, frames);
	float peak = 0, tail = 0;
	for (int n = 0; n < frames; n++) {
		peak = std::fmax(peak, std::fabs(a[static_cast<size_t>(n)]));
		if (n > 48000 * 5 / 2) tail = std::fmax(tail, std::fabs(a[static_cast<size_t>(n)]));
	}
	CHECK(peak > 0.01f);
	CHECK(tail < 1e-3f);
	// (the note off goes in before the block of frame 24000 either way: 750 blocks)
	CHECK(a == b);
}

TEST_CASE("shell: timed MIDI goes in at the first 2 ms step at or after its frame (timed_midi.h)") {
	const sdk::EngineEntry* va = sdk::find_engine("va");
	REQUIRE(va != nullptr);
	// a note at frame 100 of a 1000-frame call is the note sent at frame 192, the next step
	// (steps every 96 frames at 48 kHz), before the block that starts there
	sdk::Shell a(*va, 48000), b(*va, 48000);
	std::vector<float> la(2000), ra(2000), lb(2000), rb(2000);
	a.midi(0x90, 60, 100, 100);
	a.process(la.data(), ra.data(), 1000);
	a.process(la.data() + 1000, ra.data() + 1000, 1000);
	b.process(lb.data(), rb.data(), 192);
	b.midi(0x90, 60, 100);
	b.process(lb.data() + 192, rb.data() + 192, 1808);
	CHECK(la == lb);
	float peak = 0;
	for (float x : la) peak = std::fmax(peak, std::fabs(x));
	CHECK(peak > 1e-3f);
}

TEST_CASE("shell: every engine plays a MIDI note through the shell") {
	for (const sdk::EngineEntry* e : sdk::engines()) {
		CAPTURE(e->info->name);
		sdk::Shell s(*e, 48000, 10, false);
		s.midi(0x90, 57, 110);
		std::vector<float> l(9600), r(9600);
		s.process(l.data(), r.data(), 9600);
		float peak = 0;
		bool finite = true;
		for (float x : l) {
			finite &= std::isfinite(x);
			peak = std::fmax(peak, std::fabs(x));
		}
		CHECK(finite);
		CHECK(peak > 1e-3f);
	}
}

TEST_CASE("shell: no pedal is no sustain: a note with no CC64 plays as with the pedal sent up (0)") {
	CHECK(awva::instrument_at_rest.sustain == 0.0f);
	for (const sdk::EngineEntry* e : sdk::engines()) {
		CAPTURE(e->info->name);
		const auto play = [e](bool pedal_up) {
			sdk::Shell s(*e, 48000, 10, false);
			if (pedal_up) s.midi(0xb0, 64, 0);
			s.midi(0x90, 57, 110);
			s.midi(0x80, 57, 64, 24000);
			std::vector<float> l(48000 * 3), r(l.size());
			s.process(l.data(), r.data(), static_cast<int>(l.size()));
			CHECK(s.globals().sustain == 0.0f);
			return l;
		};
		CHECK(play(false) == play(true));
	}
}

TEST_CASE("players: a MIDI file plays an engine and says what it is") {
	const sdk::MidiEvent ev[] = {
	    {0.0, {0x90, 60, 100}, 3},
	    {0.0, {0x91, 64, 90}, 3},
	    {0.3, {0xe1, 0x00, 0x48}, 3},
	    {0.6, {0x80, 60, 0}, 3},
	    {0.6, {0x81, 64, 0}, 3},
	};
	const std::vector<uint8_t> bytes = sdk::make_midi(ev);
	const char* path = "awva_sdk_tests_players.mid";
	FILE* f = std::fopen(path, "wb");
	REQUIRE(f != nullptr);
	std::fwrite(bytes.data(), 1, bytes.size(), f);
	std::fclose(f);
	std::string error;
	const std::unique_ptr<sdk::Player> p = sdk::midi_player(path, 10, 48000, &error);
	REQUIRE(p != nullptr);
	CHECK(p->describe().find("awva_sdk_tests_players.mid") != std::string::npos);
	CHECK(p->describe().find("fnv") != std::string::npos);
	CHECK(p->seconds() == doctest::Approx(2.6));
	sdk::Engine e(*sdk::find_engine("ep"));
	e.init(48000, 10);
	std::vector<awva::NoteControls> vc(10, awva::note_at_rest);
	awva::InstrumentControls g = awva::instrument_at_rest;
	float peak = 0;
	for (int b = 0; b < static_cast<int>(p->seconds() * 48000 / 32); b++) {
		p->block(b, vc.data(), g);
		float out[32];
		float* const o[1] = {out};
		e.process(vc.data(), g, o);
		for (float x : out) peak = std::fmax(peak, std::fabs(x));
	}
	CHECK(peak > 0.01f);
	CHECK(e.active_voices() == 0);
	std::remove(path);
	CHECK(sdk::midi_player("no/such/file.mid", 10, 48000, &error) == nullptr);
	CHECK(!error.empty());
}

TEST_CASE("shell: MIDI 2.0 in (UMP) plays as its MIDI 1.0 counterpart, and a note's id comes back when it has ended") {
	const sdk::EngineEntry* va = sdk::find_engine("va");
	REQUIRE(va != nullptr);
	sdk::Shell a(*va, 48000), b(*va, 48000);
	std::vector<float> la(4800), ra(4800), lb(4800), rb(4800);
	a.midi(0x90, 60, 127, 0, 7);
	const uint32_t on[2] = {0x40903c00u, 0xffff0000u}; // velocity 1.0 = MIDI 1.0's 127
	b.ump(on, 0, 7);
	a.process(la.data(), ra.data(), 4800);
	b.process(lb.data(), rb.data(), 4800);
	CHECK(la == lb);
	int32_t ids[4];
	CHECK(a.ended_notes(ids, 4) == 0);
	a.midi(0x80, 60, 0);
	int got = 0;
	for (int k = 0; k < 200 && got == 0; k++) {
		a.process(la.data(), ra.data(), 4800);
		got = a.ended_notes(ids, 4);
	}
	REQUIRE(got == 1);
	CHECK(ids[0] == 7);
}
