// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The UMP decoder (awva/ump.h) on its own: MIDI 2.0's scales carry a sensor's codes and every float
// pitch exactly; the host's calls and their order; the strike's fade; a sensor's notes as measured;
// the sensor range; per-note timbre; the instrument's messages.
#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "awva/ump.h"

namespace {

namespace ump = awva::ump;
using awva::NoteControls;

bool same(float a, float b) { return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b); } // bit for bit

// a host that logs the decoder's calls
struct Host {
	std::vector<std::string> log;
	NoteControls last{};
	float sustain = -1, accel = -1, spin[3] = {-1, -1, -1}, range = 48;
	void note_started(int id, const NoteControls&) { log.push_back("start " + std::to_string(id)); }
	void note_changed(int id, const NoteControls& n) {
		log.push_back("change " + std::to_string(id));
		last = n;
	}
	void note_silenced(int id) { log.push_back("silence " + std::to_string(id)); }
	void note_dropped(int id) { log.push_back("drop " + std::to_string(id)); }
	[[nodiscard]] bool echoed(int) const { return false; }
	[[nodiscard]] float bend_range() const { return range; }
	[[nodiscard]] bool zone_master(int channel) const { return channel == 0; }
	void set_bend_range(float r) { range = r; }
	void zone_configured(int, int) {}
	void set_sustain(float s) { sustain = s; }
	[[nodiscard]] bool bends_out_on(int) const { return false; }
	void bend_moved(float) {}
	void set_accel(float g) { accel = g; }
	void set_spin(int axis, float s) { spin[axis] = s; }
};

template <int N> void send(ump::Decoder<N>& d, Host& h, ump::Packet p) {
	d.packet(h, p);
	d.flush(h);
}

} // namespace

TEST_CASE("ump: a sensor's 14-bit codes are exact in MIDI 2.0's 32-bit scales, with a sensor range of 4") {
	// code / 8191 over the int16 range, a pressure (0 .. 4) and a bipolar controller (-4 .. 4)
	for (int code = -32768; code <= 32767; code++) {
		const float x = static_cast<float>(code) / 0x1fff;
		if (x > 4.0f || x < -4.0f) continue; // (the ends: beyond the range, clamped)
		REQUIRE(same(ump::bipolar32(ump::to_bipolar32(x, 4.0), 4.0), x));
		if (code >= 0) REQUIRE(same(ump::unsigned32(ump::to_unsigned32(x, 4.0), 4.0), x));
	}
	// and with no range (1): every code within -1 .. 1
	for (int code = -8191; code <= 8191; code++) {
		const float x = static_cast<float>(code) / 0x1fff;
		REQUIRE(same(ump::bipolar32(ump::to_bipolar32(x)), x));
		if (code >= 0) REQUIRE(same(ump::unsigned32(ump::to_unsigned32(x)), x));
	}
	CHECK(ump::fixed428(ump::to_fixed428(4.0)) == 4.0);
	CHECK(ump::fixed428(ump::to_fixed428(1.5)) == 1.5);
}

TEST_CASE("ump: every float pitch from 0.25 to 128 is exact in Pitch 7.25") {
	float p = 0.25f;
	while (p < 128.0f) {
		const uint32_t u = ump::to_pitch725(p);
		if (!same(ump::pitch725(u), p)) {
			FAIL("not exact: " << p);
			break;
		}
		if (p > 1.0f) p += 0.0009f; // (above 1 a sample of the floats; below it every one)
		p = std::nextafter(p, 200.0f);
	}
}

TEST_CASE("ump: a packet's length from its message type") {
	CHECK(ump::words(0x20000000u) == 1);
	CHECK(ump::words(0x40000000u) == 2);
	CHECK(ump::words(0xf0000000u) == 4);
}

TEST_CASE("ump: the host hears of each changed note once a flush, in the order they first changed") {
	ump::Decoder<16> d;
	Host h;
	d.packet(h, ump::message(ump::op_note_on, 1, 60, 0, 0x80000000u));
	d.packet(h, ump::message(ump::op_note_on, 2, 64, 0, 0x80000000u));
	d.packet(h, ump::message(ump::op_channel_pressure, 1, 0, 0, 0x40000000u));
	d.flush(h);
	CHECK(h.log == std::vector<std::string>{"start 0", "change 0", "start 1", "change 1"});
	h.log.clear();
	send(d, h, ump::message(ump::op_note_off, 1, 60, 0, 0));
	CHECK(h.log == std::vector<std::string>{"change 0"});
	CHECK(h.last.gate == 0);
	CHECK(h.last.pressure == 0.0f);
	h.log.clear();
	d.all_off(h);
	CHECK(h.log == std::vector<std::string>{"drop 1"});
}

TEST_CASE("ump: with every slot held, a new note takes the oldest, whose sound ends") {
	ump::Decoder<4> d;
	Host h;
	for (int k = 0; k < 4; k++) send(d, h, ump::message(ump::op_note_on, 1, 60 + k, 0, 0x80000000u));
	h.log.clear();
	send(d, h, ump::message(ump::op_note_on, 1, 70, 0, 0x80000000u));
	CHECK(h.log == std::vector<std::string>{"silence 0", "start 0", "change 0"});
	CHECK(d.key(0) == 70);
}

TEST_CASE("ump: a MIDI note's strike: its velocity as the pressure rate, x0.6 a tick while above 0.01, then 0") {
	ump::Decoder<16> d;
	Host h;
	send(d, h, ump::message(ump::op_note_on, 1, 60, 0, 0xffff0000u));
	CHECK(h.last.pressure_rate == 1.0f);
	CHECK(h.last.pressure == 1.0f); // (no pressure yet: the velocity)
	CHECK(h.last.velocity == 1.0f);
	CHECK(d.fading());
	float r = 1.0f;
	int ticks = 0;
	while (d.fading() && ticks < 100) {
		d.tick(h);
		r = r > 0.01f ? r * 0.6f : 0.0f;
		REQUIRE(same(h.last.pressure_rate, r));
		ticks++;
	}
	CHECK(h.last.pressure_rate == 0.0f);
	CHECK(ticks == 11); // (0.6^9 = 0.010 is above 0.01: ten fades, then 0)
	send(d, h, ump::message(ump::op_note_off, 1, 60, 0, 0x80000000u));
	CHECK(same(h.last.pressure_rate, -ump::velocity16(0x80000000u))); // the release
	CHECK(same(h.last.release, ump::velocity16(0x80000000u)));
}

TEST_CASE("ump: a MIDI note's pressure stays at 1/127 or more while held, and a sensor's note's as it comes") {
	ump::Decoder<16> d;
	Host h;
	send(d, h, ump::message(ump::op_note_on, 1, 60, 0, 0x80000000u));
	send(d, h, ump::message(ump::op_poly_pressure, 1, 60, 0, 0));
	CHECK(h.last.pressure == 1.0f / 127.0f);
	// a sensor: its own rate (Assignable Per-Note Controller 1) makes the note measured
	send(d, h, ump::message(ump::op_note_on, 1, 62, 0, 0x80000000u));
	send(d, h, ump::message(ump::op_assignable_pnc, 1, 62, ump::apnc_pressure_rate, ump::to_bipolar32(0.25f)));
	CHECK(same(h.last.pressure_rate, 0.25f));
	send(d, h, ump::message(ump::op_poly_pressure, 1, 62, 0, 0));
	CHECK(h.last.pressure == 0.0f); // (no floor)
	send(d, h, ump::message(ump::op_assignable_pnc, 1, 62, ump::apnc_pressure_rate, ump::to_bipolar32(-0.5f)));
	send(d, h, ump::message(ump::op_note_off, 1, 62, 0, 0xffff0000u));
	CHECK(h.last.gate == 0);
	CHECK(same(h.last.pressure_rate, -0.5f)); // (its rate as measured, not the Note Off's velocity)
}

TEST_CASE("ump: a channel's sensor range scales its pressures and its sensor controllers") {
	ump::Decoder<16> d;
	Host h;
	send(d, h, ump::message(ump::op_assignable_controller, 3, 0, ump::ac_sensor_range, ump::to_fixed428(4.0)));
	CHECK(d.sensor_range(3) == 4.0);
	send(d, h, ump::message(ump::op_note_on, 3, 50, 0, 0x80000000u));
	const float x = 12000.0f / 0x1fff; // (a hard press: above MIDI's full scale)
	send(d, h, ump::message(ump::op_poly_pressure, 3, 50, 0, ump::to_unsigned32(x, 4.0)));
	CHECK(same(h.last.pressure, x));
	send(d, h, ump::message(ump::op_assignable_pnc, 3, 50, ump::apnc_timbre, ump::to_bipolar32(-1.5f, 4.0)));
	CHECK(same(h.last.timbre, -1.5f));
	CHECK(d.sensor_range(4) == 1.0); // (another channel: MIDI's)
}

TEST_CASE("ump: a note's own timbre (Registered Per-Note Controller 74) is kept when the channel's CC74 moves") {
	ump::Decoder<16> d;
	Host h;
	send(d, h, ump::message(ump::op_note_on, 1, 60, 0, 0x80000000u));
	send(d, h, ump::message(ump::op_note_on, 1, 64, 0, 0x80000000u));
	send(d, h, ump::message(ump::op_registered_pnc, 1, 64, ump::RPNC_BRIGHTNESS, 0x40000000u));
	CHECK(h.last.timbre == -0.5f);
	h.log.clear();
	send(d, h, ump::message(ump::op_control_change, 1, 74, 0, 0xc0000000u)); // CC74 of the channel
	CHECK(h.log == std::vector<std::string>{"change 0"});                    // (only the other note)
}

TEST_CASE("ump: the instrument's messages: sustain, motion, the bend range") {
	ump::Decoder<16> d;
	Host h;
	send(d, h, ump::message(ump::op_control_change, 0, 64, 0, 0xffffffffu));
	CHECK(h.sustain == 1.0f);
	const uint32_t m1[1] = {0x20b04000u}; // MIDI 1.0 CC64 = 0
	d.packet(h, m1);
	CHECK(h.sustain == 0.0f);
	send(d, h, ump::message(ump::op_assignable_controller, 0, 0, ump::ac_accel, 0x40000000u));
	CHECK(h.accel == doctest::Approx(1.0f).epsilon(1e-6)); // (u32 x 4 g: a quarter is 1 g)
	send(d, h, ump::message(ump::op_assignable_controller, 0, 0, ump::ac_spin + 2, 0xc0000000u));
	CHECK(h.spin[2] == doctest::Approx(0.5f).epsilon(1e-6));
	send(d, h, ump::message(ump::op_registered_controller, 1, 0, 0, 12u << 25));
	CHECK(h.range == 12.0f);
	send(d, h, ump::message(ump::op_registered_controller, 0, 0, 0, 2u << 25)); // (the zone master's: not the notes')
	CHECK(h.range == 12.0f);
}
