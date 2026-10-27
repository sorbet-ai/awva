// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// The small shared units of dsp/: dspmath.h, svf.h, params.h, engine_set.h.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "doctest/doctest.h"
#include "awva/dspmath.h"
#include "awva/engine_set.h"
#include "awva/engines/bowed.h"
#include "awva/engines/sine.h"
#include "awva/engines/va.h"
#include "awva/engines/trumpet.h"
#include "awva/params.h"
#include "awva/svf.h"

namespace dsp = awva;

TEST_CASE("dspmath: fastpow2 (p >= 0, as the engines use it) and tan0 approximate exp2 and tan") {
	for (int k = 0; k <= 87; k++) {
		const float p = 0.137f * static_cast<float>(k);
		CAPTURE(p);
		CHECK(std::fabs(awva::fastpow2(p) / std::exp2(p) - 1.0f) < 1e-4f);
	}
	for (int k = 1; k < 30; k++) {
		const float a = 0.01f * static_cast<float>(k);
		CHECK(std::fabs(awva::tan0(a) / std::tan(a) - 1.0f) < 1e-3f); // (K_f0: up to ~4 kHz at 48 kHz)
	}
	CHECK(awva::rmax(1.0f, 2.0f) == 2.0f);
	CHECK(awva::rmin(1.0f, 2.0f) == 1.0f);
}

TEST_CASE("svf: unity gain at DC, -3 dB at the cutoff for Q = 0.707, Pade tan") {
	const float sr = 48000.0f, fc = 1000.0f;
	const awva::SvfCoef c = awva::svf_from_g(std::tan(3.14159265f * fc / sr), 0.70710678f);
	// DC: the lowpass settles at the input
	awva::SvfState s{0, 0};
	float v1 = 0, v2 = 0;
	for (int n = 0; n < 20000; n++) SVF_TICK(1.0f, s.ic1, s.ic2, c, v1, v2);
	CHECK(v2 == doctest::Approx(1.0f).epsilon(1e-4));
	// a sine at the cutoff: lowpass amplitude 1/sqrt(2)
	s = {0, 0};
	float peak = 0;
	for (int n = 0; n < 48000; n++) {
		SVF_TICK(std::sin(2 * 3.14159265f * fc * n / sr), s.ic1, s.ic2, c, v1, v2);
		if (n > 24000) peak = std::fmax(peak, std::fabs(v2));
	}
	CHECK(peak == doctest::Approx(0.70710678f).epsilon(2e-3));
	// the Pade coefficients are the tan coefficients (lowpass and bandpass terms)
	for (int k = 0; k < 11; k++) {
		const float f = 50.0f * std::pow(1.7f, static_cast<float>(k));
		const float x = 3.14159265f * f / sr;
		const awva::SvfCoef a = awva::svf_pade(x, 2.0f), b = awva::svf_from_g(std::tan(x), 2.0f);
		CAPTURE(f);
		CHECK(a.a == doctest::Approx(b.a).epsilon(1e-5));
		CHECK(a.ga == doctest::Approx(b.ga).epsilon(1e-5));
		CHECK(a.g2a == doctest::Approx(b.g2a).epsilon(1e-4));
	}
}

TEST_CASE("params: write_param clamps, rounds unless continuous, ignores NaN") {
	const awva::ParamDesc cont{"c", "g", -1.0f, 1.0f, awva::ParamKind::continuous, AWVA_NO_TEXT};
	const awva::ParamDesc steps{"s", "g", 0.0f, 8.0f, awva::ParamKind::steps, AWVA_NO_TEXT};
	float v = 0.5f;
	CHECK(!awva::write_param(&cont, &v, NAN));
	CHECK(v == 0.5f);
	CHECK(!awva::write_param(nullptr, &v, 0.1f));
	CHECK(!awva::write_param(&cont, nullptr, 0.1f));
	CHECK(awva::write_param(&cont, &v, 0.25f));
	CHECK(v == 0.25f);
	CHECK(awva::write_param(&cont, &v, 7.0f));
	CHECK(v == 1.0f);
	CHECK(awva::write_param(&cont, &v, -INFINITY));
	CHECK(v == -1.0f);
	CHECK(awva::write_param(&steps, &v, 2.4f));
	CHECK(v == 2.0f);
	CHECK(awva::write_param(&steps, &v, 2.6f));
	CHECK(v == 3.0f);
	CHECK(awva::write_param(&steps, &v, 100.0f));
	CHECK(v == 8.0f);
}

TEST_CASE("engine_set: visit reaches each engine with its own type and memory") {
	using Set = dsp::EngineSet<dsp::engines::va, dsp::engines::sine>;
	static_assert(Set::count == 2);
	static Set::Hot s; // (the engine last initialised: sine)
	static Set::Stream f;
	static Set::Bulk l;
	for (int k = 0; k < Set::count; k++)
		Set::visit(k, s, f, l, [](auto& e, auto& fast, auto& large, auto) { e.init(fast, large, 48000, 6); });
	CHECK(std::strcmp(Set::info(0)->name, "va") == 0);
	CHECK(std::strcmp(Set::info(1)->name, "sine") == 0);
	CHECK(Set::info(2) == nullptr);
	int seen = 0;
	Set::visit(1, s, [&seen](auto& e, auto i) {
		seen = static_cast<int>(decltype(i)::value) + 10 * e.voices();
	});
	CHECK(seen == 61);
	seen = -1;
	Set::visit(5, s, [&seen](auto&, auto) { seen = 0; });
	CHECK(seen == -1);
}

namespace {
// bowed without its init_writes_memory: the arena's zeroing path (every engine with regions
// says its init writes them, and the engine_set test proves it)
struct bowed_zeroed : dsp::engines::bowed {
	static constexpr bool init_writes_memory = false;
};
} // namespace

TEST_CASE("engine_set: one arena per class (the objects, each region), the largest engine's, and start gives the new engine its power-on state") {
	using B = bowed_zeroed;
	using T = dsp::engines::trumpet;
	using Set = dsp::EngineSet<B, T, dsp::engines::sine>;
	static_assert(Set::Stream::size == std::max(sizeof(B::StreamMemory), sizeof(T::StreamMemory)));
	static_assert(Set::Bulk::size == std::max(sizeof(B::BulkMemory), sizeof(T::BulkMemory)));
	static_assert(Set::Stream::size < sizeof(B::StreamMemory) + sizeof(T::StreamMemory), "shared, not added up");
	static_assert(Set::Hot::size == std::max({sizeof(B), sizeof(T), sizeof(dsp::engines::sine)}), "one engine object, the largest");
	static_assert(!dsp::init_writes_memory<B> && dsp::init_writes_memory<T>);
	// the fit check: an arena smaller than a region doesn't hold it
	static_assert(!dsp::Arena<8, 4>::holds<B::StreamMemory>());
	static_assert(dsp::Arena<sizeof(B::StreamMemory), alignof(B::StreamMemory)>::holds<B::StreamMemory>());

	static Set::Hot s, s0;
	static Set::Stream fast, fast0;
	static Set::Bulk large, large0;
	// what ran before left its data in the arenas
	std::memset(s.bytes, 0x5a, sizeof s.bytes);
	std::memset(fast.bytes, 0x5a, sizeof fast.bytes);
	std::memset(large.bytes, 0x5a, sizeof large.bytes);
	const auto zeroed = [](const void* p, size_t n) {
		const auto* b = static_cast<const unsigned char*>(p);
		for (size_t i = 0; i < n; i++)
			if (b[i] != 0) return false;
		return true;
	};
	for (int k : {1, 0}) { // trumpet (the smaller regions, init writes them), then bowed over it (zeroed)
		CAPTURE(k);
		bool before_init = false, object_zeroed = false;
		size_t fast_n = 0, large_n = 0;
		Set::start(k, s, fast, large, [&](auto& e, auto& f, auto& l, auto) {
			CHECK(static_cast<void*>(&e) == static_cast<void*>(s.bytes)); // the arenas' starts
			CHECK(static_cast<void*>(&f) == static_cast<void*>(fast.bytes));
			CHECK(static_cast<void*>(&l) == static_cast<void*>(large.bytes));
			object_zeroed = zeroed(&e, sizeof e); // whatever engine used the object's arena before
			before_init = zeroed(&f, sizeof f) && zeroed(&l, sizeof l);
			fast_n = sizeof f;
			large_n = sizeof l;
			e.init(f, l, 48000, 10);
		});
		// the engine's object is zeroed before its init; without the trait its regions too
		CHECK(object_zeroed);
		if (k == 0) CHECK(before_init);
		// either way, its regions are what init leaves over zeros (power on)
		std::memset(&fast0, 0, sizeof fast0);
		std::memset(&large0, 0, sizeof large0);
		Set::start(k, s0, fast0, large0, [](auto& e, auto& f, auto& l, auto) { e.init(f, l, 48000, 10); });
		CHECK(std::memcmp(fast.bytes, fast0.bytes, fast_n) == 0);
		CHECK(std::memcmp(large.bytes, large0.bytes, large_n) == 0);
	}
}
