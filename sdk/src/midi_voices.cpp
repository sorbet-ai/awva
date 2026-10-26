// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// MIDI in -> voice controls on the PC (midi_voices.h): the shared units, this host's controls.
#include "awva_sdk/midi_voices.h"

#include <algorithm>

namespace awva::sdk {

MidiVoices::MidiVoices(int voices) :
    g_(dsp::globals_at_rest), nv_(std::clamp(voices, 1, MAX_VOICES)) {
	vc_.fill(dsp::voice_at_rest);
	alloc_.set_voices(nv_);
}

void MidiVoices::set_legato(bool on) {
	if (on == legato_on_) return;
	legato_on_ = on;
	legato_.clear();
	if (on)
		for (int v = 1; v < nv_; v++) vc_[static_cast<size_t>(v)].pres = vc_[static_cast<size_t>(v)].vpres = 0.0f;
}

// mono legato: the one voice plays the newest held note at the strongest held
// note's pressure; a release returns to the newest still held; the last note's release is the voice's
void MidiVoices::write_legato(int s, const midi::NoteSignals& n) {
	legato_.note(s, n);
	midi::NoteSignals sig = n;
	if (legato_.empty()) {
		if (alloc_.voice(s) < 0) return;
	} else {
		const int top = legato_.top();
		if (alloc_.voice(top) < 0) alloc_.take(top, 0);
		sig = legato_.signals();
	}
	dsp::VoiceControls& c = vc_[0];
	c.note = sig.note;
	c.pres = sig.pres;
	c.vpres = sig.vpres;
	c.but_x = sig.but_x;
	c.but_y = std::clamp(sig.but_y, -1.0f, 1.0f);
}

int MidiVoices::notes_held() const {
	int n = 0;
	for (int s = 0; s < midi::NOTES; s++) n += in_.channel(s) >= 0;
	return n;
}

bool MidiVoices::message(const uint8_t* m, int len) {
	if (len < 1 || m[0] < 0x80 || m[0] >= 0xf0) return true; // (system messages: nothing to play)
	if (queued_ == QUEUE) return false;
	queue_[queued_][0] = (uint32_t)m[0] << 16 | (uint32_t)(len > 1 ? m[1] & 0x7f : 0) << 8 | (uint32_t)(len > 2 ? m[2] & 0x7f : 0);
	queue_[queued_][1] = 0xffffffffu;
	queued_++;
	return true;
}

bool MidiVoices::ump2(uint32_t w0, uint32_t w1) {
	if ((w0 >> 28) != 4) return true; // (only MIDI 2.0 channel voice messages play)
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
		const uint32_t w0 = queue_[i][0], w1 = queue_[i][1];
		if (w1 == 0xffffffffu && (w0 >> 24) == 0) apply((uint8_t)(w0 >> 16), (uint8_t)(w0 >> 8), (uint8_t)w0);
		else if (!in_.message2(*this, w0, w1)) (void)instrument_.message2(*this, in_, w0, w1);
		dec_.flush(*this);
	}
	queued_ = 0;
}

void MidiVoices::apply(uint8_t status, uint8_t d1, uint8_t d2) {
	const uint8_t channel = status & 0x0f;
	status &= 0xf0;
	if (in_.message(*this, channel, status, d1, d2)) return;
	(void)instrument_.message(*this, in_, channel, status, d1, d2);
}

// a note starts: held, its level, a voice
void MidiVoices::note_started(int s, const midi::NoteSignals& n) {
	held_[s] = true;
	alloc_.set_level(s, n.pres + n.vpres); // a new note: no old estimate
	if (legato_on_) return;                // (its voice is the legato voice's, at note_changed)
	(void)alloc_.start(*this, s);
}

void MidiVoices::note_changed(int s, const midi::NoteSignals& n) {
	held_[s] = n.on;
	floor_[s] = n.on ? n.pres + n.vpres : -1.0f;
	alloc_.raise_level(s, floor_[s]);
	if (legato_on_) {
		write_legato(s, n);
		return;
	}
	const int v = alloc_.voice(s);
	if (v < 0) return;
	dsp::VoiceControls& c = vc_[static_cast<size_t>(v)];
	c.note = n.note;
	c.pres = n.pres;
	c.vpres = n.vpres;
	c.but_x = n.but_x;
	c.but_y = std::clamp(n.but_y, -1.0f, 1.0f);
}

void MidiVoices::note_silenced(int s) {
	if (legato_on_) { // (the note leaves the legato stack)
		write_legato(s, midi::NoteSignals{vc_[0].note, 0.0f, 0.0f, vc_[0].but_x, vc_[0].but_y, false});
		return;
	}
	const int v = alloc_.voice(s);
	if (v < 0) return;
	vc_[static_cast<size_t>(v)].pres = 0.0f;
	vc_[static_cast<size_t>(v)].vpres = 0.0f;
}

void MidiVoices::note_dropped(int s) {
	note_silenced(s);
	held_[s] = false;
	floor_[s] = -1.0f;
}

} // namespace awva::sdk
