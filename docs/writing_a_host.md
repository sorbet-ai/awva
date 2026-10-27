<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Writing a host

A host loads an engine component (`component/include/nsc/nsc.h`, ABI 7), gives it memory, sends it MIDI 2.0 and reads its audio. There are two kinds:
- **a plugin**, which plays from a DAW's notes;
- **an embedded instrument**, which reads its own sensors and allocates voices itself.

They share everything below except the way in: a plugin uses `process()` and an instrument uses `process_voices()`.

## 1. Find and check it

- Build the component, or take its library: `<id>.dll` / `.dylib` / `.so`, with `<id>.json` (its manifest) beside it. A project of your own can build one with `awva_add_component(<target> <engine> STATIC|SHARED|MODULE)`; the sources are generated in your build directory.
- Load it, and call `nsc_entry()`. `component/include/nsc/load.h` does this, and checks that `desc->magic` is `NSC_MAGIC` and `desc->abi` is `NSC_ABI`. Refuse a component of another ABI.
- Read the descriptor. It is static and readable before any instance exists:
  - `id` (the short name), `uid` (`com.sorbet-ai.awva.<id>`, the plugin or package id), `name` (a title for people), `short_name` (1 to 6 characters, for narrow displays), `summary`, `version`;
  - `colour`: its colour, 0xrrggbb (never 0);
  - `min_rate` .. `max_rate`: the sample rates it supports;
  - `max_voices`, `channels`, `tail_seconds`, `cost` (instructions per voice and frame on the reference core: its worst preset; times your voices and rate, the share of your processor it needs);
  - the three region sizes;
  - the parameters, the presets, the heroes.

## 2. Give it memory and a rate

- Allocate the regions: `hot_bytes`, `stream_bytes`, `bulk_bytes` (by how they are used: the hot one every sample and small, the stream one every sample and large, the bulk one rarely), zero-filled and 64-byte aligned. `nsc_instance_new()` in `load.h` does it on the heap. An embedded host without a heap places them in its own memories.
- Call `init(state, stream, bulk, &config)`, on the main thread, with `nsc_config {sample_rate, control_hz, voices}`: `control_hz` 0 takes MIDI every engine block (a plugin's), an embedded host passes its scan rate (500 Hz). It returns -1 for a rate outside `min_rate` .. `max_rate`; offer only those rates.
- On a rate change, `init()` again: the regions are reused, nothing carries over, and the notes end. Re-apply your parameter values and preset after it.

## 3. Play it

**A plugin: `process(state, frames, events, n, out)`**, any number of frames per call: the host's buffer as it comes. The engine runs 32-frame blocks inside, each rendered when its first frame is due, so the calls' sizes make no difference to the samples.
- An event is a UMP packet with its frame, counted from the call's start. An event that arrives after its frame was rendered gets a negative frame, down to -32. The component takes MIDI in control steps on an integer-frame grid (`nsc_config.control_hz`), so place events by frame and nothing more.
- Send MIDI 2.0 (UMP type 4) in its standard meaning, or MIDI 1.0 in UMP (type 2, MPE included). One channel can carry every note: per-note timbre as Registered Per-Note Controller 74, per-note pitch as Pitch 7.25 (docs/midi.md has the table, a sensor's controllers included).
- Give a Note On the host's note id (`nsc_event.note_id`, else -1). After each `process()`, drain `ended()` (call it again while it returns `max`) and send each id's note-end (CLAP_EVENT_NOTE_END). Pass at most `desc->max_events` events a block; `desc->max_note_ids` notes are tracked at once (a build's sizes: `AWVA_MIDI_QUEUE`, `AWVA_NOTE_IDS`).
- Mono: `mode(state, NSC_MODE_MONO, 1)`: one voice, played by every held key (`mono.h`: the player slurs or re-articulates by the keys' pressures, the engine's mono family, `nsc_desc.mono` (`EngineInfo::mono`), says how its pitch moves). A host that plays mono itself, through `process_voices()`, takes the same family for its own player.

**An instrument: `process_voices(state, voices, globals, out)`.** The host allocates voices itself and gives each its controls (`nsc_voice`: pitch, pressure, pressure rate, timbre, the velocities and the gate, as `awva::NoteControls`). The component only renders, bit for bit as the same engine compiled in.

**The floating-point mode.** On a PC, `process()` and `process_voices()` flush subnormals to zero (`no_denormals.h`), so a decaying tail costs nothing. A host whose samples must be bit for bit an embedded host's (a virtual device of an instrument; 32-bit Arm keeps its mode) sets `mode(state, NSC_MODE_KEEP_DENORMALS, 1)` after `init()`: the component then runs in the host's mode as it is.

**The audio thread** runs `process()`, `process_voices()` and every call that changes the engine (`init`, `reset`, `mode`, `set_preset`, `set_param`, `load_preset`); `save_preset` runs on any thread. An embedded host gives that thread the module header's `stack_bytes`: the deepest of those calls, `load_preset`'s 5 KB included.

## 4. Parameters and presets

- Parameters are by index, each with a stable id (`nsc_param.id`, FNV-1a of its name). Store values by id, under `schema_hash`; a host can look an id up with `param_index()`.
- Each has `min`, `max`, `kind` (continuous, steps, toggle) and a declared default `def`: its value after `init()`.
- The last parameter is `output`, the output level: 0 .. 2, default 1. At 1 the samples are the engine's, bit for bit.
- For people, each has a `label` ("Cutoff"), a `hover` text (what it does, for a tooltip, worded for MIDI 2.0 and MPE) and an `icon`: a concept, or `concept+modifier`, whose drawings are `assets/icons/*.svg` ([Parameter icons](icons.md)). Show the icon before the label and the hover text as its tooltip. A component built with `AWVA_PARAM_TEXT 0` (an embedded host that shows no text) has NULL there: show the name.
- `set_param()` takes the engines' write rule: NaN is ignored, steps are rounded, and the engine clamps. `set_preset(i)` loads a factory preset; afterwards, rescan every parameter.

## 5. Threads, level, denormals

- The audio thread only, never concurrently: `process`, `process_voices`, `set_param`, `set_preset`, `mode`, `reset`.
- Any thread: the descriptor, `param_index`, `param`.
- The main thread: `init`, before any audio-thread call.
- Output is mono, nominal ±1 at full scale 1 (`OUTPUT_SCALE`, engine.h): play it at unity.
- Denormals: the component flushes them itself during `process()` and `process_voices()`, and restores your FPU mode on return. Nothing to set. The SDK's own renders run without the guard; the two differ only in tails far below hearing, and not at all in 24-bit PCM.

## 6. Check it

- `ctest -R component` in awva: every library loads, renders bit for bit as linked in, and the UMP path matches the SDK's renderer.
- `component.add_component_from_outside` builds a component from a project outside awva.
