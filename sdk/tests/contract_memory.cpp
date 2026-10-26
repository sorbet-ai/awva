// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// memory: the engine by its own type (AWVA_ENGINE_TYPE, from its traits AWVA_ENGINE_TRAITS), its
// object and its regions placed by the test as a host places them (docs/conformance.md):
//   guards              a guard before and after the object, StreamMemory and BulkMemory; init,
//                       playing, presets, parameter writes, reset and init at other rates write
//                       nothing outside them
//   init_writes_memory  (an engine that declares it, engine.h) init over regions of any bytes
//                       leaves the bytes it leaves over zeros, and plays the same samples
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

#include AWVA_ENGINE_TRAITS
#include "contract.h"

using namespace contract; // NOLINT(google-build-using-namespace): the suite's own helpers

namespace {

using E = AWVA_ENGINE_TYPE;
static_assert(dsp::VoiceEngine<E>);

// n bytes, 64-byte aligned, with a guard of GUARD bytes before and after
class Guarded {
public:
	static constexpr size_t GUARD = 1024;
	static constexpr uint8_t PATTERN = 0xa5;
	explicit Guarded(size_t n) :
	    n_(n), mem_(n + 2 * GUARD + 64, PATTERN) {
		uint8_t* base = mem_.data() + GUARD;
		p_ = base + (64 - reinterpret_cast<uintptr_t>(base) % 64) % 64;
		std::memset(p_, 0, n_);
	}
	[[nodiscard]] uint8_t* data() const { return p_; }
	[[nodiscard]] size_t size() const { return n_; }
	[[nodiscard]] bool intact() const {
		for (const uint8_t* q = mem_.data(); q < p_; q++)
			if (*q != PATTERN) return false;
		for (const uint8_t* q = p_ + n_; q < mem_.data() + mem_.size(); q++)
			if (*q != PATTERN) return false;
		return true;
	}

private:
	size_t n_;
	std::vector<uint8_t> mem_;
	uint8_t* p_ = nullptr;
};

// the engine in three guarded buffers
struct Placed {
	Guarded object{sizeof(E)}, stream{sizeof(typename E::StreamMemory)}, bulk{sizeof(typename E::BulkMemory)};
	E* e = nullptr;
	Placed() { e = new (object.data()) E; } // (plain data: zero-filled storage is an engine before init)
	[[nodiscard]] typename E::StreamMemory& f() const { return *std::launder(reinterpret_cast<typename E::StreamMemory*>(stream.data())); }
	[[nodiscard]] typename E::BulkMemory& l() const { return *std::launder(reinterpret_cast<typename E::BulkMemory*>(bulk.data())); }
	void init(int rate) const { e->init(f(), l(), rate, nvoices()); }
	[[nodiscard]] bool intact() const { return object.intact() && stream.intact() && bulk.intact(); }
};

// n blocks of the keys on the placed engine (before(b) as play())
std::vector<float> run(E& e, Keys& k, int n, const std::function<void(int)>& before = {}) {
	std::vector<float> out;
	for (int b = 0; b < n; b++) {
		k.step();
		if (before) before(b);
		float buf[dsp::BLOCK];
		float* const o[1] = {buf};
		e.process(k.vc.data(), k.g, o);
		out.insert(out.end(), buf, buf + dsp::BLOCK);
	}
	return out;
}

} // namespace

TEST_CASE("memory: init, playing, presets, parameters, reset and other rates write nothing outside the object and its regions") {
	CAPTURE(name());
	Placed m;
	m.init(SR);
	Keys k(nvoices());
	Rng r{5u};
	int next = 0;
	(void)run(*m.e, k, blocks(2.0), [&](int b) {
		fuzz_controls(r, k);
		const int np = info().num_params; // (not E::num_params: 0 for some, a constant % 0)
		if (np > 0 && b % 3 == 0) {
			const int i = static_cast<int>(r.next() * static_cast<float>(np)) % np;
			(void)m.e->set_param(i, fuzz_value(r, *E::param_desc(i)));
		}
		if (info().num_presets > 0 && b % 200 == 0) m.e->set_preset(next++ % info().num_presets);
	});
	m.e->reset();
	(void)run(*m.e, k, blocks(0.5), [&](int b) { phrase(k, b); });
	for (int rate : {info().min_rate, 192000}) { // (its lowest rate and the highest)
		m.init(rate);
		Keys kr(nvoices(), rate);
		(void)run(*m.e, kr, blocks(1.0, rate), [&](int b) { phrase(kr, b); });
	}
	CHECK(m.object.intact());
	CHECK(m.stream.intact());
	CHECK(m.bulk.intact());
}

TEST_CASE("memory: init_writes_memory: init over regions of any bytes leaves what it leaves over zeros and plays the same") {
	CAPTURE(name());
	if constexpr (!dsp::init_writes_memory<E>) {
		MESSAGE(name() << ": no init_writes_memory (a host zero-fills its regions before init)");
		return;
	} else {
		for (int p : {presets().front(), presets().back()}) {
			CAPTURE(preset_label(p));
			const auto start = [p](Placed& m) {
				m.init(SR);
				if (p >= 0) m.e->set_preset(p);
			};
			Placed zero;
			start(zero);
			const std::vector<uint8_t> stream0(zero.stream.data(), zero.stream.data() + zero.stream.size());
			const std::vector<uint8_t> bulk0(zero.bulk.data(), zero.bulk.data() + zero.bulk.size());
			Keys k0(nvoices());
			const std::vector<float> ref = run(*zero.e, k0, blocks(2.0), [&](int b) { phrase(k0, b); });
			for (int fill : {0x5a, 0xff, -1}) { // (-1: random bytes)
				CAPTURE(fill);
				Placed m; // (the object zeroed, as a host zeroes an engine's object before its init)
				uint32_t s = 1234u;
				for (Guarded* g : {&m.stream, &m.bulk})
					for (size_t i = 0; i < g->size(); i++) {
						s = s * 1664525u + 1013904223u;
						g->data()[i] = static_cast<uint8_t>(fill >= 0 ? fill : static_cast<int>(s >> 24));
					}
				start(m);
				// (a region the engine doesn't have, NoMemory, is never written)
				const bool stream_same = std::is_empty_v<typename E::StreamMemory> || std::memcmp(m.stream.data(), stream0.data(), stream0.size()) == 0;
				const bool bulk_same = std::is_empty_v<typename E::BulkMemory> || std::memcmp(m.bulk.data(), bulk0.data(), bulk0.size()) == 0;
				Keys k(nvoices());
				const bool samples_same = same_bits(run(*m.e, k, blocks(2.0), [&](int b) { phrase(k, b); }), ref);
				CAPTURE(stream_same);
				CAPTURE(bulk_same);
				CAPTURE(samples_same);
				const bool as_over_zeros = stream_same && bulk_same && samples_same;
				CONFORM("init_writes_memory", p, as_over_zeros);
				CHECK(m.intact());
			}
		}
	}
}
