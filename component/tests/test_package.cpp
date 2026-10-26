// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
//
// The engine package (nsc/package.h, docs/packages.md) as tools/nsc_pack.py writes it from this
// build: a component's package (its library as this PC's image, its preset files, the manifest)
// and a presets-only one, read back with package.h's own checks.
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "nsc/package.h"

#ifdef NSC_TEST_PACKAGES // (the packages are written by tools/nsc_pack.py: a build with Python)

namespace {

std::vector<uint8_t> read_file(const std::filesystem::path& p) {
	std::ifstream f(p, std::ios::binary);
	REQUIRE_MESSAGE(f.good(), p.string());
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

std::vector<nscpkg::Section> sections(const std::vector<uint8_t>& b) {
	std::vector<nscpkg::Section> out(nscpkg::rd32(b.data() + offsetof(nscpkg::PackageHeader, n_sections)));
	for (size_t i = 0; i < out.size(); i++) std::memcpy(&out[i], b.data() + 64 + 48 * i, sizeof out[i]);
	return out;
}

std::string text_of(const std::vector<uint8_t>& b, const nscpkg::Section& s) {
	return std::string(reinterpret_cast<const char*>(b.data() + s.offset), s.size);
}

size_t preset_files(const char* engine) {
	size_t n = 0;
	for (const auto& f : std::filesystem::directory_iterator(std::string(AWVA_PRESETS_DIR) + "/" + engine))
		n += f.path().string().size() > 12 && f.path().string().ends_with(".preset.json");
	return n;
}

// this PC, with room for any engine (the checks of the regions and the stack are an embedded host's)
constexpr nscpkg::Target kPC = {NSC_PACKAGE_TAG, NSC_PACKAGE_TAG, "", "", 1u << 30, 1u << 30, 1u << 30, 0, 1};

} // namespace

TEST_CASE("package: a component's package is its library as this PC's image, its presets and a manifest") {
	const std::vector<uint8_t> b = read_file(std::string(NSC_TEST_PACKAGES) + "/va.awvapkg");
	nscpkg::Reason why;
	REQUIRE_MESSAGE(nscpkg::check_package(b.data(), b.size(), static_cast<uint32_t>(b.size()), why), why.text);
	const std::vector<nscpkg::Section> s = sections(b);
	REQUIRE(!s.empty());
	for (const nscpkg::Section& x : s) CHECK(nscpkg::crc32(0, b.data() + x.offset, x.size) == x.crc);
	// the manifest first: the package's own fields and the engine's
	CHECK(s[0].kind == nscpkg::KIND_MANIFEST);
	const std::string m = text_of(b, s[0]);
	CHECK(m.find("\"format\": \"awva-package/2\"") != std::string::npos);
	CHECK(m.find("\"label\": \"va, a test package\"") != std::string::npos);
	CHECK(m.find("\"id\": \"va\"") != std::string::npos);
	// built with the MIDI tables every host of a package takes (AWVA_PACKAGE_MIDI)
	CHECK(m.find("\"max_events\": 256,") != std::string::npos);
	CHECK(m.find("\"max_note_ids\": 16,") != std::string::npos);
	CHECK(m.find(std::string("\"images\": [\n    \"") + NSC_PACKAGE_TAG + "\"\n  ]") != std::string::npos);
	// this PC's image: its module header passes, and its image is the library, byte for byte
	nscpkg::Section img;
	REQUIRE(nscpkg::find_section(b.data(), nscpkg::KIND_IMAGE, NSC_PACKAGE_TAG, &img) >= 0);
	nscpkg::ModuleHeader h;
	std::memcpy(&h, b.data() + img.offset, sizeof h);
	REQUIRE_MESSAGE(nscpkg::check_module(h, img.size, kPC, why), why.text);
	CHECK(std::string(h.id) == "va");
	CHECK(h.abi == NSC_ABI);
	CHECK(h.reloc_count == 0);
	CHECK(nscpkg::crc32(0, b.data() + img.offset + sizeof h, img.size - sizeof h) == h.payload_crc);
	const std::vector<uint8_t> lib = read_file(std::string(NSC_PACKAGE_LIBS) + "/va" + NSC_COMPONENT_SUFFIX);
	CHECK(h.image_size == lib.size());
	CHECK(std::memcmp(b.data() + img.offset + sizeof h, lib.data(), lib.size()) == 0);
	// no image for another target
	CHECK(nscpkg::find_section(b.data(), nscpkg::KIND_IMAGE, "armv7em-fpv5-d16-softfp", nullptr) < 0);
	// the engine's preset files, in name order
	std::vector<std::string> tags;
	for (const nscpkg::Section& x : s)
		if (x.kind == nscpkg::KIND_PRESET) tags.emplace_back(x.tag);
	CHECK(tags.size() == preset_files("va"));
	CHECK(std::is_sorted(tags.begin(), tags.end()));
	CHECK(tags.front() == "00-classic");
}

TEST_CASE("package: a presets-only package has a manifest and presets, no image") {
	const std::vector<uint8_t> b = read_file(std::string(NSC_TEST_PACKAGES) + "/bowed_presets.awvapkg");
	nscpkg::Reason why;
	REQUIRE_MESSAGE(nscpkg::check_package(b.data(), b.size(), static_cast<uint32_t>(b.size()), why), why.text);
	const std::vector<nscpkg::Section> s = sections(b);
	const std::string m = text_of(b, s[0]);
	CHECK(m.find("\"id\": \"bowed\"") != std::string::npos);
	CHECK(m.find("\"images\": []") != std::string::npos);
	CHECK(nscpkg::find_section(b.data(), nscpkg::KIND_IMAGE, nullptr, nullptr) < 0);
	CHECK(s.size() == 1 + preset_files("bowed"));
}

TEST_CASE("package: a byte changed is refused by the crcs, with the words every host uses") {
	const std::vector<uint8_t> good = read_file(std::string(NSC_TEST_PACKAGES) + "/va.awvapkg");
	const auto refused = [&](size_t at) {
		std::vector<uint8_t> b = good;
		b[at] ^= 1;
		nscpkg::Reason why;
		CHECK_FALSE(nscpkg::check_package(b.data(), b.size(), static_cast<uint32_t>(b.size()), why));
		return std::string(why.text);
	};
	CHECK(refused(0) == "not a package");
	CHECK(refused(13) == "package header crc");
	CHECK(refused(64 + 40) == "section table crc");
	nscpkg::Reason why;
	CHECK_FALSE(nscpkg::check_package(good.data(), good.size(), static_cast<uint32_t>(good.size()) + 4, why));
	CHECK(std::string(why.text).rfind("package size ", 0) == 0);
}

#endif // NSC_TEST_PACKAGES
