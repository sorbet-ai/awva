// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// MIDI in: the one decoder from Universal MIDI Packets to the engines' per-note controls
// (NoteControls) and the instrument's messages. It reads standard MIDI 2.0 channel voice messages
// (UMP type 4) and MIDI 1.0 channel voice messages in UMP (type 2, MPE included), each in its own
// resolution: no private scale, no other way in. Every group is taken as one. Pure: no HAL, no
// heap; the voices (voice_allocator.h) and the echo filter are the host's.
//
//   engine control  MIDI 2.0 (type 4)                              MIDI 1.0 (type 2)
//   note            Note On / Off: (channel, note number)          the same
//   velocity        Note On velocity / 65535                       velocity / 127
//   release         Note Off velocity / 65535                      note-off velocity / 127
//   pitch           the note number, or Registered Per-Note        the note number
//                   Controller 3 (Pitch 7.25, absolute); plus the
//                   channel's pitch bend x the bend range (an MPE member channel) or the
//                   instrument's bend (any other channel: +-2 semitones)
//   pressure        Poly Pressure (that note), Channel Pressure    the same / 127
//                   (its channel's notes): u32 / (2^32 - 1) x the channel's sensor range
//   pressure_rate   Assignable Per-Note Controller 1 (a sensor's   (the strike, below)
//                   measured rate, bipolar x the sensor range)
//   timbre          Registered Per-Note Controller 74 (the note's  CC74 (64 = 0), CC1 (0 .. 1)
//                   own, bipolar), Assignable Per-Note Controller
//                   2 (a sensor's, bipolar x the sensor range), CC74 (bipolar), CC1
//   sustain         CC64                                           CC64 / 127
//   motion          Assignable Controllers 0x10 (accel_g: u32 x 4) and 0x14 .. 0x16 (spin, bipolar)
//   sensor range    Assignable Controller 1 (unsigned 4.28: 1.0 = 0x10000000, 1 .. 16): the full
//                   scale of the channel's pressures and of its sensor controllers (a controller
//                   with headroom above MIDI's full pressure declares it, as MPE declares a bend range)
//   bend range      Registered Controller 0/0 (semitones in the top 7 bits, cents in the next 7);
//                   RPN 0 (MIDI 1.0); not the MPE zone master's
//   MPE zone        Registered Controller 0/6, RPN 6 on channel 1: the member channels (lower zone)
//
// A note without a measured rate (any MIDI note but a sensor's) gets a strike: its Note On's
// velocity as its pressure_rate, fading x0.6 every tick() (STEP_SECONDS) while above 0.01, then 0;
// its Note Off's velocity as a negative rate. Until pressure comes its pressure is the velocity (a
// keyboard without aftertouch still sounds), and while it is held its pressure stays at 1/127 or
// more (pressure 0 is a release for the engines). A sensor's note (one that sent its rate) is
// taken as measured: no strike, no floor, its Note Off ends its gate only.
// With every note slot held, a new note takes the oldest one (its sound ends: note_silenced).
//
// The host (any type with these members; the calls are templates, inlined), called at flush() for
// every changed note, in the order they first changed, and at once for the instrument's messages:
//   void note_started(int id, const NoteControls&)  a Note On: give it a voice
//   void note_changed(int id, const NoteControls&)  its controls changed (to its voice)
//   void note_silenced(int id)                      its sound ends now (taken by a newer note)
//   void note_dropped(int id)                       it ends silently (all_off)
//   bool echoed(int channel)       the host's own notes went out on this channel lately: their
//                                  echo is not played
//   float bend_range()             the notes' pitch bend range, semitones
//   bool zone_master(int channel)  a bend range on this channel is the zone master's own
//   void set_bend_range(float)     the notes' new bend range
//   void zone_configured(int channel, int members)   an MCM came
//   void set_sustain(float)        CC64, 0 .. 1
//   bool bends_out_on(int channel) the host sends its own bends there: a bend from it is an echo
//   void bend_moved(float delta)   the instrument's bend changes by delta (before the notes)
//   void set_accel(float g)        motion: the acceleration's magnitude
//   void set_spin(int axis, float) motion: the angular rate about an axis
// A note's id is its slot, 0 .. Notes - 1; channel(id) and key(id) say which note it is.
#ifndef AWVA_UMP_H_
#define AWVA_UMP_H_

#include <stdint.h>

#include "controls.h"

namespace awva::ump {

inline constexpr double STEP_SECONDS = 0.002; // the strike's fade step: tick() is due this often

// a Universal MIDI Packet's length in words, from its first word's message type (UMP 1.1)
[[nodiscard]] constexpr int words(uint32_t w0) {
	constexpr uint8_t n[16] = {1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4};
	return n[w0 >> 28];
}

// --- MIDI 2.0's scales --------------------------------------------------------------------------

// a 32-bit value as a float: unsigned 0 .. 1, or bipolar -1 .. 1 by the specification (the centre
// 0x80000000 is 0 exactly, each side scaled to its own end), times a range
[[nodiscard]] inline float unsigned32(uint32_t v, double range = 1.0) { return (float)((double)v / 4294967295.0 * range); }
[[nodiscard]] inline float bipolar32(uint32_t v, double range = 1.0) {
	const double c = 2147483648.0, d = (double)v - c;
	return (float)((d < 0 ? d / c : d / 2147483647.0) * range);
}
[[nodiscard]] inline float velocity16(uint32_t w1) { return (float)((double)(w1 >> 16) / 65535.0); }
[[nodiscard]] inline float pitch725(uint32_t v) { return (float)((double)v / 33554432.0); }
// unsigned 4.28 (1.0 = 0x10000000)
[[nodiscard]] inline double fixed428(uint32_t v) { return (double)v / 268435456.0; }

// and back: the 32-bit value whose decoding is x (to the nearest; clamped to the scale's ends)
[[nodiscard]] inline uint32_t round_u32(double v) { return (uint32_t)(v + 0.5); }
[[nodiscard]] inline uint32_t to_unsigned32(float x, double range = 1.0) {
	const double v = (double)x / range * 4294967295.0;
	return !(v > 0) ? 0u : (v >= 4294967294.5 ? 0xffffffffu : round_u32(v));
}
[[nodiscard]] inline uint32_t to_bipolar32(float x, double range = 1.0) {
	const double y = (double)x / range;
	if (!(y > -1.0)) return 0u;
	if (y >= 1.0) return 0xffffffffu;
	return y <= 0.0 ? 0x80000000u - round_u32(-y * 2147483648.0) : 0x80000000u + round_u32(y * 2147483647.0);
}
[[nodiscard]] inline uint16_t to_velocity16(float x) {
	return !(x > 0.0f) ? (uint16_t)0 : (x >= 1.0f ? (uint16_t)0xffff : (uint16_t)round_u32((double)x * 65535.0));
}
[[nodiscard]] inline uint32_t to_pitch725(float note) {
	const double v = (double)note * 33554432.0;
	return !(v > 0) ? 0u : (v >= 4294967294.5 ? 0xffffffffu : round_u32(v));
}
[[nodiscard]] inline uint32_t to_fixed428(double x) { return x <= 0 ? 0u : (x >= 15.999999 ? 0xffffffffu : round_u32(x * 268435456.0)); }

// --- MIDI 2.0's channel voice messages (type 4, two words) --------------------------------------

enum Opcode : uint8_t {
	op_registered_pnc = 0x0,
	op_assignable_pnc = 0x1,
	op_registered_controller = 0x2,
	op_assignable_controller = 0x3,
	op_note_off = 0x8,
	op_note_on = 0x9,
	op_poly_pressure = 0xa,
	op_control_change = 0xb,
	op_channel_pressure = 0xd,
	op_pitch_bend = 0xe,
};
// the controllers this decoder reads beyond the registered ones: a sensor's per-note rate and
// timbre, and a channel's sensor range and motion
enum Assignable : uint8_t {
	apnc_pressure_rate = 1,
	apnc_timbre = 2,
	ac_sensor_range = 1,
	ac_accel = 0x10,
	ac_spin = 0x14, // .. 0x16
};
inline constexpr uint8_t RPNC_PITCH = 3, RPNC_BRIGHTNESS = 74;

struct Packet {
	uint32_t w0, w1;
};
[[nodiscard]] constexpr Packet message(Opcode op, int channel, int b2, int b3, uint32_t data) {
	return {0x40000000u | ((uint32_t)op << 20) | ((uint32_t)(channel & 0xf) << 16) | ((uint32_t)(b2 & 0x7f) << 8) | (uint32_t)(b3 & 0xff), data};
}

// --- the decoder ----------------------------------------------------------------------------------

template <int Notes> class Decoder {
	static_assert(Notes >= 1 && Notes <= 255, "note ids are uint8_t");

public:
	// one UMP packet (its words; type 4: two, type 2: one); the host flushes after it
	template <class Host> void packet(Host& h, const uint32_t* w) {
		const uint32_t t = w[0] >> 28;
		if (t == 2) midi1(h, (uint8_t)((w[0] >> 16) & 0x0f), (uint8_t)((w[0] >> 16) & 0xf0), (uint8_t)((w[0] >> 8) & 0x7f), (uint8_t)(w[0] & 0x7f));
		else if (t == 4) midi2(h, w[0], w[1]);
	}
	template <class Host> void packet(Host& h, const Packet& p) {
		const uint32_t w[2] = {p.w0, p.w1};
		packet(h, w);
	}

	// the host hears of every note changed since the last flush, in the order they first changed
	template <class Host> void flush(Host& h) {
		for (int k = 0; k < queued_; k++) {
			const int id = queue_[k];
			Slot& n = slot_[id];
			const uint8_t p = n.pending;
			n.pending = 0;
			n.queued = false;
			if (p & SILENCED) h.note_silenced(id);
			if (p & DROPPED) {
				h.note_dropped(id);
				continue;
			}
			if (p & (STARTED | CHANGED)) {
				const NoteControls c = controls(id);
				if (p & STARTED) h.note_started(id, c);
				h.note_changed(id, c);
			}
		}
		queued_ = 0;
	}

	// the strikes fade one step (then flush)
	template <class Host> void tick(Host& h) {
		bool fading = false;
		for (int id = 0; id < Notes; id++) {
			Slot& n = slot_[id];
			if (n.on && n.strike && n.rate > 0.0f) {
				n.rate = n.rate > 0.01f ? n.rate * 0.6f : 0.0f;
				fading |= n.rate > 0.0f;
				mark(id, CHANGED);
			}
		}
		fading_ = fading;
		flush(h);
	}
	// a strike is fading: tick() is due every STEP_SECONDS
	[[nodiscard]] bool fading() const { return fading_; }

	// every note off at once, silently (the panic key)
	template <class Host> void all_off(Host& h) {
		for (int s = 0; s < Notes; s++) {
			if (slot_[s].channel >= 0) {
				slot_[s].on = false;
				mark(s, DROPPED);
				slot_[s].channel = -1;
			}
		}
		for (bool& p : chan_pres_set_) p = false;
		flush(h);
	}

	// the instrument's bend (semitones), which every note follows
	template <class Host> void set_bend(Host& h, float semitones) {
		bend_ = semitones;
		update(-1);
		(void)h;
	}
	[[nodiscard]] float bend() const { return bend_; }

	// the MPE zone: member channels 2 .. 1 + n (0: none, every channel is like the master)
	void set_members(int n) { members_ = n; }
	[[nodiscard]] int members() const { return members_; }
	[[nodiscard]] bool member(int channel) const { return channel >= 1 && channel <= members_; }

	// note id's note: its channel (-1: no note) and key
	[[nodiscard]] int channel(int id) const { return slot_[id].channel; }
	[[nodiscard]] int key(int id) const { return slot_[id].key; }
	// a channel's sensor range (Assignable Controller 1)
	[[nodiscard]] double sensor_range(int channel) const { return range_[channel]; }

private:
	enum : uint8_t { STARTED = 1,
		CHANGED = 2,
		SILENCED = 4,
		DROPPED = 8 };
	struct Slot {
		int8_t channel = -1; // -1: free
		uint8_t key = 0;
		bool on = false;
		bool strike = false;   // its rate is the strike's (it fades), not a sensor's
		bool sensor = false;   // a sensor's note: its rate measured, its controls as they come
		bool pressure = false; // pressure received: it follows that, not the velocity
		bool absolute = false; // its pitch came as Pitch 7.25
		bool own_timbre = false;
		bool queued = false;
		uint8_t pending = 0;
		uint32_t age = 0;
		float abs_pitch = 0, pitch = 69.0f, pres = 0, rate = 0, timbre = 0, velocity = 0, release = 0;
	};

	[[nodiscard]] static float clampf(float x, float lo, float hi) { return x > hi ? hi : (x < lo ? lo : x); }
	// a held note's pressure stays above 0 (pressure 0 is a release for the engines)
	[[nodiscard]] static float held_pres(float p) { return p > 1.0f / 127.0f ? p : 1.0f / 127.0f; }

	void mark(int id, uint8_t what) {
		Slot& n = slot_[id];
		n.pending |= what;
		if (!n.queued) {
			n.queued = true;
			queue_[queued_++] = (uint8_t)id;
		}
	}
	[[nodiscard]] NoteControls controls(int id) const {
		const Slot& n = slot_[id];
		return {n.pitch, n.pres, n.rate, n.timbre, n.velocity, n.release, (uint8_t)(n.on ? 1 : 0)};
	}
	[[nodiscard]] int find(int channel, int key) const {
		for (int s = 0; s < Notes; s++)
			if (slot_[s].channel == channel && slot_[s].key == key) return s;
		return -1;
	}
	// a slot's pitch: its key, the instrument's bend and its channel's (or its own, absolute)
	[[nodiscard]] float pitch_of(int s) const {
		const Slot& m = slot_[s];
		return m.absolute ? m.abs_pitch + bend_ : m.key + bend_ + (member(m.channel) ? chan_bend_[m.channel] : 0.0f);
	}
	void set_pitch(int s) {
		slot_[s].pitch = pitch_of(s);
		mark(s, CHANGED);
	}

	// --- MIDI 1.0 (type 2) ---
	template <class Host> void midi1(Host& h, uint8_t channel, uint8_t status, uint8_t d1, uint8_t d2) {
		if (status == 0x80 || (status == 0x90 && d2 == 0)) {
			// (always: a note-off of a note that sounds is never an echo)
			note_off(channel, d1, (status == 0x80 ? d2 : 0) * (1.0f / 127.0f));
			return;
		}
		const bool note_data = status == 0x90 || status == 0xa0 || status == 0xd0 || (status == 0xe0 && member(channel)) ||
		    (status == 0xb0 && (d1 == 1 || d1 == 74));
		if (note_data) {
			if (h.echoed(channel)) return; // the host's own notes, echoed
			switch (status) {
			case 0x90: note_on(channel, d1, d2 * (1.0f / 127.0f)); break;
			case 0xa0: pressure(channel, d1, d2 * (1.0f / 127.0f)); break;
			case 0xd0: pressure(channel, -1, d1 * (1.0f / 127.0f)); break;
			case 0xe0: bend(h, channel, (float)((int)((d2 << 7) + d1) - 0x2000) * (1.0f / 8192)); break;
			default: // CC1 (mod wheel, 0..1) or CC74 (64 = 0)
				timbre(channel, d1 == 1 ? d2 * (1.0f / 127.0f) : clampf((d2 - 64) * (1.0f / 64.0f), -1.0f, 1.0f));
				break;
			}
			return;
		}
		if (status == 0xb0) {
			if (rpn_control(channel, d1, d2)) return;
			if (d1 == 6) { // data entry
				if (rpn_is(channel, 0, 0)) {
					// the notes' bend range: at least 1 (0 divides by zero); an MPE host sends one
					// per channel, the master channel's is not the notes'
					if (d2 >= 1 && !h.zone_master(channel)) h.set_bend_range(d2);
				} else if (rpn_is(channel, 0, 6)) {
					// the MPE Configuration Message: the lower zone (master channel 1) with d2
					// member channels (0: none); the upper zone (master 16) isn't supported
					if (channel == 0 && d2 <= 15) set_members(d2);
					h.zone_configured(channel, d2);
				}
			} else if (d1 == 64) {
				h.set_sustain((float)d2 * (1.0f / 127.0f));
			}
			return;
		}
		if (status == 0xe0) instrument_bend(h, channel, (float)((int)((d2 << 7) + d1) - 0x2000) * (2.0f / 8192));
	}

	// --- MIDI 2.0 (type 4) ---
	template <class Host> void midi2(Host& h, uint32_t w0, uint32_t w1) {
		const uint8_t op = (uint8_t)((w0 >> 20) & 0xf), channel = (uint8_t)((w0 >> 16) & 0x0f);
		const uint8_t d1 = (uint8_t)((w0 >> 8) & 0x7f), d2 = (uint8_t)(w0 & 0xff);
		if (op == op_note_off) {
			note_off(channel, d1, velocity16(w1));
			return;
		}
		const bool note_data = op == op_note_on || op == op_poly_pressure || op == op_channel_pressure || op == op_registered_pnc || op == op_assignable_pnc ||
		    (op == op_pitch_bend && member(channel)) || (op == op_control_change && (d1 == 1 || d1 == 74));
		if (note_data) {
			if (h.echoed(channel)) return;
			switch (op) {
			case op_note_on: note_on(channel, d1, velocity16(w1)); break;
			case op_poly_pressure: pressure(channel, d1, unsigned32(w1, range_[channel])); break;
			case op_channel_pressure: pressure(channel, -1, unsigned32(w1, range_[channel])); break;
			case op_pitch_bend: bend(h, channel, bipolar32(w1)); break;
			case op_registered_pnc:
				if (d2 == RPNC_PITCH) { // Pitch 7.25: the note's absolute pitch
					const int s = find(channel, d1);
					if (s >= 0) {
						slot_[s].abs_pitch = pitch725(w1);
						slot_[s].absolute = true;
						set_pitch(s);
					}
				} else if (d2 == RPNC_BRIGHTNESS) { // the note's own timbre (bipolar, as CC74)
					note_timbre(channel, d1, bipolar32(w1));
				}
				break;
			case op_assignable_pnc:
				if (d2 == apnc_pressure_rate) {
					const int s = find(channel, d1);
					if (s >= 0) {
						Slot& m = slot_[s];
						m.sensor = true;
						m.strike = false;
						m.rate = bipolar32(w1, range_[channel]);
						mark(s, CHANGED);
					}
				} else if (d2 == apnc_timbre) {
					note_timbre(channel, d1, bipolar32(w1, range_[channel]));
				}
				break;
			default: // CC1 (0 .. 1) or CC74 (bipolar)
				timbre(channel, d1 == 1 ? unsigned32(w1) : bipolar32(w1));
				break;
			}
			return;
		}
		const uint8_t i7 = (uint8_t)(w0 & 0x7f);
		if (op == op_registered_controller) { // bank d1, index i7
			const int top = (int)(w1 >> 25);
			if (d1 == 0 && i7 == 0) {
				// the bend range: semitones in the top 7 bits, cents in the next 7 (RPN 0's MSB, LSB)
				if (top >= 1 && !h.zone_master(channel)) h.set_bend_range((float)top + (float)((w1 >> 18) & 0x7f) * 0.01f);
			} else if (d1 == 0 && i7 == 6) {
				if (channel == 0 && top <= 15) set_members(top);
				h.zone_configured(channel, top);
			}
		} else if (op == op_assignable_controller && d1 == 0) {
			if (i7 == ac_sensor_range) {
				const double r = fixed428(w1);
				range_[channel] = r < 1.0 ? 1.0 : r;
			} else if (i7 == ac_accel) {
				h.set_accel(unsigned32(w1, 4.0));
			} else if (i7 >= ac_spin && i7 < ac_spin + 3) {
				h.set_spin(i7 - ac_spin, bipolar32(w1));
			}
		} else if (op == op_control_change && d1 == 64) {
			h.set_sustain(unsigned32(w1));
		} else if (op == op_pitch_bend) {
			instrument_bend(h, channel, bipolar32(w1) * 2.0f);
		}
	}

	// the RPN selected on each channel by CC 101/100 (an NRPN select, CC 99/98, deselects it;
	// 0x7f/0x7f, none, at the start: a bare Data Entry selects nothing). True: one of the selects.
	bool rpn_control(int channel, uint8_t cc, uint8_t value) {
		switch (cc) {
		case 0x64: rpn_lsb_[channel] = value; return true;
		case 0x65: rpn_msb_[channel] = value; return true;
		case 0x62:
		case 0x63: rpn_lsb_[channel] = rpn_msb_[channel] = 0x7f; return true;
		default: return false;
		}
	}
	[[nodiscard]] bool rpn_is(int channel, uint8_t msb, uint8_t lsb) const { return rpn_lsb_[channel] == lsb && rpn_msb_[channel] == msb; }

	// a bend of the whole instrument (outside the zone), unless it is the echo of the host's own bends
	template <class Host> void instrument_bend(Host& h, int channel, float semitones) {
		if (h.bends_out_on(channel)) return;
		h.bend_moved(semitones - bend_);
		bend_ = semitones;
		update(-1);
	}

	void note_on(int channel, int key, float velo) {
		int s = find(channel, key);
		for (int n = 0; s < 0 && n < Notes; n++)
			if (slot_[n].channel < 0) s = n;
		if (s < 0) { // all in use: the oldest, whose sound ends
			s = 0;
			for (int n = 1; n < Notes; n++)
				if (slot_[n].age < slot_[s].age) s = n;
			mark(s, SILENCED);
		}
		Slot& m = slot_[s];
		m.channel = (int8_t)channel;
		m.key = (uint8_t)key;
		m.age = ++age_;
		m.absolute = false;
		m.own_timbre = false;
		m.sensor = false;
		m.pressure = chan_pres_set_[channel]; // MPE: the pressure sent before the Note On
		m.pitch = pitch_of(s);
		m.pres = held_pres(m.pressure ? chan_pres_[channel] : velo);
		m.rate = velo;
		m.timbre = chan_timbre_[channel];
		m.velocity = velo;
		m.on = true;
		m.strike = true;
		fading_ = true;
		mark(s, STARTED | CHANGED);
	}

	// release: the Note Off's velocity
	void note_off(int channel, int key, float release) {
		const int s = find(channel, key);
		if (s < 0) return;
		Slot& m = slot_[s];
		if (!m.sensor) {
			m.pres = 0.0f;
			m.rate = -release;
		}
		m.release = release;
		m.on = false;
		mark(s, CHANGED);
		m.channel = -1;
		bool any = false;
		for (const Slot& x : slot_) any |= x.channel == channel;
		if (!any) chan_pres_set_[channel] = false; // pressure is per note (MPE)
	}

	// pressure of the notes of a channel (key -1: channel pressure)
	void pressure(int channel, int key, float p) {
		if (key < 0) {
			chan_pres_[channel] = p;
			chan_pres_set_[channel] = true;
		}
		for (int s = 0; s < Notes; s++) {
			Slot& m = slot_[s];
			if (m.channel == channel && (key < 0 || m.key == key)) {
				m.pressure = true;
				m.pres = m.sensor ? p : held_pres(p);
				mark(s, CHANGED);
			}
		}
	}

	// a member channel's bend, -1 .. 1 (x the bend range)
	template <class Host> void bend(Host& h, int channel, float b) {
		chan_bend_[channel] = b * h.bend_range();
		update(channel);
	}

	// one note's own timbre (MIDI 2.0 per-note): from now on it, not the channel's CC74
	void note_timbre(int channel, int key, float y) {
		const int s = find(channel, key);
		if (s < 0) return;
		slot_[s].own_timbre = true;
		slot_[s].timbre = y;
		mark(s, CHANGED);
	}
	// a channel's timbre (a note with its own keeps it)
	void timbre(int channel, float y) {
		chan_timbre_[channel] = y;
		for (int s = 0; s < Notes; s++)
			if (slot_[s].channel == channel && !slot_[s].own_timbre) {
				slot_[s].timbre = y;
				mark(s, CHANGED);
			}
	}

	// the pitches of the notes of a channel (-1: all) after a change of its bend
	void update(int channel) {
		for (int s = 0; s < Notes; s++)
			if (slot_[s].channel >= 0 && (channel < 0 || slot_[s].channel == channel)) set_pitch(s);
	}

	Slot slot_[Notes];
	uint8_t queue_[Notes] = {};
	int queued_ = 0;
	bool fading_ = false;
	uint32_t age_ = 0;
	int members_ = 15;
	float bend_ = 0.0f;        // the instrument's bend (semitones)
	float chan_bend_[16] = {}; // semitones, member channels
	float chan_timbre_[16] = {};
	float chan_pres_[16] = {};
	bool chan_pres_set_[16] = {};
	double range_[16] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
	uint8_t rpn_lsb_[16] = {0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f};
	uint8_t rpn_msb_[16] = {0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f};
};

} // namespace awva::ump

#endif // AWVA_UMP_H_
