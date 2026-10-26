// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// What plays an engine: a Player gives the controls of every block. Three kinds:
//   scripted   a performance compiled in from tools/harness (performance.h: "default",
//              perf_stale.h: "stale", perf_<name>.h: "<name>"), deterministic
//   keys       a recorded stream of per-note sensor values (CSV), replayed per voice
//              (tools/harness/harness_util.h: load_recording)
//   midi       a Standard MIDI File through the MIDI/MPE mapping (midi_voices.h)
//   ump        a text file of timed Universal MIDI Packets (MIDI 2.0, and MIDI 1.0 in UMP) the same way
// Each says what it is and where it came from (describe()), for the renderer's log.
#ifndef AWVA_SDK_PERFORMANCE_H_
#define AWVA_SDK_PERFORMANCE_H_

#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "awva/engine.h"

namespace awva::sdk {

// a scripted performance, compiled in (sdk/CMakeLists.txt: one per header)
struct ScriptedPerformance {
	const char* name;   // "default", "stale", "bowed", ...
	const char* header; // the file it was compiled from
	int sample_rate;    // the rate it is written for (its SAMPLERATE)
	int max_voices;     // its NVOICES_MAX
	// the controls (pitch, pressure, pressure_rate, timbre, gate) of block b for nv voices, and the globals
	void (*block)(int b, int nv, NoteControls* vc, dsp::InstrumentControls& g);
};

// the scripted performances linked in (awva_performances), by name
[[nodiscard]] std::span<const ScriptedPerformance* const> performances();
[[nodiscard]] const ScriptedPerformance* find_performance(std::string_view name);

class Player {
public:
	virtual ~Player() = default;
	// the controls of block b (blocks in order from 0): writes what changes, the rest stays
	virtual void block(int b, NoteControls* vc, dsp::InstrumentControls& g) = 0;
	// its natural length in seconds (a render's default)
	[[nodiscard]] virtual double seconds() const = 0;
	// what it is, for a log: kind, name, file (absolute), its size and checksum, its length
	[[nodiscard]] virtual std::string describe() const = 0;
};

// nullptr and a reason in *error if it can't play (unknown name, unreadable file, no events)
[[nodiscard]] std::unique_ptr<Player> scripted_player(std::string_view name, int voices, int sample_rate, std::string* error);
[[nodiscard]] std::unique_ptr<Player> key_player(const char* csv_path, int voices, int sample_rate, std::string* error);
[[nodiscard]] std::unique_ptr<Player> midi_player(const char* mid_path, int voices, int sample_rate, std::string* error, bool legato = false);
// a UMP file: one packet a line, "<frame> <word> [<word> ...]" (decimal frame, hex words), '#'
// starts a comment; frames in order. legato: the mapping in mono legato (midi_voices.h)
[[nodiscard]] std::unique_ptr<Player> ump_player(const char* path, int voices, int sample_rate, bool legato, std::string* error);

// the file's absolute path, size and FNV-1a 64 checksum, "<path> (<n> bytes, fnv <hex>)"
[[nodiscard]] std::string file_identity(const char* path);

} // namespace awva::sdk

#endif // AWVA_SDK_PERFORMANCE_H_
