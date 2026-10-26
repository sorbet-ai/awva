// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
//
// The inputs of a voice engine: the engines' block, one note's controls and the instrument's.
// Plain data; engine.h builds the contract on them.
#ifndef AWVA_CONTROLS_H_
#define AWVA_CONTROLS_H_

#include <stdint.h>

namespace strisy {

// samples per process() call
static constexpr int BLOCK = 32;

} // namespace strisy

namespace awva {

// One note's expression at block rate, in MIDI 2.0's terms: what a voice of an engine reads. The
// MIDI mapping (sdk: MidiVoices) writes it from MIDI; a test may write it directly.
struct NoteControls {
	float pitch;         // semitones, MIDI numbering (69 = A4), fractional: the note with its bends
	float pressure;      // 0 (released) .. 1: MIDI 2.0's full-scale Poly Pressure; engines take up to 1.5
	float pressure_rate; // how fast the pressure moves, about -1 .. 1: > 0 pressing (the strike), < 0 lifting
	float timbre;        // -1 .. 1: MIDI 2.0's Registered Per-Note Controller 74 (brightness), MPE's CC74
	float velocity;      // the Note On's velocity, 0 .. 1 (kept until the next)
	float release;       // the Note Off's velocity, 0 .. 1 (kept until the next)
	uint8_t gate;        // 1 from the Note On to the Note Off, else 0 (pressure > 0 while it is 1)
};

// a note at rest: A4, no pressure
inline constexpr NoteControls note_at_rest = {69.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0};

// The whole instrument at block rate: MIDI 2.0's sustain pedal and, from a host with a motion
// sensor (or from MIDI: Assignable Controllers), its motion as physical quantities.
struct InstrumentControls {
	float sustain; // CC64, 0 .. 1
	struct Motion {
		float accel_g; // the magnitude of the acceleration in g (1 at rest)
		float spin[3]; // the angular rate about each axis, -1 .. 1 = the sensor's full scale
	} motion;
};

// the instrument at rest: no sustain (a host without a pedal has none), 1 g, no spin
inline constexpr InstrumentControls instrument_at_rest = {0.0f, {1.0f, {0.0f, 0.0f, 0.0f}}};

} // namespace awva

#endif // AWVA_CONTROLS_H_
