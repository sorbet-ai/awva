// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// EngineSet<E...>: a fixed set of VoiceEngines chosen at compile time (a host that runs one of
// them at a time), as three plain-data objects the host places itself:
//
//   EngineSet<...>::Hot      the engine object they share       (Region::hot)
//   EngineSet<...>::Stream   one StreamMemory region they share (Region::stream)
//   EngineSet<...>::Bulk     one BulkMemory region they share   (Region::bulk)
//
// Only one engine runs at a time, so the engine object and its memory regions are one arena per
// class, as bulk as the largest engine's: while an engine runs, its object is the start of the
// Hot arena and its StreamMemory and BulkMemory the start of the others. start(k, ...) hands the
// arenas to engine k: its object's share is zeroed, and its regions' share unless its init() writes
// all of them itself (init_writes_memory, engine.h: the same state, without writing the bytes
// twice in the switch block); then its init() runs. So an engine always starts from its state at
// power on, whatever ran before; nothing of it is kept while another engine runs (init() sets its
// preset's parameters anyway).
//
// visit(k, f): f(engine k) with engine k's own type, as a chain of compares the compiler inlines
// (no function pointers: a static stack analysis can follow every call). Its object's members are
// those of the engine last started; its static members (info, presets, parameter descriptions)
// are any engine's.
#ifndef AWVA_ENGINE_SET_H_
#define AWVA_ENGINE_SET_H_

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <new>
#include <utility>

#include "engine.h"

namespace awva {

namespace detail {

template <size_t I, class T, class... R> struct Nth {
	using type = typename Nth<I - 1, R...>::type;
};
template <class T, class... R> struct Nth<0, T, R...> {
	using type = T;
};

} // namespace detail

// One memory region the engines take turns in (their objects, or a region class): Bytes of storage,
// aligned for every type it holds. Plain data (zero-filled storage is valid). as<M>() is the
// object or region of type M at its start.
template <size_t Bytes, size_t Align> struct Arena {
	static constexpr size_t size = Bytes;
	static constexpr size_t align = Align;
	alignas(Align) std::byte bytes[Bytes];

	template <class M> static constexpr bool holds() { return sizeof(M) <= Bytes && alignof(M) <= Align; }

	template <class M> [[nodiscard]] M& as() {
		static_assert(holds<M>(), "an engine's memory region doesn't fit the arena");
		// the arena's bytes provide the storage; M is plain data (an implicit-lifetime type)
		return *std::launder(reinterpret_cast<M*>(bytes));
	}
	// the state at power on for a region of type M: its bytes zeroed
	template <class M> void clear() { std::memset(bytes, 0, sizeof(M)); }
};

// the arena for every region type M...: as bulk and as aligned as the largest
template <class... M> using SharedArena = Arena<std::max({sizeof(M)...}), std::max({alignof(M)...})>;

// Fits<Arena, Engine, Region>::value: a check that names the engine (and the region) in the
// compiler's message when its region doesn't fit an arena of a fixed size
template <class A, class Engine, class Region> struct Fits {
	static constexpr bool value = A::template holds<Region>();
	static_assert(value, "this engine's region (the template arguments: arena, engine, region) is larger than the arena");
};

template <VoiceEngine... E> struct EngineSet {
	static constexpr int count = sizeof...(E);
	template <size_t I> using engine = typename detail::Nth<I, E...>::type;

	using Hot = SharedArena<E...>;
	using Stream = SharedArena<typename E::StreamMemory...>;
	using Bulk = SharedArena<typename E::BulkMemory...>;
	static_assert((Fits<Stream, E, typename E::StreamMemory>::value && ...));
	static_assert((Fits<Bulk, E, typename E::BulkMemory>::value && ...));

	// f(engine k) for 0 <= k < count (nothing otherwise); f gets the engine and its index as a
	// std::integral_constant
	template <class F> static void visit(int k, Hot& s, F&& f) {
		visit_(k, s, f, std::index_sequence_for<E...>());
	}
	// the same with the engine's memory regions in the arenas: f(engine, stream, bulk, index).
	// Only for the engine the arenas were started for (start below).
	template <class F> static void visit(int k, Hot& s, Stream& stream, Bulk& bulk, F&& f) {
		visit_mem_(k, s, stream, bulk, f, std::index_sequence_for<E...>());
	}
	// engine k takes over the arenas: its object and regions zeroed, then init(engine, stream, bulk,
	// index) (the engine's init with them). Call it where the engines run (the synth thread): the
	// engine that ran before must not touch the arenas again.
	template <class F> static void start(int k, Hot& s, Stream& stream, Bulk& bulk, F&& init) {
		visit_mem_(k, s, stream, bulk, init, std::index_sequence_for<E...>(), true);
	}

	// EngineInfo of engine k (nullptr outside)
	[[nodiscard]] static const EngineInfo* info(int k) {
		static constexpr const EngineInfo* infos[] = {&E::info...};
		return k >= 0 && k < count ? infos[k] : nullptr;
	}

private:
	template <class F, size_t... I> static void visit_(int k, Hot& s, F& f, std::index_sequence<I...>) {
		(void)((k == static_cast<int>(I) && (f(s.template as<engine<I>>(), std::integral_constant<size_t, I>()), true)) || ...);
	}
	template <size_t I, class F> static void with_mem_(Hot& s, Stream& stream, Bulk& bulk, F& f, bool clear) {
		using StreamM = typename engine<I>::StreamMemory;
		using BulkM = typename engine<I>::BulkMemory;
		if (clear) {
			s.template clear<engine<I>>();
			if (!init_writes_memory<engine<I>>) {
				stream.template clear<StreamM>();
				bulk.template clear<BulkM>();
			}
		}
		f(s.template as<engine<I>>(), stream.template as<StreamM>(), bulk.template as<BulkM>(), std::integral_constant<size_t, I>());
	}
	template <class F, size_t... I>
	static void visit_mem_(int k, Hot& s, Stream& stream, Bulk& bulk, F& f, std::index_sequence<I...>, bool clear = false) {
		(void)((k == static_cast<int>(I) && (with_mem_<I>(s, stream, bulk, f, clear), true)) || ...);
	}
};

} // namespace awva

#endif // AWVA_ENGINE_SET_H_
