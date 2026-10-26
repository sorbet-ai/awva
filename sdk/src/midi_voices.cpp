// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// MIDI in -> voice controls on the PC (midi_voices.h): the shared units, this host's controls.
#include "awva_sdk/midi_voices.h"

#include <algorithm>

namespace awva::sdk {

MidiVoices::MidiVoices(int voices) :
    g_(dsp::instrument_at_rest), nv_(std::clamp(voices, 1, MAX_VOICES)) {
	vc_.fill(note_at_rest);
	alloc_.set_voices(nv_);
}

void MidiVoices::set_mono(bool on) {
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

// mono: every held key plays the one voice (mono.h); the voice is the player's, each step; the
// last key's release is the voice's at once
void MidiVoices::write_mono(int s, const NoteControls& n) {
	mono_.note(s, n);
	if (mono_.empty()) {
		if (alloc_.voice(s) < 0) return;
		vc_[0] = mono_.voice();
		return;
	}
	const int top = mono_.top();
	if (alloc_.voice(top) < 0) alloc_.take(top, 0);
}

int MidiVoices::notes_held() const {
	int n = 0;
	for (int s = 0; s < NOTES; s++) n += dec_.channel(s) >= 0;
	return n;
}

bool MidiVoices::message(const uint8_t* m, int len) {
	if (len < 1 || m[0] < 0x80 || m[0] >= 0xf0) return true; // (system messages: nothing to play)
	// (as MIDI 1.0 in UMP: type 2)
	return ump(0x20000000u | (uint32_t)m[0] << 16 | (uint32_t)(len > 1 ? m[1] & 0x7f : 0) << 8 | (uint32_t)(len > 2 ? m[2] & 0x7f : 0), 0);
}

bool MidiVoices::ump(uint32_t w0, uint32_t w1) {
	const uint32_t t = w0 >> 28;
	if (t != 2 && t != 4) return true; // (only channel voice messages play)
	if (queued_ == QUEUE) return false;
	queue_[queued_][0] = w0;
	queue_[queued_][1] = w1;
	queued_++;
	return true;
}

// one control step: the tick (the level decay, the strike fade), then the messages
void MidiVoices::step() {
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

// a note starts: held, its level, a voice
void MidiVoices::note_started(int s, const NoteControls& n) {
	held_[s] = true;
	alloc_.set_level(s, n.pressure + n.pressure_rate); // a new note: no old estimate
	if (mono_on_) return;                              // (its voice is the mono voice, at note_changed)
	(void)alloc_.start(*this, s);
}

void MidiVoices::note_changed(int s, const NoteControls& n) {
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

void MidiVoices::note_silenced(int s) {
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

void MidiVoices::note_dropped(int s) {
	note_silenced(s);
	held_[s] = false;
	floor_[s] = -1.0f;
}

} // namespace awva::sdk
