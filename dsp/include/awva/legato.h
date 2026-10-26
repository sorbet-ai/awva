// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// Mono legato: the internal synth plays one voice, and a note pressed while another is held takes
// that voice over without a new attack (the engine sees the pitch change with the gate held: a
// physical model plays it as a slur). Releasing the newest note returns to the newest one still
// held, again without an attack (last-note priority); with nothing held a new note is a normal
// attack. Pure: no HAL, no heap. The host hands the voice to top() with VoiceAllocator::take and
// writes it what signals() gives.
//
// What the voice plays: the pitch and the tilt of the newest held note (the note being played),
// the pressure and its velocity of the most strongly pressed held note (the dynamic): pressing
// the next note while the last one is still held keeps the level, and as the old note lifts its
// pressure hands over to the new one's, so the dynamic carries over and the new note's own strike
// doesn't tongue the slur.
#ifndef AWVA_LEGATO_H_
#define AWVA_LEGATO_H_

#include "controls.h"

namespace awva::voices {

template <int Max> class Legato {
	static_assert(Max >= 2, "a stack");

public:
	using Signals = awva::NoteControls;

	// note id's controls (any note, in any order): held (its gate, pressure above 0) it is pushed or
	// updated, else it leaves the stack. Past Max held notes the oldest is forgotten.
	void note(int id, const Signals& s) {
		const int at = find(id);
		const bool held = s.gate && s.pressure > 0.0f;
		if (!held) {
			if (at >= 0) remove(at);
			return;
		}
		if (at >= 0) {
			sig_[at] = s;
			return;
		}
		if (n_ == Max) remove(0);
		id_[n_] = id;
		sig_[n_] = s;
		n_++;
	}
	// forget every note (the mode switched, all notes off)
	void clear() { n_ = 0; }

	[[nodiscard]] bool empty() const { return n_ == 0; }
	[[nodiscard]] int size() const { return n_; }
	[[nodiscard]] bool held(int id) const { return find(id) >= 0; }
	// the newest held note: the one the voice plays (-1: none)
	[[nodiscard]] int top() const { return n_ > 0 ? id_[n_ - 1] : -1; }

	// the voice's controls: the newest note's pitch and timbre, the strongest note's pressure and
	// its rate (not empty())
	[[nodiscard]] Signals signals() const {
		Signals s = sig_[n_ - 1];
		int loud = n_ - 1;
		for (int k = 0; k < n_; k++)
			if (sig_[k].pressure > sig_[loud].pressure) loud = k;
		s.pressure = sig_[loud].pressure;
		s.pressure_rate = sig_[loud].pressure_rate;
		return s;
	}

private:
	[[nodiscard]] int find(int id) const {
		for (int k = 0; k < n_; k++)
			if (id_[k] == id) return k;
		return -1;
	}
	void remove(int at) {
		for (int k = at; k + 1 < n_; k++) {
			id_[k] = id_[k + 1];
			sig_[k] = sig_[k + 1];
		}
		n_--;
	}

	int id_[Max] = {};
	Signals sig_[Max] = {};
	int n_ = 0;
};

} // namespace awva::voices

#endif // AWVA_LEGATO_H_
