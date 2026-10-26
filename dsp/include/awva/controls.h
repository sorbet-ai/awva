// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
//
// The inputs of a voice engine: the block length, one voice's five controls and the instrument's
// eight global ones. Plain data; engine.h (namespace awva) builds the contract on them.
// (Namespace strisy: the engines' shared helpers.)
#ifndef AWVA_CONTROLS_H_
#define AWVA_CONTROLS_H_

namespace strisy {

// samples per process() call
static constexpr int BLOCK = 32;

// one voice, as its host writes it between blocks:
//   note   pitch in semitones (MIDI numbers, 69 = A4), fractional
//   pres   pressure: 0 released, 1 the full press (more when pressed harder)
//   vpres  velocity of the pressure: > 0 rising (the strike), < 0 falling (the release)
//   but_x  tilt sideways, -1 .. 1 (a per-note controller)
//   but_y  tilt forward/back, -1 .. 1 (a per-note controller)
struct VoiceControls {
	float note, pres, vpres, but_x, but_y;
};

// the whole instrument: the accelerometer (in g; acc_abs its magnitude, 1 at rest), the gyroscope
// (rot_*, about -1 .. 1) and the pedal (0 .. 1)
struct GlobalControls {
	float acc_abs, acc_x, acc_y, acc_z, rot_x, rot_y, rot_z, pedal;
};

} // namespace strisy

#endif // AWVA_CONTROLS_H_
