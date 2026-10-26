// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
//
// awva_docs: the engines' reference pages from their EngineInfo (docs/engines/<name>.md) and the
// engine list of the index (docs/README.md). Only the text between the generated markers is
// written; everything else in a page (its "How it works") is kept as it is.
//
//   awva_docs DOCS_DIR            write the generated parts (a missing page is created)
//   awva_docs DOCS_DIR --check    exit 1 if any generated part is out of date (the test docs.*)
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "awva_sdk/engines.h"

namespace {
namespace sdk = awva::sdk;
namespace dsp = awva;

constexpr const char* BEGIN = "<!-- awva_docs: generated from the engine's EngineInfo (sdk/tools/awva_docs.cpp); run it to update, don't edit -->";
constexpr const char* END = "<!-- awva_docs: end -->";
constexpr int SR = 48000, NV = 10;

// the parameter id of the component ABI (component/include/nsc/nsc.h): FNV-1a 32 of the name
uint32_t fnv1a(const char* s) {
	uint32_t h = 2166136261u;
	for (; *s; s++) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
	return h;
}

std::string num(double v) {
	char b[32];
	std::snprintf(b, sizeof b, "%g", v);
	return b;
}

std::string bytes(size_t n) {
	if (n == 0) return "none";
	if (n >= 1024 && n % 1024 == 0) return std::to_string(n / 1024) + " KB";
	if (n >= 1024) return num(static_cast<double>(n) / 1024.0) + " KB (" + std::to_string(n) + " bytes)";
	return std::to_string(n) + " bytes";
}

struct InputDoc {
	uint32_t bit;
	const char* name;
	const char* control;
	const char* midi;
};
constexpr InputDoc INPUTS[] = {
    {dsp::in_pressure, "pressure", "`pres`, 0 released .. 1 a firm press (up to ~1.5)", "MPE: channel pressure of the note's channel; MIDI 1.0: poly or channel pressure; MIDI 2.0: Poly Pressure (full scale = 1)"},
    {dsp::in_velocity, "strike", "`vpres`, the pressure's velocity: > 0 while the key goes down, < 0 coming up", "note-on velocity (a strike that fades), note-off velocity (the release)"},
    {dsp::in_tilt_x, "tilt x", "`but_x`, sideways tilt -1 .. 1 (the pitch bend is already in `note`)", "not from standard MIDI (the Striso key's own)"},
    {dsp::in_tilt_y, "tilt y", "`but_y`, forward/back tilt -1 .. 1", "MPE: CC74 of the note's channel; MIDI 1.0: CC74 (64 = 0) or CC1; MIDI 2.0: CC74 (bipolar)"},
    {dsp::in_note_events, "note events", "`gate`, `strike`, `release`", "note on and off"},
    {dsp::in_shake, "shake", "`acc_abs`, the accelerometer's magnitude in g (1 at rest)", "the instrument's motion sensor (not from MIDI)"},
    {dsp::in_accel, "acceleration", "`acc_x`, `acc_y`, `acc_z` in g", "the instrument's motion sensor (not from MIDI)"},
    {dsp::in_rotation, "rotation", "`rot_x`, `rot_y`, `rot_z`, about -1 .. 1", "the instrument's gyroscope (not from MIDI)"},
    {dsp::in_pedal, "pedal", "`pedal`, 0 .. 1 (3/16 at rest)", "CC64"},
};

const char* kind_name(dsp::ParamKind k) {
	switch (k) {
	case dsp::ParamKind::steps: return "steps";
	case dsp::ParamKind::toggle: return "toggle";
	default: return "continuous";
	}
}

std::string heroes_line(const dsp::Heroes& h) {
	std::string s;
	for (const char* n : h.name)
		if (n && *n) s += (s.empty() ? "" : ", ") + std::string("`") + n + "`";
	return s.empty() ? "(none)" : s;
}

std::string engine_section(const sdk::EngineEntry& e) {
	const dsp::EngineInfo& in = *e.info;
	sdk::Engine eng(e);
	eng.init(SR, NV);
	std::ostringstream o;
	o << BEGIN << "\n\n";
	o << "**" << in.summary << "**\n\n";
	o << "Header `dsp/include/awva/" << e.header << "`, traits `dsp/include/awva/engines/" << in.name << ".h`, component id `com.sorbet-ai.awva." << in.name << "`.\n\n";
	o << "## Inputs\n\n| input | control | from MIDI |\n|---|---|---|\n";
	o << "| note | `note`, the pitch in semitones (69 = A4), bends included | the note number, plus the channel's pitch bend (MPE) or the note's own pitch (MIDI 2.0) |\n";
	for (const InputDoc& d : INPUTS)
		if (in.inputs & d.bit) o << "| " << d.name << " | " << d.control << " | " << d.midi << " |\n";
	o << "\nAn input not listed makes no difference to the sound; each listed one changes it in at least one preset (the contract suite checks both). What each does in this engine: \"Playing\" below. MIDI in detail: [Playing with MIDI 2.0 and MPE](../midi.md).\n\n";
	o << "## Presets\n\n";
	if (in.num_presets == 0) o << "None: the parameters are the sound.\n\n";
	else {
		o << "| # | name | hero parameters |\n|---|---|---|\n";
		for (int i = 0; i < in.num_presets; i++)
			o << "| " << i << " | " << e.preset_name(i) << " | " << heroes_line(dsp::heroes_of(in, e.preset_name(i))) << " |\n";
		o << "\nAfter `init()` the engine plays preset " << eng.preset() << " (" << e.preset_name(eng.preset()) << ").\n\n";
	}
	o << "## Hero parameters\n\n"
	  << heroes_line(in.heroes) << "\n\n";
	o << "## Parameters\n\n";
	o << "Default: the value after `init()`" << (in.num_presets > 0 ? std::string(" (preset ") + std::to_string(eng.preset()) + ")" : std::string()) << ". The id is the component ABI's (FNV-1a 32 of the name).\n\n";
	o << "| id | name | group | min | max | default | kind |\n|---|---|---|---|---|---|---|\n";
	for (int i = 0; i < eng.num_params(); i++) {
		const dsp::ParamDesc* d = eng.param_desc(i);
		char id[16];
		std::snprintf(id, sizeof id, "%08x", fnv1a(d->name));
		o << "| `" << id << "` | `" << d->name << "` | " << d->group << " | " << num(d->min) << " | " << num(d->max) << " | " << num(*eng.param(i)) << " | " << kind_name(d->kind) << " |\n";
	}
	if (eng.num_params() == 0) o << "| | (none) | | | | | |\n";
	o << "\n## Budget and memory\n\n| | |\n|---|---|\n";
	o << "| voices | up to " << in.budget.max_voices << " |\n";
	o << "| channels | " << in.channels << (in.channels == 1 ? " (mono)" : " (stereo)") << " |\n";
	o << "| CPU | " << in.budget.instr_per_block << " QEMU instructions per 32-sample block, worst preset, 10 voices, the default performance |\n";
	o << "| tail | silent and idle within " << num(in.budget.tail_seconds) << " s of the last release |\n";
	o << "| engine object | " << bytes(in.state_bytes) << " (on a 64-bit host) |\n";
	o << "| FastMemory | " << bytes(in.fast_bytes) << " |\n";
	o << "| LargeMemory | " << bytes(in.large_bytes) << " |\n\n";
	o << END;
	return o.str();
}

std::string index_section() {
	std::ostringstream o;
	o << BEGIN << "\n\n| engine | what it is | presets | parameters |\n|---|---|---|---|\n";
	for (const sdk::EngineEntry* e : sdk::engines())
		o << "| [" << e->info->name << "](engines/" << e->info->name << ".md) | " << e->info->summary << " | " << e->info->num_presets << " | " << e->info->num_params << " |\n";
	o << "\n"
	  << END;
	return o.str();
}

bool read(const std::string& path, std::string& out) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	std::ostringstream s;
	s << f.rdbuf();
	out = s.str();
	return true;
}

// the page with its generated part replaced (or a new page around it)
std::string updated(const std::string& old, const std::string& gen, const std::string& fresh_head, const std::string& fresh_tail) {
	const size_t a = old.find(BEGIN), b = old.find(END);
	if (a == std::string::npos || b == std::string::npos || b < a) return fresh_head + gen + fresh_tail;
	return old.substr(0, a) + gen + old.substr(b + std::strlen(END));
}

int process(const std::string& path, const std::string& gen, const std::string& head, const std::string& tail, bool check, int& stale) {
	std::string old;
	const bool exists = read(path, old);
	const std::string now = updated(exists ? old : std::string(), gen, head, tail);
	if (exists && now == old) return 0;
	if (check) {
		std::fprintf(stderr, "out of date: %s (run awva_docs on the docs directory)\n", path.c_str());
		stale++;
		return 0;
	}
	std::ofstream f(path, std::ios::binary);
	if (!f) {
		std::fprintf(stderr, "cannot write %s\n", path.c_str());
		return 2;
	}
	f << now;
	std::printf("wrote %s\n", path.c_str());
	return 0;
}
} // namespace

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: awva_docs DOCS_DIR [--check]\n");
		return 2;
	}
	const std::string dir = argv[1];
	const bool check = argc > 2 && std::strcmp(argv[2], "--check") == 0;
	int stale = 0;
	for (const sdk::EngineEntry* e : sdk::engines()) {
		const std::string name = e->info->name;
		std::string head = "<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->\n<!-- SPDX-License-Identifier: Apache-2.0 -->\n# ";
		head += name;
		head += "\n\n";
		const std::string tail = "\n\n## How it works\n\n(to be written)\n";
		std::string path = dir;
		path += "/engines/";
		path += name;
		path += ".md";
		if (process(path, engine_section(*e), head, tail, check, stale)) return 2;
	}
	if (process(dir + "/README.md", index_section(), "# awva documentation\n\n", "\n", check, stale)) return 2;
	return stale ? 1 : 0;
}
