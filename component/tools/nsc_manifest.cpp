// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
//
// nsc_manifest <component library> [<out.json>]: loads a component (nsc/load.h) and writes its
// manifest, the descriptor as JSON: what a host lists before loading it.
#include <cstdio>
#include <string>

#include "nsc/load.h"

namespace {

std::string esc(const char* s) {
	std::string o = "\"";
	for (; s && *s; s++) {
		if (*s == '"' || *s == '\\') o += '\\';
		if (static_cast<unsigned char>(*s) < 0x20) continue;
		o += *s;
	}
	return o + "\"";
}

} // namespace

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: nsc_manifest <component library> [<out.json>]\n");
		return 2;
	}
	nsc_library lib;
	const int r = nsc_load(argv[1], &lib);
	if (r != 0) {
		std::fprintf(stderr, "nsc_manifest: %s: not loaded (%d)\n", argv[1], r);
		return 1;
	}
	const nsc_desc& d = *lib.c->desc;
	std::string j = "{\n";
	j += "  \"id\": " + esc(d.id) + ",\n  \"uid\": " + esc(d.uid) + ",\n  \"name\": " + esc(d.name) + ",\n  \"short_name\": " + esc(d.short_name) + ",\n  \"summary\": " + esc(d.summary) + ",\n";
	j += "  \"abi\": " + std::to_string(d.abi) + ",\n  \"version\": " + esc(d.version) + ",\n";
	char h[16];
	std::snprintf(h, sizeof h, "%08x", d.schema_hash);
	j += R"(  "schema_hash": ")" + std::string(h) + "\",\n";
	std::snprintf(h, sizeof h, "%06x", d.colour);
	j += R"(  "colour": ")" + std::string(h) + "\",\n";
	j += "  \"channels\": " + std::to_string(d.channels) + ",\n  \"max_voices\": " + std::to_string(d.max_voices) + ",\n";
	j += "  \"max_events\": " + std::to_string(d.max_events) + ",\n  \"max_note_ids\": " + std::to_string(d.max_note_ids) + ",\n  \"max_notes\": " + std::to_string(d.max_notes) + ",\n";
	char cost[32];
	std::snprintf(cost, sizeof cost, "%.6g", static_cast<double>(d.cost));
	j += "  \"cost\": " + std::string(cost) + ",\n";
	j += "  \"hot_bytes\": " + std::to_string(d.hot_bytes) + ",\n  \"stream_bytes\": " + std::to_string(d.stream_bytes) +
	    ",\n  \"bulk_bytes\": " + std::to_string(d.bulk_bytes) + ",\n";
	j += "  \"params\": [";
	for (int i = 0; i < d.num_params; i++) j += std::string(i ? ", " : "") + esc(d.params[i].name);
	j += "],\n  \"presets\": [";
	for (int i = 0; i < d.num_presets; i++) j += std::string(i ? ", " : "") + esc(d.presets[i].name);
	j += "]\n}\n";
	nsc_unload(&lib);
	FILE* f = argc > 2 ? std::fopen(argv[2], "wb") : stdout;
	if (!f) return 1;
	std::fputs(j.c_str(), f);
	if (f != stdout) std::fclose(f);
	return 0;
}
