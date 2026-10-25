// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
//
// MIDI with time, in integer frames: the one step rule of the component (component/include/nsc),
// the Shell and the renderer's MIDI player. MIDI is taken in 2 ms steps, as the board takes it:
// step k is at frame ceil(k sr / 500) (at 48 kHz every 96 frames, at 44.1 kHz 88 or 89, never
// drifting); before a block that starts at frame f, every step at or before f runs, each with the
// events whose frame is at or before the step's. An event is UMP (MIDI 2.0 channel voice, type 4;
// MIDI 1.0 in UMP, type 2) or MIDI 1.0 bytes. Fixed size, no heap (it can live in a component's
// state).
//
// Notes and the host's note ids: a Note On may carry the host's id for the note (a CLAP note id;
// -1: none). The note is tracked by (channel, key) until it has ended: released and then either
// its tail (the engine's tail_seconds) has passed or the engine is idle, or another note took
// its key. ended() hands out the ids of the notes that ended (CLAP NOTE_END).
#ifndef AWVA_SDK_TIMED_MIDI_H_
#define AWVA_SDK_TIMED_MIDI_H_

#include <cstdint>
#include <cstring>

#include "awva_sdk/midi_voices.h"

namespace awva::sdk {

[[nodiscard]] constexpr uint64_t step_frame(uint64_t k, int sample_rate) {
	return (k * static_cast<uint64_t>(sample_rate) + 499u) / 500u;
}

class TimedMidi {
public:
	static constexpr int QUEUE = 1024; // events waiting for their step
	static constexpr int NOTES = 64;   // notes with a host id, tracked until they end

	explicit TimedMidi(int voices, int sample_rate) :
	    midi_(voices), sr_(sample_rate) {}

	// a UMP packet at a frame (frames don't go back; 1 or 2 words of a type 2 / 4 packet)
	bool ump(uint64_t frame, const uint32_t* words, int32_t note_id = -1) {
		const uint32_t t = words[0] >> 28;
		if (t != 2 && t != 4) return true; // (nothing to play)
		Event e{frame, {words[0], t == 4 ? words[1] : 0u}, {}, 0, note_id};
		return push(e);
	}
	// MIDI 1.0 bytes (one message) at a frame
	bool midi1(uint64_t frame, const uint8_t* m, int len, int32_t note_id = -1) {
		if (len < 1 || len > 3) return true;
		Event e{frame, {}, {}, static_cast<uint8_t>(len), note_id};
		std::memcpy(e.bytes, m, static_cast<size_t>(len));
		return push(e);
	}

	// the steps due by a block that starts at frame f
	void advance(uint64_t f) {
		int taken = 0;
		for (; step_frame(steps_, sr_) <= f; steps_++) {
			const uint64_t fs = step_frame(steps_, sr_);
			for (; taken < queued_ && queue_[taken].frame <= fs; taken++) feed(queue_[taken]);
			midi_.step();
		}
		if (taken > 0) {
			std::memmove(queue_, queue_ + taken, sizeof(Event) * static_cast<size_t>(queued_ - taken));
			queued_ -= taken;
		}
		now_ = f;
	}

	// the ids of notes that have ended (at most max); tail_frames: the engine's tail, idle: the
	// engine has no voice sounding
	int ended(int32_t* ids, int max, uint64_t tail_frames, bool idle) {
		int n = 0;
		for (Note& t : notes_) {
			if (n >= max) break;
			if (t.id < 0 || !t.released) continue;
			if (idle || now_ >= t.released_at + tail_frames || t.taken) {
				ids[n++] = t.id;
				t.id = -1;
			}
		}
		return n;
	}

	[[nodiscard]] MidiVoices& midi() { return midi_; }
	[[nodiscard]] const MidiVoices& midi() const { return midi_; }

private:
	struct Event {
		uint64_t frame;
		uint32_t w[2];
		uint8_t bytes[3];
		uint8_t len; // MIDI 1.0 bytes (0: UMP)
		int32_t note_id;
	};
	struct Note {
		int32_t id = -1;
		uint8_t channel = 0, key = 0;
		bool released = false, taken = false;
		uint64_t released_at = 0;
	};

	bool push(const Event& e) {
		if (queued_ == QUEUE) return false;
		queue_[queued_++] = e;
		return true;
	}

	void feed(const Event& e) {
		uint8_t status, d1, d2;
		bool on, off;
		if (e.len > 0) {
			status = e.bytes[0];
			d1 = e.len > 1 ? e.bytes[1] : 0;
			d2 = e.len > 2 ? e.bytes[2] : 0;
			(void)midi_.message(e.bytes, e.len);
			on = (status & 0xf0) == 0x90 && d2 > 0;
			off = (status & 0xf0) == 0x80 || ((status & 0xf0) == 0x90 && d2 == 0);
		} else if ((e.w[0] >> 28) == 2) {
			status = static_cast<uint8_t>(e.w[0] >> 16);
			d1 = static_cast<uint8_t>((e.w[0] >> 8) & 0x7f);
			d2 = static_cast<uint8_t>(e.w[0] & 0x7f);
			const uint8_t m[3] = {status, d1, d2};
			const int len = (status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0 ? 2 : 3;
			(void)midi_.message(m, len);
			on = (status & 0xf0) == 0x90 && d2 > 0;
			off = (status & 0xf0) == 0x80 || ((status & 0xf0) == 0x90 && d2 == 0);
		} else {
			status = static_cast<uint8_t>(e.w[0] >> 16);
			d1 = static_cast<uint8_t>((e.w[0] >> 8) & 0x7f);
			(void)midi_.ump2(e.w[0], e.w[1]);
			on = (status & 0xf0) == 0x90;
			off = (status & 0xf0) == 0x80;
		}
		const uint8_t ch = status & 0x0f;
		if (on || off) {
			for (Note& t : notes_)
				if (t.id >= 0 && t.channel == ch && t.key == d1) {
					if (on) t.taken = t.released = true; // (its key is a new note's now)
					else if (!t.released) {
						t.released = true;
						t.released_at = e.frame;
					}
				}
		}
		if (on && e.note_id >= 0)
			for (Note& t : notes_)
				if (t.id < 0) {
					t = Note{e.note_id, ch, d1, false, false, 0};
					break;
				}
	}

	MidiVoices midi_;
	int sr_;
	Event queue_[QUEUE];
	int queued_ = 0;
	uint64_t steps_ = 0, now_ = 0;
	Note notes_[NOTES];
};

} // namespace awva::sdk

#endif // AWVA_SDK_TIMED_MIDI_H_
