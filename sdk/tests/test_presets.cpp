// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
//
// The preset format (awva/preset.h) on its own: floats written and read back bit for bit, what the
// reader takes and refuses, and survival by name across an engine's versions (resolve: clamped,
// skipped, missing, refused, exact), on the modal engine.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "doctest/doctest.h"
#include "awva_sdk/engines.h"
#include "awva_sdk/presets.h"

namespace {

namespace sdk = awva::sdk;
namespace preset = awva::preset;

uint32_t bits(float f) {
	uint32_t u;
	std::memcpy(&u, &f, sizeof u);
	return u;
}
float from_bits(uint32_t u) {
	float f;
	std::memcpy(&f, &u, sizeof f);
	return f;
}

// a one-parameter preset of engine id with the value v, written and read back
float round_trip(float v) {
	auto p = std::make_unique<preset::Preset>();
	p->engine.set("x");
	REQUIRE(p->add_param("v", v));
	const std::string json = sdk::to_json(*p);
	auto q = std::make_unique<preset::Preset>();
	preset::Error err = preset::Error::none;
	REQUIRE_MESSAGE(sdk::from_json(json, *q, &err), preset::error_name(err) << ": " << json);
	REQUIRE(q->num_params == 1);
	return q->params[0].value;
}

std::unique_ptr<preset::Preset> read_ok(const std::string& json) {
	auto p = std::make_unique<preset::Preset>();
	preset::Error err = preset::Error::none;
	REQUIRE_MESSAGE(sdk::from_json(json, *p, &err), preset::error_name(err) << ": " << json);
	return p;
}

preset::Error read_err(const std::string& json) {
	auto p = std::make_unique<preset::Preset>();
	preset::Error err = preset::Error::none;
	CHECK_FALSE(sdk::from_json(json, *p, &err));
	return err;
}

const std::string HEAD = R"({"format": "awva-preset/1", "engine": {"id": "modal"}, )";

} // namespace

TEST_CASE("preset floats: written with the fewest digits, read back bit for bit") {
	// every exponent, both signs, the subnormals, the largest and smallest normals, and a
	// pseudo-random sweep of mantissas
	uint32_t x = 0x9e3779b9u;
	int checked = 0;
	for (int k = 0; k < 200000; k++) {
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		const float f = from_bits(x);
		if (!std::isfinite(f)) continue;
		REQUIRE_MESSAGE(bits(round_trip(f)) == bits(f), "0x" << std::hex << x);
		checked++;
	}
	CHECK(checked > 190000);
	for (const uint32_t u : {0x00000000u, 0x80000000u, 0x00000001u, 0x007fffffu, 0x00800000u, 0x7f7fffffu, 0xff7fffffu, 0x3f800000u,
	         0x3dcccccdu, 0x4b7fffffu, 0x4b800001u})
		CHECK(bits(round_trip(from_bits(u))) == u);
	// the short forms the factory files have
	auto p = std::make_unique<preset::Preset>();
	p->engine.set("x");
	REQUIRE(p->add_param("a", 0.1f));
	REQUIRE(p->add_param("b", 5.0f));
	REQUIRE(p->add_param("c", -0.025f));
	REQUIRE(p->add_param("d", 16000.0f));
	const std::string json = sdk::to_json(*p);
	CHECK(json.find("\"a\": 0.1") != std::string::npos);
	CHECK(json.find("\"b\": 5") != std::string::npos);
	CHECK(json.find("\"c\": -0.025") != std::string::npos);
	CHECK(json.find("\"d\": 16000") != std::string::npos);
}

TEST_CASE("preset reader: JSON's syntax, flat, and what it refuses") {
	// unknown keys skipped, nested values too; escapes; booleans as modes; whitespace anywhere
	const auto p = read_ok(R"( { "format" : "awva-preset/1", "future": {"a": [1, {"b": null}], "c": "x"},
	    "engine": {"id": "modal", "version": "1.2.3", "params_hash": "77b3f666", "extra": true},
	    "title": "a \"b\" \\ c\/d\n", "tags": ["one", "two"], "heroes": ["level"],
	    "params": {"level": 1.5, "trem": true, "model": 2}, "modes": {"legato": true, "x": 0} } )");
	CHECK(p->engine == "modal");
	CHECK(p->version == "1.2.3");
	CHECK(p->has_hash);
	CHECK(p->params_hash == 0x77b3f666u);
	CHECK(p->title == "a \"b\" \\ c/d\n");
	CHECK(p->num_tags == 2);
	CHECK(p->num_heroes == 1);
	REQUIRE(p->param("level") != nullptr);
	CHECK(*p->param("level") == 1.5f);
	CHECK(*p->param("trem") == 1.0f);
	CHECK(p->param("nothing") == nullptr);
	REQUIRE(p->mode("legato") != nullptr);
	CHECK(*p->mode("legato"));
	CHECK_FALSE(*p->mode("x"));

	// written and read again: the same preset
	const auto q = read_ok(sdk::to_json(*p));
	CHECK(q->title == p->title.c_str());
	CHECK(q->num_params == p->num_params);
	CHECK(sdk::to_json(*q) == sdk::to_json(*p));

	CHECK(read_err(R"({"engine": {"id": "modal"}, "params": {}})") == preset::Error::no_format);
	CHECK(read_err(R"({"format": "awva-preset/2", "engine": {"id": "modal"}})") == preset::Error::no_format);
	CHECK(read_err(R"({"format": "awva-preset/1", "params": {}})") == preset::Error::no_engine);
	CHECK(read_err(HEAD + R"("params": {"level": 1.5,}})") == preset::Error::syntax);
	CHECK(read_err(HEAD + R"("params": {"level": 1.5}} trailing)") == preset::Error::syntax);
	CHECK(read_err(HEAD + R"("params": {"level": "loud"}})") == preset::Error::bad_value);
	CHECK(read_err(R"({"format": "awva-preset/1", "engine": {"id": "modal", "params_hash": "xyz"}})") == preset::Error::bad_hash);
	CHECK(read_err(HEAD + R"("title": ")" + std::string(200, 't') + R"("})") == preset::Error::too_long);
	std::string many = HEAD + R"("params": {)";
	for (int i = 0; i <= preset::MAX_PARAMS; i++) many += (i ? ", \"p" : "\"p") + std::to_string(i) + "\": 1";
	CHECK(read_err(many + "}}") == preset::Error::too_long);
	CHECK(read_err("") == preset::Error::syntax);
	// unknown values nest up to 32 deep, skipped without recursion; deeper is refused
	auto nested = [&](int depth) {
		std::string v;
		for (int i = 0; i < depth; i++) v += i % 2 ? "{\"k\": " : "[1, ";
		v += "2";
		for (int i = depth - 1; i >= 0; i--) v += i % 2 ? "}" : "]";
		return HEAD + "\"future\": " + v + "}";
	};
	read_ok(nested(32));
	CHECK(read_err(nested(33)) == preset::Error::syntax);
	CHECK(read_err(nested(100000)) == preset::Error::syntax);
	CHECK(read_err(R"({"format": "awva-preset/1", "engine": {"id": "modal"})") == preset::Error::syntax);
}

TEST_CASE("presets survive an engine's versions by name: clamped, skipped, missing, refused, exact") {
	const sdk::EngineEntry* entry = sdk::find_engine("modal");
	REQUIRE(entry != nullptr);
	sdk::Engine e(*entry);
	e.init(48000, 4);
	const std::vector<float> defaults = sdk::default_values(*entry);
	auto index = [&](const char* name) {
		for (int i = 0; i < e.num_params(); i++)
			if (std::strcmp(e.param_desc(i)->name, name) == 0) return i;
		FAIL("no parameter " << name);
		return -1;
	};
	char hash[16];
	std::snprintf(hash, sizeof hash, "%08x", sdk::params_hash(*entry));

	// every parameter as it is: exact
	std::string all = sdk::to_json(sdk::save_preset(e, "all"));
	sdk::PresetReport r = sdk::load_preset_json(e, all);
	CHECK(r.loaded);
	CHECK(r.exact);

	// an older or newer version: one value out of today's range, a step between steps, one
	// parameter it had that this one doesn't, the rest not named
	const std::string old = R"({"format": "awva-preset/1", "engine": {"id": "modal", "params_hash": "00000001"},
	    "params": {"level": 99, "model": 2.6, "retired": 0.5, "decay": 2}})";
	e.set_param(index("upper"), 0.25f); // (a missing parameter gets its default, not what was there)
	r = sdk::load_preset_json(e, old);
	REQUIRE(r.loaded);
	CHECK_FALSE(r.exact);
	CHECK(r.refused.empty());
	CHECK(r.clamped == std::vector<std::string>{"level", "model"});
	CHECK(r.skipped == std::vector<std::string>{"retired"});
	CHECK(static_cast<int>(r.missing.size()) == std::min(e.num_params() - 3, preset::MAX_REPORTED)); // (names up to MAX_REPORTED)
	CHECK(*e.param(index("level")) == e.param_desc(index("level"))->max);
	CHECK(*e.param(index("model")) == 3.0f);
	CHECK(*e.param(index("decay")) == 2.0f);
	CHECK(*e.param(index("upper")) == defaults[static_cast<size_t>(index("upper"))]);
	CHECK(e.preset() == -1);

	// every value in range under another schema hash: loaded as written, but not exact
	std::string other_hash = all;
	const size_t at = other_hash.find(hash);
	REQUIRE(at != std::string::npos);
	other_hash.replace(at, 8, "00000002");
	r = sdk::load_preset_json(e, other_hash);
	CHECK(r.loaded);
	CHECK_FALSE(r.exact);
	CHECK(r.clamped.empty());
	CHECK(r.skipped.empty());
	CHECK(r.missing.empty());

	// another engine's preset, and a text that isn't one: refused, nothing changed
	const float level = *e.param(index("level"));
	r = sdk::load_preset_json(e, R"({"format": "awva-preset/1", "engine": {"id": "va"}, "params": {"level": 0.5}})");
	CHECK_FALSE(r.loaded);
	CHECK(r.refused == "engine va");
	r = sdk::load_preset_json(e, "not json");
	CHECK_FALSE(r.loaded);
	CHECK(r.refused == "syntax");
	CHECK(*e.param(index("level")) == level);
}
