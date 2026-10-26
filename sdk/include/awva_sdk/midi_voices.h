// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// MIDI in -> the engines' voice controls on the PC, by the same code an embedded host runs: the
// decoder (awva/ump.h: MIDI 2.0 and MIDI 1.0 / MPE in UMP, the notes and the instrument's
// messages: the bend range, the MCM, the instrument's bend, the sustain pedal, motion) and the
// voices (awva/voice_allocator.h, awva/legato.h). What an instrument does beyond them (its MIDI
// out, its other CCs) a host on the PC doesn't need.
//
// Time: MIDI is taken in fixed steps, the host's control rate (set_step(): 2 ms unless it says
// another): message() and ump() queue, step() runs one step (the voices' level decay, the strike
// fade if one is fading, then the messages queued since the last step), each factor from its time
// constant. The controls of each voice are in voices().
#ifndef AWVA_SDK_MIDI_VOICES_H_
#define AWVA_SDK_MIDI_VOICES_H_

#include <cstdint>
#include <array>

#include "awva/engine.h"
#include "awva/legato.h"
#include "awva/ump.h"
#include "awva/voice_allocator.h"

#ifndef AWVA_MIDI_STEP_QUEUE
#define AWVA_MIDI_STEP_QUEUE 512 // MIDI messages a control step takes
#endif

namespace awva::sdk {

class MidiVoices {
public:
	static constexpr int QUEUE = AWVA_MIDI_STEP_QUEUE; // messages per step (a build's)
	static constexpr int NOTES = 16;                   // notes held at once (a 17th takes the oldest)

	explicit MidiVoices(int voices);

	// one MIDI message (status byte first), for the next step; false: the queue is full
	bool message(const uint8_t* m, int len);
	bool message(uint8_t status, uint8_t d1, uint8_t d2) {
		const uint8_t m[3] = {status, d1, d2};
		return message(m, 3);
	}
	// one UMP packet (MIDI 2.0: type 4, two words; MIDI 1.0: type 2, w1 unused), for the next step
	bool ump(uint32_t w0, uint32_t w1);
	// one step: the voices' level decay, the strike fade, then the queued messages
	void step();
	// the step's length in seconds (2 ms unless set)
	void set_step(double seconds) {
		alloc_.set_step(seconds);
		dec_.set_step(seconds);
	}

	[[nodiscard]] const NoteControls* voices() const { return vc_.data(); }
	[[nodiscard]] int num_voices() const { return nv_; }
	// mono legato (legato.h): one voice, a note pressed while another is held slurs to it; the
	// held notes are forgotten on a switch, and the voices beyond the first go quiet
	void set_legato(bool on);
	[[nodiscard]] bool legato() const { return legato_on_; }
	[[nodiscard]] const dsp::InstrumentControls& globals() const { return g_; }
	[[nodiscard]] int notes_held() const;
	[[nodiscard]] int members() const { return dec_.members(); }

	// the decoder's host (ump.h)
	void note_started(int s, const NoteControls& n);
	void note_changed(int s, const NoteControls& n);
	void note_silenced(int s);
	void note_dropped(int s);
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
	void write_legato(int s, const NoteControls& n);

	awva::ump::Decoder<NOTES> dec_;
	voices::VoiceAllocator<NOTES, 16> alloc_;
	static constexpr int MAX_VOICES = 16;
	std::array<NoteControls, MAX_VOICES> vc_;
	dsp::InstrumentControls g_;
	int nv_ = 0;
	voices::Legato<NOTES> legato_;
	bool legato_on_ = false;
	float bend_range_ = 48.0f;
	bool held_[NOTES] = {};
	float floor_[NOTES] = {};
	uint32_t queue_[QUEUE][2]; // UMP packets
	int queued_ = 0;
};

} // namespace awva::sdk

#endif // AWVA_SDK_MIDI_VOICES_H_
