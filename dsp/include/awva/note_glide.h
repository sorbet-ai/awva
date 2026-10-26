// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
//
// A bore can't change at once. A host may step a held note's pitch in one block (a bend that
// jumps, a controls file); a bore engine that followed would change its whole bore at once, and a
// beating reed or lip slaps shut once (an isolated, click-like pulse). NoteGlide gives such an
// engine the note it plays: a step of the held note of STEP semitones or more in one block is
// followed linearly over a few blocks, the way tone holes and valves move; smaller changes (bends,
// vibrato) and the first block of every note pass straight through. (Mono's slurs, mono.h, move
// the pitch slower than STEP a block on these engines' families: this guard doesn't act on them.)
#ifndef AWVA_NOTE_GLIDE_H_
#define AWVA_NOTE_GLIDE_H_

namespace awva {

struct NoteGlide {
	static constexpr float STEP = 0.5f; // semitones in one block: a step, not a bend

	float note; // the note played
	float goal; // the host's note
	int left;   // blocks to the goal
	bool held;  // held in the last block

	// the note to play this block: target the host's note, held_now the note held this block
	// (pressure > 0), blocks the transition's length (at least 1)
	float next(float target, bool held_now, int blocks) {
		const bool slur = held && held_now;
		held = held_now;
		if (!slur) {
			note = goal = target;
			left = 0;
			return note;
		}
		const float d = target - goal;
		if (d >= STEP || d <= -STEP) left = blocks < 1 ? 1 : blocks;
		goal = target;
		if (left > 0) {
			note += (goal - note) / (float)left;
			if (--left == 0) note = goal;
		} else {
			note = goal;
		}
		return note;
	}
	// the blocks of a transition of seconds at a sample rate (block of strisy::BLOCK samples)
	[[nodiscard]] static int blocks_for(float seconds, float sample_rate, int block) {
		const int n = (int)(seconds * sample_rate / (float)block + 0.5f);
		return n < 1 ? 1 : n;
	}
};

} // namespace awva

#endif // AWVA_NOTE_GLIDE_H_
