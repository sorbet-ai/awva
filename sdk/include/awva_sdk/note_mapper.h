// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// MIDI in -> the engines' voice controls, by the same code an embedded host runs: the decoder
// (awva/ump.h: MIDI 2.0 and MIDI 1.0 / MPE in UMP, the notes and the instrument's messages: the
// bend range, the MCM, the instrument's bend, the sustain pedal, motion) and the voices
// (awva/voice_allocator.h, awva/mono.h). What an instrument does beyond them (its MIDI out, its
// other CCs) a host doesn't need here.
//
// Its tables are the host's size: NoteMapper<MaxNotes, MaxVoices> holds MaxNotes notes at once (a
// note is its (channel, note number); with every slot held a new note takes the oldest one) on at
// most MaxVoices voices. TimedMidi uses the build's sizes, AWVA_MIDI_NOTES and AWVA_MIDI_VOICES.
//
// Time: MIDI is taken in fixed steps, the host's control rate (set_step(): 2 ms unless it says
// another): message() and ump() queue, step() runs one step (the voices' level decay, the strike
// fade if one is fading, then the messages queued since the last step), each factor from its time
// constant. The controls of each voice are in voices().
#ifndef AWVA_SDK_NOTE_MAPPER_H_
#define AWVA_SDK_NOTE_MAPPER_H_

#include <algorithm>
#include <array>
#include <cstdint>

#include "awva/engine.h"
#include "awva/mono.h"
#include "awva/ump.h"
#include "awva/voice_allocator.h"

#ifndef AWVA_MIDI_STEP_QUEUE
#define AWVA_MIDI_STEP_QUEUE 512 // MIDI messages a control step takes
#endif

namespace awva::sdk {

template <int MaxNotes, int MaxVoices> class NoteMapper {
	static_assert(MaxNotes >= 1 && MaxVoices >= 1, "a note and a voice at least");

public:
	static constexpr int QUEUE = AWVA_MIDI_STEP_QUEUE; // messages per step (a build's)
	static constexpr int NOTES = MaxNotes;             // notes held at once (one more takes the oldest)
	static constexpr int MAX_VOICES = MaxVoices;

	explicit NoteMapper(int voices) :
	    g_(dsp::instrument_at_rest), nv_(std::clamp(voices, 1, MaxVoices)) {
		vc_.fill(note_at_rest);
		alloc_.set_voices(nv_);
	}

	// one MIDI message (status byte first), for the next step; false: the queue is full
	bool message(const uint8_t* m, int len) {
		if (len < 1 || m[0] < 0x80 || m[0] >= 0xf0) return true; // (system messages: nothing to play)
		// (as MIDI 1.0 in UMP: type 2)
		return ump(0x20000000u | (uint32_t)m[0] << 16 | (uint32_t)(len > 1 ? m[1] & 0x7f : 0) << 8 | (uint32_t)(len > 2 ? m[2] & 0x7f : 0), 0);
	}
	bool message(uint8_t status, uint8_t d1, uint8_t d2) {
		const uint8_t m[3] = {status, d1, d2};
		return message(m, 3);
	}
	// one UMP packet (MIDI 2.0: type 4, two words; MIDI 1.0: type 2, w1 unused), for the next step
	bool ump(uint32_t w0, uint32_t w1) {
		const uint32_t t = w0 >> 28;
		if (t != 2 && t != 4) return true; // (only channel voice messages play)
		if (queued_ == QUEUE) return false;
		queue_[queued_][0] = w0;
		queue_[queued_][1] = w1;
		queued_++;
		return true;
	}
	// one step: the voices' level decay, the strike fade, then the queued messages
	void step() {
		alloc_.tick(*this);
		if (dec_.fading()) dec_.tick(*this);
		for (int i = 0; i < queued_; i++) {
			dec_.packet(*this, queue_[i]);
			dec_.flush(*this);
		}
		queued_ = 0;
		if (mono_on_ && !mono_.empty()) { // the player: the voice from every held key, this step
			mono_.step(step_s_);
			vc_[0] = mono_.voice();
		}
	}
	// the step's length in seconds (2 ms unless set)
	void set_step(double seconds) {
		alloc_.set_step(seconds);
		dec_.set_step(seconds);
		step_s_ = static_cast<float>(seconds);
	}

	[[nodiscard]] const NoteControls* voices() const { return vc_.data(); }
	[[nodiscard]] int num_voices() const { return nv_; }
	// mono (mono.h): one voice, played by every held key; the held notes are forgotten on a
	// switch, and the voices beyond the first go quiet
	void set_mono(bool on) {
		if (on == mono_on_) return;
		mono_on_ = on;
		mono_.clear();
		if (on)
			for (int v = 1; v < nv_; v++) {
				NoteControls& c = vc_[static_cast<size_t>(v)];
				c.pressure = c.pressure_rate = 0.0f;
				c.gate = 0;
			}
	}
	[[nodiscard]] bool mono() const { return mono_on_; }
	// how mono's voice goes note to note: the engine's family and the player's thresholds (mono.h)
	void set_mono_style(const voices::MonoStyle& m) { mono_.set_style(m); }
	[[nodiscard]] const voices::MonoStyle& mono_style() const { return mono_.style(); }
	[[nodiscard]] const dsp::InstrumentControls& globals() const { return g_; }
	[[nodiscard]] int notes_held() const {
		int n = 0;
		for (int s = 0; s < NOTES; s++) n += dec_.channel(s) >= 0;
		return n;
	}
	[[nodiscard]] int members() const { return dec_.members(); }

	// the decoder's host (ump.h)
	// a note starts: held, its level, a voice
	void note_started(int s, const NoteControls& n) {
		held_[s] = true;
		alloc_.set_level(s, n.pressure + n.pressure_rate); // a new note: no old estimate
		if (mono_on_) return;                              // (its voice is the mono voice, at note_changed)
		(void)alloc_.start(*this, s);
	}
	void note_changed(int s, const NoteControls& n) {
		held_[s] = n.gate != 0;
		floor_[s] = n.gate ? n.pressure + n.pressure_rate : -1.0f;
		alloc_.raise_level(s, floor_[s]);
		if (mono_on_) {
			write_mono(s, n);
			return;
		}
		const int v = alloc_.voice(s);
		if (v < 0) return;
		vc_[static_cast<size_t>(v)] = n;
	}
	void note_silenced(int s) {
		if (mono_on_) { // (the note leaves the player's keys)
			NoteControls c = vc_[0];
			c.pressure = c.pressure_rate = 0.0f;
			c.gate = 0;
			write_mono(s, c);
			return;
		}
		const int v = alloc_.voice(s);
		if (v < 0) return;
		NoteControls& c = vc_[static_cast<size_t>(v)];
		c.pressure = c.pressure_rate = 0.0f;
		c.gate = 0;
	}
	void note_dropped(int s) {
		note_silenced(s);
		held_[s] = false;
		floor_[s] = -1.0f;
	}
	[[nodiscard]] bool echoed(int) const { return false; } // (nothing goes out)
	[[nodiscard]] float bend_range() const { return bend_range_; }
	// (the default MPE lower zone: channel 1 is the zone master)
	[[nodiscard]] bool zone_master(int channel) const { return channel == 0; }
	void set_bend_range(float semitones) { bend_range_ = semitones; }
	void zone_configured(int, int) {}
	void set_sustain(float p) { g_.sustain = p; }
	[[nodiscard]] bool bends_out_on(int) const { return false; }
	void bend_moved(float) {}
	void set_accel(float g) { g_.motion.accel_g = g; }
	void set_spin(int axis, float s) { g_.motion.spin[axis] = s; }
	// VoiceAllocator's host: a note slot's key is down, its level floor
	[[nodiscard]] bool held(int s) const { return held_[s]; }
	[[nodiscard]] float floor(int s) const { return floor_[s]; }

private:
	// mono: every held key plays the one voice (mono.h); the voice is the player's, each step; the
	// last key's release is the voice's at once
	void write_mono(int s, const NoteControls& n) {
		mono_.note(s, n);
		if (mono_.empty()) {
			if (alloc_.voice(s) < 0) return;
			vc_[0] = mono_.voice();
			return;
		}
		const int top = mono_.top();
		if (alloc_.voice(top) < 0) alloc_.take(top, 0);
	}

	awva::ump::Decoder<MaxNotes> dec_;
	voices::VoiceAllocator<MaxNotes, MaxVoices> alloc_;
	std::array<NoteControls, MaxVoices> vc_;
	dsp::InstrumentControls g_;
	int nv_ = 0;
	voices::Mono<MaxNotes> mono_;
	bool mono_on_ = false;
	float step_s_ = 0.002f; // the control step's length
	float bend_range_ = 48.0f;
	bool held_[MaxNotes] = {};
	float floor_[MaxNotes] = {};
	uint32_t queue_[QUEUE][2]; // UMP packets
	int queued_ = 0;
};

} // namespace awva::sdk

#endif // AWVA_SDK_NOTE_MAPPER_H_
