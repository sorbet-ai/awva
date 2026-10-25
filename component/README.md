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
  - the budget: QEMU instructions per block, max voices, tail;
  - the three memory regions and the stack.
- **Memory** is the host's. It provides state, fast and large regions, zero-filled and 64-byte aligned. No call allocates, blocks or throws, and nothing is global.
- **`process(events)`** takes UMP events at frames within the 32-frame block and renders one block. Inside the component, as on the board, are:
  - the decoder (`ump.h`);
  - the up-converter for MIDI 1.0 in UMP, which is what an MPE host sends;
  - the voice allocator;
  - mono legato (`mode(NSC_MODE_LEGATO, 1)`).
- **`process_voices(controls)`** is for a host that allocates voices itself. The board does today, and the virtual board's loaded engines use it: the engine's own `process()` on the host's controls.

## Answers to the CLAP prototype's questions

| # | question | answer |
|---|---|---|
| 1 | time in integer frames | An event carries its frame within the block, 0..31. The 2 ms step grid is integer: step k is at frame ceil(k × sr / 500). An event is taken at the first step at or after its frame. No ties are decided in floating point. The SDK renderer's MIDI player still uses seconds: its move to frames is a separate change, because it can move the MIDI-file goldens. |
| 2 | the 2 ms MIDI step | The component owns it, in `process()`, on the grid above. At 44.1 kHz the steps are 88 or 89 frames apart, never accumulating drift. The host only places events by frame. |
| 3 | voice-ended callback with the host's note id | Not in ABI 1. Planned for 1b: `ended(state, uint32_t* note_ids, max)`, polled after `process()`, listing notes whose voice went silent (the allocator knows the voice's level). Until then a host ends a note at its Note Off plus `tail_seconds`. |
| 4 | note ids end to end | Inside, a note is (group, channel, key) → a slot. A host keeps its own note id ↔ (channel, key) map; NOTE_END (3) will report (channel, key). |
| 5 | per-note y without a channel per note | Planned for 1b in the up-converter: Assignable Per-Note Controller 4 → y, and 3 → x, per (channel, key), so one channel carries every note. Today y is per channel (CC74 / MPE). |
| 6 | denormals | The component doesn't touch the FPU mode. The goldens were made without FTZ, so for bit-exactness a host must not set FTZ/DAZ either. The engines keep their states out of the denormal range themselves (their decays end at 0). A host may flush subnormal *outputs*, as the prototype does. |
| 7a | parameter ids | Each `nsc_param.id` is FNV-1a of its name, frozen. A renamed parameter keeps its old id: the engine then declares it explicitly (ABI 2 adds an `id` override to the engine's table). `schema_hash` covers ids, ranges and kinds. |
| 7b | defaults | A parameter's default is its value after `init()` with the preset that `preset()` reports (the engine's preset 0). A host reads them with `param()` after init. ABI 2 may declare them statically. |
| 7c | "a preset rewrote the parameters" | After `set_preset()` every parameter may have changed: the host rescans (CLAP: `rescan(CLAP_PARAM_RESCAN_VALUES)`). |
| 7d | which calls run where | **Audio thread only**, never concurrently with each other: `process`, `process_voices`, `set_param`, `set_preset`, `mode`, `reset`. **Any thread:** `desc`, `param_index`, and `param`, which reads a float. **`init` on the main thread**, before any audio-thread call. |
| 8 | level and channels | The engines are mono (`channels` = 1). Nominal level is about ±1, peaks up to ±4, never above 8 (the render tests' limit). The master effects (stereo, with their limiter at 4.5) are not part of an engine component; they become their own effect component. A plugin scales as it likes (the prototype uses ×0.25). |
| 9 | sample-rate changes | Call `init()` again at the new rate: the regions are reused, nothing is carried over, the notes end. The host re-applies its parameter values and preset after `init()`. The cost is one engine init: the trumpet's warm-start tables are static data, so it is milliseconds. |
| 10 | state and its version | The state is: the preset index, then the parameter values by id, under `schema_hash`. A value whose id is unknown is dropped; a different schema hash means a value is clamped to its range. The preset JSON of the proposal (§8) is phase 2. |
| 11 | UMP coverage | Types 4 (MIDI 2.0 channel voice) and 2 (MIDI 1.0 channel voice) play. All groups are taken as one; the group is ignored. Types 0, 1, 3, 5 and 0xD/0xF (utility, system, SysEx, stream) are ignored. |

## The Shell's gaps

The component's `process()` is the host side the Shell lacked: UMP input, events at the renderer's step grid in integer frames, and (with 3/4 above) note ids. A plugin should be written on the component, not on the Shell. The Shell keeps its job, rendering key signals and MIDI 1.0 for the SDK.

## Proof

`ctest -R component` checks:
- every library loads, and its descriptor and manifest match the engine's;
- accordion and trumpet from their libraries render bit for bit as linked in, through every preset, the scripted performances and the key takes;
- `process()` with a MIDI 2.0 performance renders bit for bit what the SDK's `MidiVoices` driving the linked engine renders, polyphonic and mono legato, on accordion, trumpet and va.

The virtual board's run (the firmware) checks the same libraries loaded from a folder against its compiled-in engines.
