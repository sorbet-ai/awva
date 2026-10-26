# The engine component (C ABI)

`include/nsc/nsc.h` is the ABI. `include/nsc/shim.h` makes any `awva::VoiceEngine` a component; the engine itself is unchanged. `include/nsc/load.h` is the host's loader. Each engine builds as its own shared library, `<build>/components/<id>.dll|.dylib|.so`, next to `<id>.json`, the manifest written by `nsc_manifest` from the built library.

```
cmake -S sdk -B build/sdk -G Ninja && cmake --build build/sdk && ctest --test-dir build/sdk -R component
```

## The interface

- **One export, `nsc_entry()`.** It returns a descriptor and the functions.
- **The descriptor** is static and readable before any instance exists. It holds:
  - id, title, summary, version (the repository commit) and the ABI;
  - the parameters (stable id, name, group, range, kind);
  - the factory presets, by name, each with its hero parameters, and the engine's own heroes;
  - the budget: Cortex-M7 instructions per block (counted under QEMU), max voices, tail;
  - the three memory regions and the stack.
- **Memory** is the host's. It provides state, fast and large regions, zero-filled and 64-byte aligned. No call allocates, blocks or throws, and nothing is global.
- **`process(events)`** takes UMP events at frames within the 32-frame block and renders one block. Inside the component are:
  - the decoder (`ump.h`);
  - the up-converter for MIDI 1.0 in UMP, which is what an MPE host sends;
  - the voice allocator;
  - mono legato (`mode(NSC_MODE_LEGATO, 1)`).
- **Presets as text.** `load_preset(json)` takes an `awva-preset/1` file (`awva/preset.h`) by name. The report counts what was clamped, skipped (names this engine lacks) and missing (set to their values after `init()`). Another engine's preset, or text that isn't a preset, is refused with nothing changed. `save_preset(title)` writes the current values as such a file. ABI 2 added these two calls.
- **`process_voices(controls)`** is for a host that allocates voices itself, such as an embedded instrument: the engine's own `process()` on the host's controls.

## The plugin lane's findings (ABI 2)

- **Late events:** `nsc_event.frame` is signed. An event that came after its block was rendered is given at its frame before the block, down to -32, and is taken at its own step: exact at every rate, no added latency.
- **The version:** `AWVA_VERSION`, set at configure time (`-DAWVA_VERSION=...`). Otherwise it is `git describe` of awva's own checkout (a submodule's too), never an enclosing repository's; an export without `.git` gives "unknown".
- **A title:** `nsc_desc.name` is a title for people ("Electric Piano", "Virtual Analog"), from the engine's traits (`title`). `id` stays the short name.
- **A static component:** `awva_add_component(<target> <engine> STATIC|SHARED|MODULE)` builds an engine as a component library: the entry, the shim, the MIDI side, the defaults. A plugin project links it and copies no glue; the libraries here are built with it too.
- **An output level:** every component has the parameter `output` after the engine's own (0..2, default 1). At 1 the samples are the engine's, bit for bit. reed and sine thus have a parameter too.
- **Sample rates:** `min_rate` .. `max_rate` in the descriptor: 11025 .. 192000, or from a higher `min_rate` where the engine's traits declare one (an engine unstable or out of tune below some rate). `init()` returns -1 for a rate outside them and does nothing; hosts refuse such rates. Outside its range an engine may go silent or loud.
- **A colour:** `nsc_desc.led` (0xrrggbb, 0 for none) is the engine's colour, from its traits' `led`; none have one yet.

## Answers to the CLAP prototype's questions

| # | question | answer |
|---|---|---|
| 1 | time in integer frames | An event carries its frame within the block (0..31). The 2 ms steps are on an integer grid: step k is at frame ceil(k × sr / 500), and an event is taken at the first step at or after its frame. One rule, in `awva_sdk/timed_midi.h`, used by the component, the Shell and the renderer (`awva_render`: a MIDI file event at its time rounded to the nearest frame). No ties are decided in floating point. |
| 2 | the 2 ms MIDI step | The component owns it, in `process()`, on the grid above. At 44.1 kHz the steps are 88 or 89 frames apart, never accumulating drift. The host only places events by frame. |
| 3 | voice-ended callback with the host's note id | `nsc_event.note_id` carries the host's id on a Note On (-1: none). `ended(state, ids, max)`, polled after `process()`, hands out the ids of the notes that ended: released, then either the engine's `tail_seconds` passed, or the engine went idle, or another note took their key. |
| 4 | note ids end to end | The host's id goes in with the Note On (`note_id`) and comes back from `ended()`. Inside, a note is (channel, key) → a slot, as before. |
| 5 | per-note y without a channel per note | The up-converter (`midi_in.h`) takes MIDI 2.0 per-note y: Assignable Per-Note Controller 4, or Registered Per-Note Controller 74 (brightness), bipolar. A note with its own y keeps it; the channel's CC74 moves only the others. Assignable Per-Note Controller 3 is x. One channel carries every note. |
| 6 | denormals | `process()` and `process_voices()` run with subnormals flushed (`nsc/no_denormals.h`: MXCSR FTZ+DAZ on x86-64, FPCR.FZ on AArch64; the host's mode saved and restored around the call, as JUCE's ScopedNoDenormals). Several engines' decaying states otherwise reach the subnormal range and stay there, which costs a PC 10 to 50 times an idle block. Only tails far below hearing change (under 1e-34 on the engines here), and nothing at all in 24-bit PCM (the contract's FTZ guard case checks that on every engine). On 32-bit Arm the guard does nothing: its samples and the target goldens are unchanged. The host needn't set anything. |
| 7a | parameter ids | Each `nsc_param.id` is FNV-1a of its name, frozen. A renamed parameter keeps its old id: the engine then declares it explicitly (ABI 2 adds an `id` override to the engine's table). `schema_hash` covers ids, ranges and kinds. |
| 7b | defaults | Declared: `nsc_param.def` is the value after `init()`, measured from the engine at build time (`component/tools/awva_defaults.cpp`) and checked bit for bit by the tests. |
| 7c | "a preset rewrote the parameters" | After `set_preset()` every parameter may have changed: the host rescans (CLAP: `rescan(CLAP_PARAM_RESCAN_VALUES)`). |
| 7d | which calls run where | **Audio thread only**, never concurrently with each other: `process`, `process_voices`, `set_param`, `set_preset`, `mode`, `reset`. **Any thread:** `desc`, `param_index`, and `param`, which reads a float. **`init` on the main thread**, before any audio-thread call. |
| 8 | level and channels | The engines are mono (`channels` = 1). Nominal level is about ±1, peaks up to ±4, never above 8 (the render tests' limit). The master effects (stereo, with their limiter at 4.5) are not part of an engine component; they become their own effect component. A plugin scales as it likes (the prototype uses ×0.25). |
| 9 | sample-rate changes | Call `init()` again at the new rate: the regions are reused, nothing is carried over, the notes end. The host re-applies its parameter values and preset after `init()`. The cost is one engine init: the trumpet's warm-start tables are static data, so it is milliseconds. |
| 10 | state and its version | The state is a preset file: `save_preset()` writes it, `load_preset()` reads it back (`awva-preset/1`, parameters by name under `params_hash`: the engine's parameters, without the component's `output`). Across versions a value is clamped to its range, an unknown name is skipped, and a missing one gets its value after `init()`. The report says which happened. A host that wants the factory preset index as well stores `preset()` beside it. |
| 11 | UMP coverage | Types 4 (MIDI 2.0 channel voice) and 2 (MIDI 1.0 channel voice) play. All groups are taken as one; the group is ignored. Types 0, 1, 3, 5 and 0xD/0xF (utility, system, SysEx, stream) are ignored. |

## The Shell's gaps, closed

- The Shell (`awva_sdk/shell.h`) now takes UMP: `ump(words, offset, note_id)`.
- Its MIDI, MIDI 1.0 and UMP alike, is taken at the renderer's step rule, in integer frames (`timed_midi.h`), not at the start of the block an event falls in.
- Its notes carry host note ids, handed back by `ended_notes()`.

## Proof

`ctest -R component` checks:
- every library loads, and its descriptor and manifest match the engine's;
- accordion and trumpet from their libraries render bit for bit as linked in, through every preset, the scripted performances and the key takes;
- `process()` with a MIDI 2.0 performance renders bit for bit what the SDK's `MidiVoices` driving the linked engine renders, polyphonic and mono legato, on accordion, trumpet and va.

`awva_conformance <library>` checks the ABI on any component library, this repository's or another's (`conformance/`; [docs/conformance.md](../docs/conformance.md)). The tests run it on every engine's library as `conformance.<engine>.*`.
