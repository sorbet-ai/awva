// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
//
// mpe_file: a standard MIDI file, MPE or plain, through an awva engine with the SDK's MIDI in
// (awva_sdk/timed_midi.h over note_mapper.h: the decoder and the voice allocator,
// a step every engine block).
//
//   mpe_file IN.mid OUT.wav [ENGINE [PRESET]]   render (32-bit float WAV, mono); prints the hash
//   mpe_file --demo OUT.mid                     write a short MPE demo file to play
//
// The render is what `awva_render --engine ENGINE --midi IN.mid` renders, sample for sample: the
// same integer-frame steps, voices and length (the hash it prints is the renderer's).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "awva_sdk/engines.h"
#include "awva_sdk/smf.h"
#include "awva_sdk/timed_midi.h"

namespace {
namespace sdk = awva::sdk;
constexpr int SR = 48000, NV = 10;
constexpr double PI = 3.14159265358979323846;

// An MPE demo: the lower zone (an MCM: RPN 6 = 15 members on channel 1), a bend range of 48
// semitones on the members, then a three-note chord, one note per member channel, each with its
// own bend, pressure and CC74 (the timbre), released one by one.
int write_demo(const char* path) {
	std::vector<sdk::MidiEvent> ev;
	auto add = [&ev](double t, uint8_t a, uint8_t b, uint8_t c) { ev.push_back({t, {a, b, c}, 3}); };
	auto add2 = [&ev](double t, uint8_t a, uint8_t b) { ev.push_back({t, {a, b, 0}, 2}); }; // (channel pressure: two bytes)
	add(0.0, 0xb0, 101, 0);                                                                 // MCM: RPN 6 on the master channel
	add(0.0, 0xb0, 100, 6);
	add(0.0, 0xb0, 6, 15);
	for (int ch = 1; ch <= 3; ch++) { // bend range 48 on the members (RPN 0)
		add(0.0, static_cast<uint8_t>(0xb0 | ch), 101, 0);
		add(0.0, static_cast<uint8_t>(0xb0 | ch), 100, 0);
		add(0.0, static_cast<uint8_t>(0xb0 | ch), 6, 48);
	}
	const int notes[3] = {57, 61, 64};
	for (int k = 0; k < 3; k++) {
		const uint8_t ch = static_cast<uint8_t>(1 + k);
		const double on = 0.1 + 0.4 * k, off = 3.0 + 0.3 * k;
		add(on, static_cast<uint8_t>(0x90 | ch), static_cast<uint8_t>(notes[k]), 90);
		for (double t = on; t < off; t += 0.02) {
			const double u = t - on;
			const double swell = std::sin(PI * std::fmin(u / 2.0, 1.0));
			add2(t, static_cast<uint8_t>(0xd0 | ch), static_cast<uint8_t>(20 + 100 * swell * swell));                    // channel pressure
			const int bend = 8192 + static_cast<int>(85.0 * std::sin(2 * PI * 5.0 * u) * std::fmin(u, 1.0));             // a vibrato
			add(t, static_cast<uint8_t>(0xe0 | ch), static_cast<uint8_t>(bend & 0x7f), static_cast<uint8_t>(bend >> 7)); // (48 semitones: +-1/2 semitone)
			add(t, static_cast<uint8_t>(0xb0 | ch), 74, static_cast<uint8_t>(64 + 40 * std::sin(PI * u / (off - on))));
		}
		add(off, static_cast<uint8_t>(0x80 | ch), static_cast<uint8_t>(notes[k]), 64);
	}
	std::stable_sort(ev.begin(), ev.end(), [](const sdk::MidiEvent& a, const sdk::MidiEvent& b) { return a.t < b.t; });
	const std::vector<uint8_t> bytes = sdk::make_midi(ev);
	FILE* f = std::fopen(path, "wb");
	if (!f) return 2;
	std::fwrite(bytes.data(), 1, bytes.size(), f);
	std::fclose(f);
	std::printf("wrote %s: %zu messages\n", path, ev.size());
	return 0;
}

bool write_wav(const char* path, const std::vector<float>& x) {
	FILE* f = std::fopen(path, "wb");
	if (!f) return false;
	const auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	const auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	const uint32_t n = static_cast<uint32_t>(x.size());
	std::fwrite("RIFF", 1, 4, f);
	u32(36 + 4 * n);
	std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16);
	u16(3); // IEEE float
	u16(1);
	u32(SR);
	u32(SR * 4);
	u16(4);
	u16(32);
	std::fwrite("data", 1, 4, f);
	u32(4 * n);
	std::fwrite(x.data(), 4, n, f);
	std::fclose(f);
	return true;
}
} // namespace

int main(int argc, char** argv) {
	if (argc == 3 && std::strcmp(argv[1], "--demo") == 0) return write_demo(argv[2]);
	if (argc < 3) {
		std::fprintf(stderr, "usage: mpe_file IN.mid OUT.wav [ENGINE [PRESET]] | mpe_file --demo OUT.mid\n");
		return 2;
	}
	const sdk::EngineEntry* entry = sdk::find_engine(argc > 3 ? argv[3] : "va");
	if (!entry) {
		std::fprintf(stderr, "no engine %s\n", argv[3]);
		return 2;
	}
	const sdk::MidiFile file = sdk::read_midi_file(argv[1]);
	if (!file.error.empty()) {
		std::fprintf(stderr, "%s: %s\n", argv[1], file.error.c_str());
		return 2;
	}

	// the engine: an instance with its memory, at our rate and voice count
	sdk::Engine engine(*entry);
	engine.init(SR, NV);
	if (argc > 4) engine.set_preset(std::atoi(argv[4]));

	// MIDI in, in integer frames (awva_sdk/timed_midi.h): each message queued at its frame, and
	// at every engine block (the default control rate: HostConfig::control_hz 0) a step runs the
	// ones due
	auto midi = std::make_unique<sdk::TimedMidi>(sdk::HostConfig{SR, 0, NV}); // (some kilobytes: on the heap)
	awva::InstrumentControls g = awva::instrument_at_rest;
	const auto frame_of = [](double t) { return t <= 0 ? uint64_t{0} : static_cast<uint64_t>(std::llround(t * SR)); };
	size_t next = 0;

	const int blocks = static_cast<int>((file.seconds + 2.0) * SR / awva::BLOCK); // the file and 2 s of tail
	std::vector<float> out(static_cast<size_t>(blocks) * awva::BLOCK);
	for (int b = 0; b < blocks; b++) {
		// the file's messages up to this block's start (each at its time, to the nearest frame), then
		// the steps due by it
		const uint64_t start = static_cast<uint64_t>(b) * awva::BLOCK;
		for (; next < file.events.size() && frame_of(file.events[next].t) <= start; next++)
			(void)midi->midi1(frame_of(file.events[next].t), file.events[next].bytes, file.events[next].len);
		midi->advance(start);
		// the voices' controls to the engine
		g = midi->midi().globals();
		float* o[1] = {&out[static_cast<size_t>(b) * awva::BLOCK]};
		engine.process(midi->midi().voices(), g, o);
	}

	uint64_t h = 1469598103934665603ull; // FNV-1a 64 over the float bits, as awva_render prints it
	double energy = 0;
	bool finite = true;
	for (float x : out) {
		uint32_t u;
		std::memcpy(&u, &x, 4);
		for (int k = 0; k < 4; k++) h = (h ^ ((u >> (8 * k)) & 0xffu)) * 1099511628211ull;
		finite = finite && std::isfinite(x);
		energy += static_cast<double>(x) * x;
	}
	if (!write_wav(argv[2], out)) return 2;
	std::printf("%s through %s: %zu messages, %.1f s, rms %.4f\nhash %016llx\n", argv[1], entry->info->name, file.events.size(), static_cast<double>(out.size()) / SR,
	    std::sqrt(energy / static_cast<double>(out.size())), static_cast<unsigned long long>(h));
	return finite && energy > 1e-6 ? 0 : 1;
}
