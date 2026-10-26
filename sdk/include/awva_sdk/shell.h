// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The host shell: what a plugin (VST3, CLAP, AU) or a standalone app wraps. One engine and the
// master effects, played by MIDI/MPE (midi_voices.h) or directly by voice
// signals, rendering any number of frames per call (the engines run 32-sample blocks, rendered
// when the previous one is used up). MIDI is taken every 2 ms: a message takes
// effect at the next 2 ms step. Not real-time safe in its constructor and set_engine();
// process() and midi() don't allocate.
//
//   Shell s(*awva::sdk::find_engine("va"), 48000);
//   s.set_preset(5);
//   s.midi(0x90, 60, 100);           // MIDI in (applied at the next block)
//   s.midi(0x80, 60, 0, 17);         // ... at frame 17 of the next process() call
//   s.process(left, right, frames);  // stereo out, about +-4 full scale (x0.25 for unity)
#ifndef AWVA_SDK_SHELL_H_
#define AWVA_SDK_SHELL_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "awva_sdk/engines.h"
#include "awva_sdk/timed_midi.h"

namespace awva::sdk {

class Shell {
public:
	// voices: the engine's voices (at most its budget's max_voices); fx: the master effects on
	Shell(const EngineEntry& engine, int sample_rate, int voices = 10, bool fx = true);

	void set_engine(const EngineEntry& engine); // a new engine, from silence (MIDI state kept)
	[[nodiscard]] Engine& engine() { return engine_; }
	[[nodiscard]] Effect& effects() { return fx_; }
	void set_fx(bool on) { fx_on_ = on; }
	void set_preset(int i) { engine_.set_preset(i); }

	// MIDI in, at a frame: offset is the message's frame in the next process() call (a plugin
	// host's sample offset; 0: now). It is taken at the first 2 ms step at or after its frame
	// (timed_midi.h: the component's and the renderer's rule, in integer frames), so where it
	// lands doesn't depend on the call sizes. A Note On may carry the host's note id (-1: none),
	// handed back by ended_notes() when the note has ended. false: the queue is full.
	bool midi(const uint8_t* msg, int len, int offset = 0, int32_t note_id = -1);
	bool midi(uint8_t status, uint8_t d1, uint8_t d2, int offset = 0, int32_t note_id = -1) {
		const uint8_t m[3] = {status, d1, d2};
		return midi(m, 3, offset, note_id);
	}
	// MIDI 2.0 in: a UMP packet (type 4, or MIDI 1.0 in UMP, type 2), the same way
	bool ump(const uint32_t* words, int offset = 0, int32_t note_id = -1);
	// the host's ids of the notes that ended (released, then the engine's tail passed or it went
	// idle, or another note took their key): how many were written, at most max. Every Note On's
	// id comes back exactly once if this is called after each process() until it returns less
	// than max, with at most TimedMidi::MAX_EVENTS messages between two process() calls
	// (timed_midi.h)
	int ended_notes(int32_t* ids, int max);

	// or the voices' controls directly (a controller's own sensors, OSC, a test): from now on these, not MIDI
	void set_controls(const NoteControls* vc, const InstrumentControls& g);

	// frames of stereo output
	void process(float* left, float* right, int frames);

	// the controls the engine got in the last block
	[[nodiscard]] const NoteControls* controls() const { return vc_.data(); }
	[[nodiscard]] const InstrumentControls& globals() const { return g_; }

	[[nodiscard]] int sample_rate() const { return sr_; }
	[[nodiscard]] int voices() const { return nv_; }

private:
	void render_block();

	Engine engine_;
	Effect fx_;
	std::unique_ptr<TimedMidi> midi_; // (large: on the heap)
	int sr_, nv_;
	bool fx_on_;
	bool direct_ = false; // set_controls() was used
	std::vector<NoteControls> vc_;
	InstrumentControls g_;
	uint64_t frames_ = 0; // frames rendered
	uint64_t out_ = 0;    // frames handed out

	float mono_[dsp::BLOCK], l_[dsp::BLOCK], r_[dsp::BLOCK];
	int pos_ = dsp::BLOCK; // next frame of l_, r_ to hand out
};

} // namespace awva::sdk

#endif // AWVA_SDK_SHELL_H_
