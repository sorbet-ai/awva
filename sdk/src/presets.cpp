// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// Sound presets on the PC (presets.h).
#include "awva_sdk/presets.h"

#include <map>
#include <memory>
#include <mutex>

namespace awva::sdk {

uint32_t params_hash(const EngineEntry& e) {
	return preset::schema_hash(e.info->num_params, [&](int i) { return e.param_desc(i); });
}

std::vector<float> default_values(const EngineEntry& e) {
	static std::mutex m;
	static std::map<const EngineEntry*, std::vector<float>> cache;
	const std::lock_guard<std::mutex> lock(m);
	auto it = cache.find(&e);
	if (it != cache.end()) return it->second;
	Engine d(e);
	d.init(48000, 1);
	std::vector<float> v(static_cast<size_t>(d.num_params()));
	for (int i = 0; i < d.num_params(); i++) v[static_cast<size_t>(i)] = *d.param(i);
	return cache.emplace(&e, v).first->second;
}

namespace {
template <int N> void names(std::vector<std::string>& out, const preset::Str<preset::NAME> (&list)[N], int count) {
	for (int i = 0; i < count && i < N; i++) out.emplace_back(list[i].c_str());
}
} // namespace

PresetReport load_preset(Engine& e, const preset::Preset& p) {
	PresetReport r;
	const int n = e.num_params();
	const std::vector<float> defaults = default_values(e.entry());
	std::vector<float> values(static_cast<size_t>(n > 0 ? n : 1));
	const preset::Resolved res = preset::resolve(
	    p, e.info().name, params_hash(e.entry()), n, [&](int i) { return e.param_desc(i); },
	    defaults.data(), values.data());
	if (res.refused) {
		r.refused = std::string("engine ") + p.engine.c_str();
		return r;
	}
	e.load_params(values.data(), -1);
	r.loaded = true;
	r.exact = res.exact;
	names(r.clamped, res.clamped, res.num_clamped);
	names(r.skipped, res.skipped, res.num_skipped);
	names(r.missing, res.missing, res.num_missing);
	return r;
}

PresetReport load_preset_json(Engine& e, std::string_view json) {
	auto p = std::make_unique<preset::Preset>();
	preset::Error err = preset::Error::none;
	if (!from_json(json, *p, &err)) {
		PresetReport r;
		r.refused = preset::error_name(err);
		return r;
	}
	return load_preset(e, *p);
}

preset::Preset save_preset(Engine& e, std::string_view title, std::string_view description, std::string_view version) {
	preset::Preset p;
	p.engine.set(e.info().name);
	p.version.set(std::string(version).c_str());
	p.params_hash = params_hash(e.entry());
	p.has_hash = true;
	p.title.set(std::string(title).c_str());
	p.description.set(std::string(description).c_str());
	for (const char* h : e.heroes().name)
		if (h && p.num_heroes < preset::MAX_HEROES) p.heroes[p.num_heroes++].set(h);
	for (int i = 0; i < e.num_params(); i++) p.add_param(e.param_desc(i)->name, *e.param(i));
	return p;
}

std::string to_json(const preset::Preset& p) {
	std::string out(32768, '\0');
	const size_t n = preset::write(p, out.data(), out.size());
	out.resize(n);
	return out;
}

bool from_json(std::string_view json, preset::Preset& p, preset::Error* error) {
	const preset::Error e = preset::read(json.data(), json.size(), p);
	if (error) *error = e;
	return e == preset::Error::none;
}

} // namespace awva::sdk
