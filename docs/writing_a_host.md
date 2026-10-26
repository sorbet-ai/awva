<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Writing a host

A host loads an engine component (`component/include/nsc/nsc.h`, ABI 3), gives it memory, sends it MIDI 2.0 and reads its audio. There are two kinds:
- **a plugin**, which plays from a DAW's notes;
- **an embedded instrument**, which reads its own sensors and allocates voices itself.

They share everything below except the way in: a plugin uses `process()` and an instrument uses `process_voices()`.

## 1. Find and check it

- Build the component, or take its library: `<id>.dll` / `.dylib` / `.so`, with `<id>.json` (its manifest) beside it. A project of your own can build one with `awva_add_component(<target> <engine> STATIC|SHARED|MODULE)`; the sources are generated in your build directory.
- Load it, and call `nsc_entry()`. `component/include/nsc/load.h` does this, and checks that `desc->magic` is `NSC_MAGIC` and `desc->abi` is `NSC_ABI`. Refuse a component of another ABI.
- Read the descriptor. It is static and readable before any instance exists:
  - `id` (the short name), `uid` (`com.sorbet-ai.awva.<id>`, the plugin or package id), `name` (a title for people), `short_name` (1 to 6 characters, for narrow displays), `summary`, `version`;
  - `led`: its colour, 0xrrggbb (never 0);
  - `min_rate` .. `max_rate`: the sample rates it supports;
  - `max_voices`, `channels`, `tail_seconds`, `instr_per_block` (the budget);
  - the three region sizes;
  - the parameters, the presets, the heroes.

## 2. Give it memory and a rate

- Allocate the regions: `state_bytes`, `fast_bytes`, `large_bytes`, zero-filled and 64-byte aligned. `nsc_instance_new()` in `load.h` does it on the heap. An embedded host without a heap places them in its own memories.
- Call `init(state, fast, large, sample_rate, voices)`, on the main thread. It returns -1 for a rate outside `min_rate` .. `max_rate`; offer only those rates.
- On a rate change, `init()` again: the regions are reused, nothing carries over, and the notes end. Re-apply your parameter values and preset after it.

## 3. Play it

**A plugin: `process(state, events, n, out)`**, one block of `NSC_BLOCK` (32) frames per call. Split the host's buffer into 32-frame blocks.
- An event is a UMP packet with its frame, counted from the block's start. An event that arrives after its block was rendered gets a negative frame, down to -32; it lands where it would have on time. The component takes MIDI in 2 ms steps on an integer-frame grid, so place events by frame and nothing more.
- Send MIDI 2.0 (UMP type 4) in its standard meaning, or MIDI 1.0 in UMP (type 2, MPE included). One channel can carry every note: per-note timbre as Registered Per-Note Controller 74, per-note pitch as Pitch 7.25 (docs/midi.md has the table, a sensor's controllers included).
- Give a Note On the host's note id (`nsc_event.note_id`, else -1). After each `process()`, drain `ended()` (call it again while it returns `max`) and send each id's note-end (CLAP_EVENT_NOTE_END).
- Mono legato: `mode(state, NSC_MODE_LEGATO, 1)`.

**An instrument: `process_voices(state, voices, globals, out)`.** The host allocates voices itself and gives each its controls (`nsc_voice`: pitch, pressure, pressure rate, timbre, the velocities and the gate, as `awva::NoteControls`). The component only renders, bit for bit as the same engine compiled in.

## 4. Parameters and presets

- Parameters are by index, each with a stable id (`nsc_param.id`, FNV-1a of its name). Store values by id, under `schema_hash`; a host can look an id up with `param_index()`.
- Each has `min`, `max`, `kind` (continuous, steps, toggle) and a declared default `def`: its value after `init()`.
- The last parameter is `output`, the output level: 0 .. 2, default 1. At 1 the samples are the engine's, bit for bit.
- `set_param()` takes the engines' write rule: NaN is ignored, steps are rounded, and the engine clamps. `set_preset(i)` loads a factory preset; afterwards, rescan every parameter.

## 5. Threads, level, denormals

- The audio thread only, never concurrently: `process`, `process_voices`, `set_param`, `set_preset`, `mode`, `reset`.
- Any thread: the descriptor, `param_index`, `param`.
- The main thread: `init`, before any audio-thread call.
- Output is mono, nominally about ±1, with peaks up to ±4. Scale as you like.
- Denormals: the component flushes them itself during `process()` and `process_voices()`, and restores your FPU mode on return. Nothing to set. The SDK's own renders run without the guard; the two differ only in tails far below hearing, and not at all in 24-bit PCM.

## 6. Check it

- `ctest -R component` in awva: every library loads, renders bit for bit as linked in, and the UMP path matches the SDK's renderer.
- `component.add_component_from_outside` builds a component from a project outside awva.
