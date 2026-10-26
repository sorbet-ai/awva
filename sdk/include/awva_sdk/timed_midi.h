// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
//
// MIDI with time, in integer frames: the one step rule of the component (component/include/nsc),
// the Shell and the renderer's MIDI player. MIDI is taken in steps of the host's control rate
// (HostConfig::control_hz): at h Hz step k is at frame ceil(k sr / h) (500 Hz: at 48 kHz every 96
// frames, at 44.1 kHz 88 or 89, never drifting); at 0 (the default) every engine block, step k at
// frame k BLOCK. Before a block that starts at frame f, every step at or before f runs, each with
// the events whose frame is at or before the step's. The voices' level decay and the strike's
// fade a step follow from their time constants (MidiVoices::set_step): at 500 Hz they are the
// 2 ms factors themselves. An event is UMP (MIDI 2.0 channel voice, type 4;
// MIDI 1.0 in UMP, type 2) or MIDI 1.0 bytes. Fixed size, no heap (it can live in a component's
// state).
//
// Notes and the host's note ids: a Note On may carry the host's id for the note (a CLAP note id;
// -1: none). The note is tracked by (channel, key) until it has ended: released and then either
// its tail (the engine's tail_seconds) has passed or the engine is idle, or another note took
// its key. ended() hands out the ids of the notes that ended (CLAP NOTE_END).
//
// Every Note On's id comes back from ended() exactly once (ids on other events are ignored; a
// MIDI 1.0 Note On of velocity 0 is a Note Off). The table and the list below are fixed, so:
// - NOTES notes are tracked at once. A Note On with an id when all are taken ends the oldest
//   tracked note (the oldest released one, else the oldest held one: MIDI in holds 16 notes, so
//   by then that one has long been dropped) and its id goes to the ended list at once.
// - A Note On with an id that can't be queued (the queue is full) ends at once the same way.
// - reset() ends every tracked note and every queued Note On at once: at most NOTES + QUEUE ids.
// - The ended list holds ENDED ids; it can't fill if the host calls ended() after each block
//   until it returns less than max, and gives at most MAX_EVENTS events a block (a block feeds
//   at most QUEUE events, so at most QUEUE + MAX_EVENTS ids go to it between two calls; with a
//   reset() between them, NOTES + QUEUE from it and MAX_EVENTS from the block after it). Past
//   that (a host that doesn't drain it) further ids are lost, never written out of bounds.
#ifndef AWVA_SDK_TIMED_MIDI_H_
#define AWVA_SDK_TIMED_MIDI_H_

#include <cstdint>
#include <cstring>
#include <new>

#include "awva_sdk/host_config.h"
#include "awva_sdk/midi_voices.h"

// The tables' sizes, a build's (a host with little RAM sets them smaller; the descriptor of a
// component reports them: nsc_desc max_events, max_note_ids)
#ifndef AWVA_MIDI_QUEUE
#define AWVA_MIDI_QUEUE 1024 // events waiting for their step (and a block's at most)
#endif
#ifndef AWVA_NOTE_IDS
#define AWVA_NOTE_IDS 64 // notes with a host id tracked at once
#endif

namespace awva::sdk {

// the frame of step k: at control_hz > 0 ceil(k sample_rate / control_hz), at 0 k BLOCK
[[nodiscard]] constexpr uint64_t step_frame(uint64_t k, int sample_rate, int control_hz) {
	if (control_hz <= 0) return k * static_cast<uint64_t>(dsp::BLOCK);
	const uint64_t h = static_cast<uint64_t>(control_hz);
	return (k * static_cast<uint64_t>(sample_rate) + h - 1u) / h;
}
// a step's length in seconds (at control_hz > 0 the nominal 1 / control_hz)
[[nodiscard]] constexpr double step_seconds(int sample_rate, int control_hz) {
	return control_hz > 0 ? 1.0 / control_hz : static_cast<double>(dsp::BLOCK) / sample_rate;
}

class TimedMidi {
public:
	static constexpr int QUEUE = AWVA_MIDI_QUEUE;            // events waiting for their step
	static constexpr int NOTES = AWVA_NOTE_IDS;              // notes with a host id, tracked until they end
	static constexpr int MAX_EVENTS = AWVA_MIDI_QUEUE;       // events a block, for the guarantee above
	static constexpr int ENDED = QUEUE + MAX_EVENTS + NOTES; // ids ended at once, until ended() hands them out

	explicit TimedMidi(const HostConfig& c) :
	    midi_(c.voices), sr_(c.sample_rate), hz_(c.control_hz < 0 ? 0 : c.control_hz), voices_(c.voices) {
		midi_.set_step(step_seconds(sr_, hz_));
	}

	// another control rate (Hz; 0: every engine block), from the next step on: the grid starts
	// again at the frame the last advance() reached
	void set_control_hz(int hz) {
		hz_ = hz < 0 ? 0 : hz;
		midi_.set_step(step_seconds(sr_, hz_));
		if (steps_ > 0) {
			base_ = now_;
			steps_ = 1; // (the step at now_ has run)
		}
	}
	[[nodiscard]] int control_hz() const { return hz_; }

	// Back to the start, as a fresh TimedMidi (a host's reset: the engine goes silent), mono and
	// its style kept. The guarantee holds through it: every tracked note's id, and the id of every queued Note
	// On that will now never sound, ends here and comes back from the next ended(), after the ids
	// that had ended before and were not handed out yet.
	void reset() {
		for (Note& t : notes_)
			if (t.id >= 0) end_now(t.id);
		for (int i = 0; i < queued_; i++)
			if (queue_[i].note_id >= 0 && starts_note(queue_[i])) end_now(queue_[i].note_id);
		const bool mono = midi_.mono();
		const voices::MonoStyle style = midi_.mono_style();
		// (in place: a MidiVoices is kilobytes, which a temporary would put on the caller's stack)
		midi_.~MidiVoices();
		new (&midi_) MidiVoices(voices_);
		midi_.set_step(step_seconds(sr_, hz_));
		midi_.set_mono(mono);
		midi_.set_mono_style(style);
		queued_ = 0;
		steps_ = now_ = base_ = 0;
		for (Note& t : notes_) t = Note{};
		ages_ = 0;
	}

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
		for (; base_ + step_frame(steps_, sr_, hz_) <= f; steps_++) {
			const uint64_t fs = base_ + step_frame(steps_, sr_, hz_);
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
		for (; n < max && done_n_ > 0; done_n_--) { // the ones ended at once, first
			ids[n++] = done_[done_head_];
			done_head_ = (done_head_ + 1) % ENDED;
		}
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
		uint64_t age = 0; // (its Note On's order)
	};

	bool push(const Event& e) {
		if (queued_ == QUEUE) {
			if (e.note_id >= 0 && starts_note(e)) end_now(e.note_id); // (it will never sound)
			return false;
		}
		queue_[queued_++] = e;
		return true;
	}

	// a sounding Note On (MIDI 1.0 bytes, MIDI 1.0 in UMP, MIDI 2.0)
	static bool starts_note(const Event& e) {
		if (e.len > 0) return (e.bytes[0] & 0xf0) == 0x90 && e.len > 2 && e.bytes[2] > 0;
		if ((e.w[0] >> 28) == 2) return ((e.w[0] >> 20) & 0xf) == 0x9 && (e.w[0] & 0x7f) > 0;
		return ((e.w[0] >> 20) & 0xf) == 0x9;
	}

	// an id to hand out at the next ended()
	void end_now(int32_t id) {
		if (done_n_ == ENDED) return; // (the host doesn't drain the list: see above)
		done_[(done_head_ + done_n_) % ENDED] = id;
		done_n_++;
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
			(void)midi_.ump(e.w[0], e.w[1]);
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
		if (on && e.note_id >= 0) {
			Note* slot = nullptr;
			for (Note& t : notes_)
				if (t.id < 0) {
					slot = &t;
					break;
				}
			if (!slot) { // all taken: the oldest released note ends now, else the oldest held one
				for (Note& t : notes_)
					if (!slot || (t.released && !slot->released) || (t.released == slot->released && t.age < slot->age)) slot = &t;
				end_now(slot->id);
			}
			*slot = Note{e.note_id, ch, d1, false, false, 0, ages_++};
		}
	}

	MidiVoices midi_;
	int sr_, hz_, voices_;
	Event queue_[QUEUE];
	int queued_ = 0;
	uint64_t steps_ = 0, now_ = 0, base_ = 0;
	Note notes_[NOTES];
	uint64_t ages_ = 0;
	int32_t done_[ENDED];
	int done_head_ = 0, done_n_ = 0;
};

} // namespace awva::sdk

#endif // AWVA_SDK_TIMED_MIDI_H_
