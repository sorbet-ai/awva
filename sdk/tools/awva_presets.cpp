// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// awva_presets: the factory presets as files.
//
//   awva_presets export <dir>   every engine's factory presets, as the engine has them now, into
//                               <dir>/<engine>/<NN>-<name>.preset.json (presets/ in the repository)
//   awva_presets show <file>    a preset file read and written again (what the reader understood)
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "awva_sdk/engines.h"
#include "awva_sdk/presets.h"

namespace sdk = awva::sdk;

namespace {

std::string slug(const char* name) {
	std::string s;
	for (const char* c = name; *c; c++) {
		const unsigned char u = static_cast<unsigned char>(*c);
		if (std::isalnum(u)) s += static_cast<char>(std::tolower(u));
		else if (!s.empty() && s.back() != '-') s += '-';
	}
	while (!s.empty() && s.back() == '-') s.pop_back();
	return s;
}

int export_all(const std::filesystem::path& dir) {
	int files = 0;
	for (const sdk::EngineEntry* entry : sdk::engines()) {
		if (entry->info->num_presets <= 0 || entry->info->num_params <= 0) continue;
		const std::filesystem::path d = dir / entry->info->name;
		std::filesystem::create_directories(d);
		for (int i = 0; i < entry->info->num_presets; i++) {
			sdk::Engine e(*entry);
			e.init(48000, 1);
			e.set_preset(i);
			auto p = std::make_unique<awva::preset::Preset>(sdk::save_preset(e, e.preset_name(i)));
			char nn[16];
			std::snprintf(nn, sizeof nn, "%02d-", i);
			const std::filesystem::path f = d / (nn + slug(e.preset_name(i)) + ".preset.json");
			std::ofstream(f, std::ios::binary) << sdk::to_json(*p);
			files++;
		}
	}
	std::printf("%d preset files in %s\n", files, dir.string().c_str());
	return 0;
}

int show(const char* file) {
	std::ifstream f(file, std::ios::binary);
	std::stringstream ss;
	ss << f.rdbuf();
	auto p = std::make_unique<awva::preset::Preset>();
	awva::preset::Error err = awva::preset::Error::none;
	if (!sdk::from_json(ss.str(), *p, &err)) {
		std::fprintf(stderr, "%s: %s\n", file, awva::preset::error_name(err));
		return 1;
	}
	std::fputs(sdk::to_json(*p).c_str(), stdout);
	return 0;
}

} // namespace

int main(int argc, char** argv) {
	const std::string cmd = argc > 1 ? argv[1] : "";
	if (cmd == "export" && argc == 3) return export_all(argv[2]);
	if (cmd == "show" && argc == 3) return show(argv[2]);
	std::fprintf(stderr, "usage: awva_presets export <dir> | show <file>\n");
	return 2;
}
