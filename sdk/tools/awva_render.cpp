// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// awva_render: any engine and preset, played by a scripted performance, a recorded key stream, a
// MIDI file or captured engine controls, to a WAV (and raw float32). It says on stderr exactly which
// engine, preset and input it used (the input file's absolute path, size and checksum), and refuses
// anything it can't find rather than falling back to something else. It is also the renderer of
// the fitting tools (tools/fit): parameter overrides, a transposition, a note list, the controls
// the engine got and a JSON manifest of the render.
//
// Usage: awva_render --engine NAME [--preset N|NAME] [input] [options]
//   input (one of; default --perf default):
//     --perf NAME       a scripted performance compiled in (--list shows them)
//     --rec FILE.csv    a recorded key stream (CSV t,src,id,pres,velo,x,y), through the key path
//     --midi FILE.mid   a Standard MIDI File, MIDI 1.0 or MPE, through the MIDI/MPE mapping
//                       (awva/midi_in.h: up-converted to MIDI 2.0)
//     --ump FILE.ump    timed UMP packets, a text file (awva_sdk/performance.h: ump_player), the same way
//     --ctl FILE.ctl    engine controls, float32 per block: 10 voices x (pitch, pressure, pressure_rate, timbre),
//                       then the 8 globals (what --ctl-out writes, or an embedded host's capture)
//   --seconds S       length (default: the input's: 17.5 s, a recording's length + 2 s, a MIDI
//                     file's length + 2 s, a controls file's blocks), plus --tail
//   --tail S          seconds after the input's own length (default 0)
//   --settle S        seconds of silence rendered before the input and not written (default 0):
//                     the parameter glide of --set has arrived when the input starts
//   --legato          (--midi, --ump) the MIDI mapping in mono legato
//   --nv N            voices (default 10)
//   --rate HZ         sample rate (default: the performance's, 48000); the block is always 32
//   --set NAME=V      a live parameter, written with its rule (clamped) after the preset; repeatable
//   --transpose T     every note the engine gets moved by T semitones (an octave shift)
//   --notes N1,N2,..  every new press takes the next note of the list, in turn
//   --params          the live parameters after --preset and --set: "name group min max value", and exit
//   --fx              through the master effects (default parameters): stereo
//   --ftz             each block with subnormals flushed, as the engine component runs it
//                     (component/include/nsc/no_denormals.h): a plugin's output, bit for bit
//   --out FILE.f32    the render as float32 (mono, or L R interleaved)
//   --wav FILE.wav    16 bit WAV, x0.25 and clipped (an embedded host at its usual level); its INFO chunk names the
//                     engine, preset and input
//   --ctl-out FILE    the controls the engine got, float32 per block (the --ctl layout)
//   --manifest FILE   a JSON record of the render: engine, preset, parameters, input, settings,
//                     outputs, peak and hash
//   --check           fail (exit 1) on non-finite output, a peak over --max-peak (default 8), or
//                     with --silent-after T any sample above 1e-4 after T s or voices still active
//   --expect HASH     fail unless the render's hash (FNV-1a 64 of the float bits) is HASH
//   --golden-name N   with AWVA_GOLDEN_OUT=<dir>: write the hash to <dir>/N.hash instead
//   --compare FILE.f32  how far the render is from another (an --out of an earlier build): the
//                     samples that differ and the largest difference, absolute and in dB re 1.0
//   --list            the engines, their presets and the performances, and exit
// stdout: "hash <hex>". Exit 2: bad arguments or an input that can't be read; exit 1: --check or
// --expect failed.
// Renders are deterministic: the engines seed their noise in init(), the same input and settings
// give the same hash on every run.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "awva_sdk/engines.h"
#include "awva_sdk/performance.h"
#include "nsc/no_denormals.h"

namespace sdk = awva::sdk;
namespace dsp = awva;

namespace {

constexpr int CTL_VOICES = 10, CTL_WIDTH = CTL_VOICES * 4 + 8;

[[noreturn]] void fail(const std::string& msg) {
	std::fprintf(stderr, "awva_render: %s\n", msg.c_str());
	std::exit(2);
}

void list() {
	for (const sdk::EngineEntry* e : sdk::engines()) {
		std::printf("engine %s (%s): %s\n", e->info->name, e->header, e->info->summary);
		for (int i = 0; i < e->info->num_presets; i++) std::printf("  preset %d %s\n", i, e->preset_name(i));
	}
	for (const sdk::ScriptedPerformance* p : sdk::performances()) std::printf("performance %s (%s)\n", p->name, p->header);
}

[[nodiscard]] uint64_t hash_samples(const std::vector<float>& x) {
	uint64_t h = 1469598103934665603ull;
	for (float v : x) {
		uint32_t u;
		std::memcpy(&u, &v, 4);
		for (int k = 0; k < 4; k++) h = (h ^ ((u >> (8 * k)) & 0xffu)) * 1099511628211ull;
	}
	return h;
}

[[nodiscard]] bool write_f32(const char* path, const std::vector<float>& v) {
	FILE* f = std::fopen(path, "wb");
	if (!f) return false;
	const bool ok = std::fwrite(v.data(), sizeof(float), v.size(), f) == v.size();
	return std::fclose(f) == 0 && ok;
}

// 16 bit WAV, x0.25 and hard clipped (an embedded host at its usual volume), with a LIST/INFO chunk
[[nodiscard]] bool write_wav(const char* path, const std::vector<float>& x, int channels, int sample_rate, const std::string& comment) {
	FILE* f = std::fopen(path, "wb");
	if (!f) return false;
	const auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	const auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::string info = "INFO";
	const auto sub = [&info](const char* id, const std::string& s) {
		std::string z = s + '\0';
		if (z.size() & 1) z += '\0';
		const uint32_t n = static_cast<uint32_t>(s.size() + 1);
		info += id;
		info.append(reinterpret_cast<const char*>(&n), 4);
		info += z;
	};
	sub("ISFT", "awva_render");
	sub("ICMT", comment);
	const uint32_t n = static_cast<uint32_t>(x.size()), data = n * 2, list = static_cast<uint32_t>(info.size());
	std::fwrite("RIFF", 1, 4, f);
	u32(4 + 8 + 16 + 8 + list + 8 + data);
	std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16);
	u16(1);
	u16(static_cast<uint16_t>(channels));
	u32(static_cast<uint32_t>(sample_rate));
	u32(static_cast<uint32_t>(sample_rate * 2 * channels));
	u16(static_cast<uint16_t>(2 * channels));
	u16(16);
	std::fwrite("LIST", 1, 4, f);
	u32(list);
	std::fwrite(info.data(), 1, info.size(), f);
	std::fwrite("data", 1, 4, f);
	u32(data);
	std::vector<int16_t> s(n);
	for (uint32_t k = 0; k < n; k++) {
		float v = x[k] * 0.25f;
		v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
		s[k] = static_cast<int16_t>(std::lrintf(v * 32767.0f));
	}
	std::fwrite(s.data(), 2, n, f);
	return std::fclose(f) == 0;
}

// captured engine controls (the --ctl layout): the key signals and the globals of every block
class ControlsPlayer final : public sdk::Player {
public:
	ControlsPlayer(std::string path, std::vector<float> c, int voices, int sample_rate) :
	    path_(std::move(path)), c_(std::move(c)), nv_(voices), sr_(sample_rate) {}
	void block(int b, dsp::NoteControls* vc, dsp::GlobalControls& g) override {
		if (static_cast<size_t>(b) >= blocks()) return; // past the end: everything stays
		const float* c = &c_[static_cast<size_t>(b) * CTL_WIDTH];
		for (int v = 0; v < nv_ && v < CTL_VOICES; v++) {
			vc[v].pitch = c[4 * v];
			vc[v].pressure = c[4 * v + 1];
			vc[v].pressure_rate = c[4 * v + 2];
			vc[v].timbre = c[4 * v + 3];
			vc[v].gate = vc[v].pressure > 0.0f;
		}
		const float* gg = c + CTL_VOICES * 4;
		g = {gg[0], gg[1], gg[2], gg[3], gg[4], gg[5], gg[6], gg[7]};
	}
	[[nodiscard]] double seconds() const override { return static_cast<double>(blocks()) * dsp::BLOCK / sr_; }
	[[nodiscard]] std::string describe() const override {
		return "engine controls " + sdk::file_identity(path_.c_str()) + ": " + std::to_string(blocks()) + " blocks";
	}
	[[nodiscard]] size_t blocks() const { return c_.size() / CTL_WIDTH; }

private:
	std::string path_;
	std::vector<float> c_;
	int nv_, sr_;
};

[[nodiscard]] std::unique_ptr<ControlsPlayer> controls_player(const char* path, int voices, int sample_rate, std::string* error) {
	FILE* f = std::fopen(path, "rb");
	if (!f) {
		*error = std::string("cannot read the controls file ") + path;
		return nullptr;
	}
	std::fseek(f, 0, SEEK_END);
	const long n = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	std::vector<float> c(n > 0 ? static_cast<size_t>(n) / 4 : 0);
	const bool ok = std::fread(c.data(), 4, c.size(), f) == c.size();
	std::fclose(f);
	if (!ok || c.size() < static_cast<size_t>(CTL_WIDTH) || n % (4 * CTL_WIDTH) != 0) {
		*error = std::string("the controls file ") + path + " is not whole blocks of " + std::to_string(CTL_WIDTH) + " floats";
		return nullptr;
	}
	return std::make_unique<ControlsPlayer>(path, std::move(c), voices, sample_rate);
}

[[nodiscard]] std::vector<float> parse_notes(const std::string& s) {
	std::vector<float> v;
	size_t a = 0;
	while (a <= s.size()) {
		size_t b = s.find(',', a);
		if (b == std::string::npos) b = s.size();
		if (b > a) {
			char* end = nullptr;
			const std::string t = s.substr(a, b - a);
			v.push_back(static_cast<float>(std::strtod(t.c_str(), &end))); // (as atof: the double, then the float)
			if (!end || *end) fail("--notes: \"" + t + "\" is not a number");
		}
		a = b + 1;
	}
	if (v.empty()) fail("--notes: no notes");
	return v;
}

[[nodiscard]] std::string json_string(const std::string& s) {
	std::string o = "\"";
	for (const char ch : s) {
		const unsigned char u = static_cast<unsigned char>(ch);
		if (ch == '"' || ch == '\\') {
			o += '\\';
			o += ch;
		} else if (u < 0x20) {
			char b[8];
			std::snprintf(b, sizeof b, "\\u%04x", u);
			o += b;
		} else o += ch;
	}
	return o + "\"";
}

[[nodiscard]] std::string json_number(double v) {
	if (!std::isfinite(v)) return "null";
	char b[40];
	std::snprintf(b, sizeof b, "%.9g", v);
	return b;
}

struct Set {
	std::string name;
	float value;
};

} // namespace

int main(int argc, char** argv) {
	const char *engine_name = nullptr, *preset_arg = nullptr, *perf = nullptr, *rec = nullptr, *midi = nullptr, *ctl = nullptr, *ump = nullptr, *compare = nullptr;
	const char *out_path = nullptr, *wav = nullptr, *expect = nullptr, *golden_name = nullptr, *ctl_out = nullptr, *manifest = nullptr;
	double seconds = -1, max_peak = 8.0, silent_after = -1, tail = 0, settle = 0;
	int nv = 10, rate = 0;
	float transpose = 0.0f;
	bool use_fx = false, check = false, list_params = false, ftz = false, legato = false;
	std::vector<Set> sets;
	std::vector<float> notes;
	for (int a = 1; a < argc; a++) {
		const std::string s = argv[a];
		const auto next = [&]() -> const char* {
			if (a + 1 >= argc) fail(s + " needs a value");
			return argv[++a];
		};
		if (s == "--engine") engine_name = next();
		else if (s == "--preset") preset_arg = next();
		else if (s == "--perf") perf = next();
		else if (s == "--rec") rec = next();
		else if (s == "--midi") midi = next();
		else if (s == "--ctl") ctl = next();
		else if (s == "--ump") ump = next();
		else if (s == "--legato") legato = true;
		else if (s == "--compare") compare = next();
		else if (s == "--seconds") seconds = std::atof(next());
		else if (s == "--tail") tail = std::atof(next());
		else if (s == "--settle") settle = std::atof(next());
		else if (s == "--nv") nv = std::atoi(next());
		else if (s == "--rate") rate = std::atoi(next());
		else if (s == "--set") {
			const std::string kv = next();
			const size_t eq = kv.find('=');
			char* end = nullptr;
			const float v = eq == std::string::npos ? 0.0f : static_cast<float>(std::strtod(kv.c_str() + eq + 1, &end));
			if (eq == std::string::npos || eq == 0 || !end || *end || end == kv.c_str() + eq + 1) fail("--set wants name=value, not " + kv);
			sets.push_back({kv.substr(0, eq), v});
		} else if (s == "--transpose") transpose = static_cast<float>(std::atof(next()));
		else if (s == "--notes") notes = parse_notes(next());
		else if (s == "--params") list_params = true;
		else if (s == "--fx") use_fx = true;
		else if (s == "--ftz") ftz = true;
		else if (s == "--out") out_path = next();
		else if (s == "--wav") wav = next();
		else if (s == "--ctl-out") ctl_out = next();
		else if (s == "--manifest") manifest = next();
		else if (s == "--check") check = true;
		else if (s == "--max-peak") max_peak = std::atof(next());
		else if (s == "--silent-after") silent_after = std::atof(next());
		else if (s == "--expect") expect = next();
		else if (s == "--golden-name") golden_name = next();
		else if (s == "--list") {
			list();
			return 0;
		} else fail("unknown argument " + s + " (see the usage at the top of sdk/tools/awva_render.cpp)");
	}

	// the engine and the preset: by name, or not at all
	if (!engine_name) fail("no --engine (--list shows them)");
	const sdk::EngineEntry* entry = sdk::find_engine(engine_name);
	if (!entry) {
		std::string known;
		for (const sdk::EngineEntry* e : sdk::engines()) known += std::string(known.empty() ? "" : ", ") + e->info->name;
		fail(std::string("no engine \"") + engine_name + "\" in this program (there are: " + known + ")");
	}
	int preset = -1;
	if (preset_arg) {
		preset = sdk::find_preset(*entry, preset_arg);
		if (preset < 0) fail(std::string("the engine ") + engine_name + " has no preset \"" + preset_arg + "\" (--list shows them)");
	}
	if (nv < 1 || nv > entry->info->budget.max_voices) fail("--nv " + std::to_string(nv) + ": the engine runs 1 to " + std::to_string(entry->info->budget.max_voices) + " voices");
	if (tail < 0 || settle < 0) fail("--tail and --settle are seconds, 0 or more");

	// the input: exactly one
	if ((perf != nullptr) + (rec != nullptr) + (midi != nullptr) + (ctl != nullptr) + (ump != nullptr) > 1) fail("one input only: --perf, --rec, --midi, --ctl or --ump");
	if (legato && !midi && !ump) fail("--legato: the MIDI mapping's mode (--midi or --ump)");
	const bool default_input = !perf && !rec && !midi && !ctl && !ump;
	if (default_input) perf = "default";
	int sr = rate;
	if (!sr) {
		const sdk::ScriptedPerformance* p = sdk::find_performance("default");
		sr = perf && sdk::find_performance(perf) ? sdk::find_performance(perf)->sample_rate : (p ? p->sample_rate : 48000);
	}

	sdk::Engine engine(*entry);
	engine.init(sr, nv);
	if (preset >= 0) engine.set_preset(preset);
	std::string sets_desc;
	for (const Set& st : sets) {
		int found = -1;
		for (int i = 0; i < engine.num_params(); i++)
			if (st.name == engine.param_desc(i)->name) found = i;
		if (found < 0) fail(std::string("the engine ") + engine_name + " has no live parameter \"" + st.name + "\" (--params shows them)");
		(void)engine.set_param(found, st.value);
		char b[96];
		std::snprintf(b, sizeof b, "%s%s=%g", sets_desc.empty() ? "" : " ", st.name.c_str(), static_cast<double>(*engine.param(found)));
		sets_desc += b;
	}
	if (list_params) {
		for (int i = 0; i < engine.num_params(); i++) {
			const dsp::ParamDesc* d = engine.param_desc(i);
			std::printf("%s %s %g %g %g\n", d->name, d->group, static_cast<double>(d->min), static_cast<double>(d->max), static_cast<double>(*engine.param(i)));
		}
		return 0;
	}

	std::string error;
	std::unique_ptr<sdk::Player> player;
	size_t ctl_blocks = 0;
	if (ctl) {
		std::unique_ptr<ControlsPlayer> cp = controls_player(ctl, nv, sr, &error);
		if (cp) ctl_blocks = cp->blocks();
		player = std::move(cp);
	} else {
		player = rec ? sdk::key_player(rec, nv, sr, &error) :
		    midi     ? sdk::midi_player(midi, nv, sr, &error, legato) :
		    ump      ? sdk::ump_player(ump, nv, sr, legato, &error) :
		               sdk::scripted_player(perf, nv, sr, &error);
	}
	if (!player) fail(error);
	const bool own_length = seconds < 0;
	if (own_length) seconds = player->seconds() + tail;

	const char* pname = engine.preset() >= 0 ? engine.preset_name(engine.preset()) : nullptr;
	char head[512];
	std::snprintf(head, sizeof head, "engine %s (%s), preset %s%d%s%s%s, %d voices, %d Hz%s%s", entry->info->name, entry->header,
	    preset_arg ? "" : "(its default) ", engine.preset(), pname ? " \"" : "", pname ? pname : "", pname ? "\"" : "", nv, sr,
	    use_fx ? ", master effects" : "", ftz ? ", subnormals flushed" : "");
	std::string input = player->describe() + (default_input ? " [the default input: no --perf, --rec or --midi]" : "");
	std::string shaping;
	if (!sets_desc.empty()) shaping += ", set " + sets_desc;
	if (transpose != 0.0f) shaping += ", transposed " + json_number(transpose);
	if (!notes.empty()) shaping += ", a list of " + std::to_string(notes.size()) + " notes";
	if (settle > 0) shaping += ", settled " + json_number(settle) + " s";
	std::fprintf(stderr, "awva_render: %s%s\nawva_render: input %s\n", head, shaping.c_str(), input.c_str());

	sdk::Effect fx(sdk::fx_effect());
	if (use_fx) fx.init(sr);

	// the input's own length in whole blocks for a controls file (no rounding of seconds)
	const int nblocks = ctl && own_length && tail == 0 ? static_cast<int>(ctl_blocks) : static_cast<int>(seconds * sr / dsp::BLOCK);
	const int channels = use_fx ? 2 : 1;
	std::vector<float> out(static_cast<size_t>(nblocks) * dsp::BLOCK * channels);
	std::vector<dsp::NoteControls> vc(static_cast<size_t>(nv), dsp::note_at_rest);
	dsp::GlobalControls g = dsp::globals_at_rest;
	// the engine gets a moved copy (the player writes only what changes): transposed, the list's notes
	const bool moved = transpose != 0.0f || !notes.empty();
	std::vector<dsp::NoteControls> ev(static_cast<size_t>(nv));
	std::vector<float> held(static_cast<size_t>(nv), 0.0f), list_note(static_cast<size_t>(nv), 69.0f), ctl_rec;
	size_t next_note = 0;
	if (ctl_out) ctl_rec.reserve(static_cast<size_t>(nblocks) * CTL_WIDTH);
	// silence first: the parameter glide arrives (nothing of it is written)
	{
		float buf[dsp::BLOCK];
		float* const eo[1] = {buf};
		const int nsettle = static_cast<int>(settle * sr / dsp::BLOCK);
		for (int b = 0; b < nsettle; b++) {
			std::optional<nsc::ScopedNoDenormals> no_denormals; // (--ftz, as below)
			if (ftz) no_denormals.emplace();
			engine.process(vc.data(), g, eo);
		}
	}
	double compute_s = 0;
	for (int b = 0; b < nblocks; b++) {
		std::optional<nsc::ScopedNoDenormals> no_denormals; // (--ftz: this block, as a component's process())
		if (ftz) no_denormals.emplace();
		player->block(b, vc.data(), g);
		const dsp::NoteControls* to = vc.data();
		if (moved) {
			for (size_t v = 0; v < vc.size(); v++) {
				if (!notes.empty() && vc[v].pressure > 0.0f && held[v] <= 0.0f) list_note[v] = notes[next_note++ % notes.size()];
				held[v] = vc[v].pressure;
				ev[v] = vc[v];
				ev[v].pitch = (notes.empty() ? vc[v].pitch : list_note[v]) + transpose;
			}
			to = ev.data();
		}
		if (ctl_out) {
			for (int v = 0; v < CTL_VOICES; v++) {
				const dsp::NoteControls c = v < nv ? to[v] : dsp::note_at_rest;
				ctl_rec.insert(ctl_rec.end(), {c.pitch, c.pressure, c.pressure_rate, c.timbre});
			}
			ctl_rec.insert(ctl_rec.end(), {g.acc_abs, g.acc_x, g.acc_y, g.acc_z, g.rot_x, g.rot_y, g.rot_z, g.pedal});
		}
		const auto t0 = std::chrono::steady_clock::now();
		float buf[dsp::BLOCK];
		float* const eo[1] = {buf};
		engine.process(to, g, eo);
		if (use_fx) {
			float l[dsp::BLOCK], r[dsp::BLOCK];
			const float* const in[1] = {buf};
			float* const fo[2] = {l, r};
			fx.process(in, fo);
			for (int n = 0; n < dsp::BLOCK; n++) {
				out[(static_cast<size_t>(b) * dsp::BLOCK + n) * 2] = l[n];
				out[(static_cast<size_t>(b) * dsp::BLOCK + n) * 2 + 1] = r[n];
			}
		} else {
			for (int n = 0; n < dsp::BLOCK; n++) out[static_cast<size_t>(b) * dsp::BLOCK + n] = buf[n];
		}
		compute_s += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	}

	size_t bad = 0, loud_tail = 0;
	float peak = 0;
	const size_t tail_from = silent_after < 0 ? out.size() : static_cast<size_t>(silent_after * sr) * channels;
	for (size_t i = 0; i < out.size(); i++) {
		const float x = out[i];
		if (!std::isfinite(x)) bad++;
		else {
			peak = std::fmax(peak, std::fabs(x));
			if (i >= tail_from && std::fabs(x) > 1e-4f) loud_tail++;
		}
	}
	if (bad) std::fprintf(stderr, "!! %zu NON-FINITE OUTPUT SAMPLES\n", bad);
	if (out_path) {
		if (!write_f32(out_path, out)) fail(std::string("cannot write ") + out_path);
		std::fprintf(stderr, "awva_render: wrote %s\n", sdk::file_identity(out_path).c_str());
	}
	if (wav) {
		if (!write_wav(wav, out, channels, sr, std::string(head) + shaping + "; input " + input)) fail(std::string("cannot write ") + wav);
		std::fprintf(stderr, "awva_render: wrote %s\n", sdk::file_identity(wav).c_str());
	}
	if (ctl_out) {
		if (!write_f32(ctl_out, ctl_rec)) fail(std::string("cannot write ") + ctl_out);
		std::fprintf(stderr, "awva_render: wrote %s\n", sdk::file_identity(ctl_out).c_str());
	}
	if (compare) {
		FILE* f = std::fopen(compare, "rb");
		if (!f) fail(std::string("cannot read ") + compare);
		std::vector<float> ref;
		float buf[4096];
		size_t got;
		while ((got = std::fread(buf, sizeof(float), 4096, f)) > 0) ref.insert(ref.end(), buf, buf + got);
		std::fclose(f);
		size_t differ = 0, first = 0;
		double maxd = 0, refpeak = 0;
		for (size_t i = 0; i < out.size() && i < ref.size(); i++) {
			refpeak = std::fmax(refpeak, std::fabs(static_cast<double>(ref[i])));
			if (std::memcmp(&out[i], &ref[i], sizeof(float)) == 0) continue;
			if (!differ++) first = i;
			maxd = std::fmax(maxd, std::fabs(static_cast<double>(out[i]) - ref[i]));
		}
		std::printf("compare %s: %zu of %zu samples differ%s", compare, differ, out.size(), ref.size() != out.size() ? " (the lengths differ)" : "");
		if (differ)
			std::printf(", the first at %.4f s, max |d| %.3g (%.1f dB re 1.0; the reference's peak %.3g)", static_cast<double>(first / channels) / sr, maxd,
			    20.0 * std::log10(maxd), refpeak);
		std::printf("\n");
	}
	const uint64_t h = hash_samples(out);
	char hs[32];
	std::snprintf(hs, sizeof hs, "%016llx", static_cast<unsigned long long>(h));
	std::fprintf(stderr, "awva_render: %.1f s audio, host compute %.3f s, peak %.3f\n", static_cast<double>(nblocks) * dsp::BLOCK / sr, compute_s, peak);
	std::printf("hash %s\n", hs);

	if (manifest) {
		std::string j = "{\n";
		const auto field = [&j](const char* k, const std::string& v, bool last = false) { j += std::string("  ") + json_string(k) + ": " + v + (last ? "\n" : ",\n"); };
		field("tool", json_string("awva_render"));
		field("engine", json_string(entry->info->name));
		field("engine_header", json_string(entry->header));
		field("preset", std::to_string(engine.preset()));
		field("preset_name", pname ? json_string(pname) : "null");
		std::string ps = "{";
		for (size_t i = 0; i < sets.size(); i++) {
			int found = 0;
			for (int k = 0; k < engine.num_params(); k++)
				if (sets[i].name == engine.param_desc(k)->name) found = k;
			ps += (i ? ", " : "") + json_string(sets[i].name) + ": " + json_number(*engine.param(found));
		}
		field("set", ps + "}");
		field("input", json_string(input));
		field("input_kind", json_string(ctl ? "ctl" : rec ? "rec" :
		                            midi                  ? "midi" :
		                            ump                   ? "ump" :
		                                                    "perf"));
		field("legato", legato ? "true" : "false");
		field("sample_rate", std::to_string(sr));
		field("block", std::to_string(dsp::BLOCK));
		field("voices", std::to_string(nv));
		field("blocks", std::to_string(nblocks));
		field("seconds", json_number(static_cast<double>(nblocks) * dsp::BLOCK / sr));
		field("settle", json_number(settle));
		field("tail", json_number(tail));
		field("transpose", json_number(transpose));
		std::string ns = "[";
		for (size_t i = 0; i < notes.size(); i++) ns += (i ? ", " : "") + json_number(notes[i]);
		field("notes", ns + "]");
		field("fx", use_fx ? "true" : "false");
		field("ftz", ftz ? "true" : "false");
		field("channels", std::to_string(channels));
		field("out", out_path ? json_string(sdk::file_identity(out_path)) : "null");
		field("wav", wav ? json_string(sdk::file_identity(wav)) : "null");
		field("ctl_out", ctl_out ? json_string(sdk::file_identity(ctl_out)) : "null");
		field("peak", json_number(peak));
		field("nonfinite", std::to_string(bad));
		field("deterministic", "true");
		field("hash", json_string(hs), true);
		j += "}\n";
		FILE* f = std::fopen(manifest, "wb");
		if (!f || std::fwrite(j.data(), 1, j.size(), f) != j.size()) fail(std::string("cannot write ") + manifest);
		std::fclose(f);
	}

	int failed = 0;
	if (check) {
		if (bad) failed = 1;
		if (peak > max_peak) {
			std::fprintf(stderr, "!! peak %.3f over %.3f\n", peak, max_peak);
			failed = 1;
		}
		if (silent_after >= 0) {
			const int active = engine.active_voices();
			if (loud_tail) {
				std::fprintf(stderr, "!! %zu samples above 1e-4 after %.2f s\n", loud_tail, silent_after);
				failed = 1;
			}
			if (active) {
				std::fprintf(stderr, "!! %d voices still active at the end\n", active);
				failed = 1;
			}
		}
	}
	const char* golden_out = std::getenv("AWVA_GOLDEN_OUT");
	if (golden_name && golden_out && *golden_out) {
		const std::string path = std::string(golden_out) + "/" + golden_name + ".hash";
		FILE* f = std::fopen(path.c_str(), "w");
		if (!f) fail("cannot write " + path);
		std::fprintf(f, "%s %016llx\n", golden_name, static_cast<unsigned long long>(h));
		std::fclose(f);
		expect = nullptr;
	}
	if (expect && std::strcmp(hs, expect) != 0) {
		std::fprintf(stderr, "!! render hash %s, expected %s (golden render changed)\n", hs, expect);
		failed = 1;
	}
	return failed;
}
