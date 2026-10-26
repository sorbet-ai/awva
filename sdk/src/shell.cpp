// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The host shell (shell.h).
#include "awva_sdk/shell.h"

#include <algorithm>

namespace awva::sdk {

Shell::Shell(const EngineEntry& engine, int sample_rate, int voices, bool fx) :
    engine_(engine), fx_(fx_effect()), midi_(std::make_unique<TimedMidi>(std::min(voices, engine.info->budget.max_voices), sample_rate)), sr_(sample_rate),
    nv_(std::min(voices, engine.info->budget.max_voices)), fx_on_(fx), vc_(static_cast<size_t>(nv_), note_at_rest),
    g_(dsp::instrument_at_rest) {
	engine_.init(sr_, nv_);
	fx_.init(sr_);
	std::fill(std::begin(l_), std::end(l_), 0.0f);
	std::fill(std::begin(r_), std::end(r_), 0.0f);
}

void Shell::set_engine(const EngineEntry& engine) {
	engine_ = Engine(engine);
	nv_ = std::min(nv_, engine.info->budget.max_voices);
	engine_.init(sr_, nv_);
}

void Shell::set_controls(const NoteControls* vc, const InstrumentControls& g) {
	direct_ = true;
	for (int v = 0; v < nv_; v++) vc_[static_cast<size_t>(v)] = vc[v];
	g_ = g;
}

bool Shell::midi(const uint8_t* msg, int len, int offset, int32_t note_id) {
	return midi_->midi1(out_ + static_cast<uint64_t>(offset > 0 ? offset : 0), msg, len, note_id);
}

bool Shell::ump(const uint32_t* words, int offset, int32_t note_id) {
	return midi_->ump(out_ + static_cast<uint64_t>(offset > 0 ? offset : 0), words, note_id);
}

int Shell::ended_notes(int32_t* ids, int max) {
	const auto tail = static_cast<uint64_t>(static_cast<double>(engine_.info().budget.tail_seconds) * sr_);
	return midi_->ended(ids, max, tail, engine_.active_voices() == 0);
}

void Shell::render_block() {
	if (!direct_) {
		midi_->advance(frames_); // the 2 ms steps due by this block's start (timed_midi.h)
		for (int v = 0; v < nv_; v++) vc_[static_cast<size_t>(v)] = midi_->midi().voices()[v];
		g_ = midi_->midi().globals();
	}
	frames_ += dsp::BLOCK;
	float* const eo[1] = {mono_};
	engine_.process(vc_.data(), g_, eo);
	if (fx_on_) {
		const float* const in[1] = {mono_};
		float* const out[2] = {l_, r_};
		fx_.process(in, out);
	} else {
		std::copy(std::begin(mono_), std::end(mono_), l_);
		std::copy(std::begin(mono_), std::end(mono_), r_);
	}
	pos_ = 0;
}

void Shell::process(float* left, float* right, int frames) {
	for (int n = 0; n < frames; n++) {
		if (pos_ == dsp::BLOCK) render_block();
		left[n] = l_[pos_];
		right[n] = r_[pos_];
		pos_++;
	}
	out_ += static_cast<uint64_t>(frames);
}

} // namespace awva::sdk
