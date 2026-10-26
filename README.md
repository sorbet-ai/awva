<p align="center"><img src="docs/logo/awva.svg" alt="awva" width="480"></p>

# awva

Expressive per-note sound engines, playable by MIDI 2.0, MPE or key signals; an SDK for the PC; and the engine component, a C ABI to load each engine from its own library (`component/`).

They work with the Striso board as a supported host: its firmware takes this repository as a git submodule, pinned by commit. (github.com/sorbet-ai/awva; ids under com.sorbet-ai.awva.)

| | |
|---|---|
| `dsp/` | the engines, header only: the contract (`engine.h`), the shared helpers, the UMP profile and decoder, MIDI in, the voice allocator, mono legato, the preset format (`preset.h`); `engines/<name>.h` traits, `effects/fx.h` |
| `sdk/` | one library per engine, the contract suite, the renderer (`awva_render`), the docs generator (`awva_docs`), the shell, MIDI files |
| `presets/` | the factory presets, one file each: `<engine>/NN-<name>.preset.json` (below) |
| `docs/` | the engines' reference pages and how they work, and playing them with MIDI 2.0 and MPE: [docs/README.md](docs/README.md) |
| `examples/` | using an engine in your own program: from plain C (`c_host`), and a MIDI file through the SDK (`mpe_file`) |
| `tools/harness/` | the scripted performances, `harness_util.h`, key recordings (`rec/`) |
| `third_party/doctest` | the test framework |

**The component (ABI 3).** `component/include/nsc/nsc.h` is a C ABI that loads one engine from its own shared library. It takes UMP in, at integer frames, with late events exact. Parameters have stable ids and declared defaults, and an `output` level follows the engine's own. Notes carry host ids, handed back by `ended()`. The descriptor gives a title, a short name, a colour, the heroes, the presets and the sample rates (11025 .. 192000). `awva_add_component()` builds an engine as a component library inside another project. See `component/README.md`, and `docs/writing_a_host.md` for a host: a plugin or the board.

Build and test on their own:

```
cmake -S sdk -B build/sdk -G Ninja && cmake --build build/sdk && ctest --test-dir build/sdk
```

## Use an engine in your own program

Two ways in, each with a small example in `examples/` that the tests build and run:

- **From any language, through the component ABI.** Each engine is also a shared library (`build/sdk/components/<engine>.dll`, `.so` or `.dylib`) with one exported C function, `nsc_entry()`, declared in [`component/include/nsc/nsc.h`](component/include/nsc/nsc.h). The host allocates the memory the engine asks for, calls `init()`, and calls `process()` once per 32-frame block with that block's MIDI 2.0 messages (Universal MIDI Packets). [`examples/c_host/c_host.c`](examples/c_host/c_host.c) does all of it in plain C: it loads an engine at run time, checks the ABI, lists the presets and parameters, plays a phrase with per-note pitch and pressure, tags each note with its own id and hears back from `ended()` when each is over, and writes a WAV.

  ```
  build/sdk/examples/c_host build/sdk/components va va.wav
  ```

- **From C++, through the SDK.** Link `awva_sdk` and `awva_engines` and use the engines directly, with the SDK's MIDI in (`sdk::TimedMidi` over `sdk::MidiVoices`: MIDI 1.0 and MPE up-converted to MIDI 2.0, decoded, and given voices on the board's 2 ms steps). [`examples/mpe_file/mpe_file.cpp`](examples/mpe_file/mpe_file.cpp) renders a standard MIDI file, MPE or plain, sample for sample as `awva_render --midi` does; `--demo` writes an MPE file to try.

  ```
  build/sdk/examples/mpe_file --demo demo.mid && build/sdk/examples/mpe_file demo.mid demo.wav va
  ```

How the MIDI messages map to the engines' controls: [docs/midi.md](docs/midi.md).

## Presets

A preset is one JSON file, `awva-preset/1` (`dsp/include/awva/preset.h`): the engine's id and its parameter schema hash, a title, description, tags, the hero parameters, every parameter **by name**, and the modes. The same format is used by the factory presets, the SDK (`awva_sdk/presets.h`: load with a report, save), the component (`load_preset` / `save_preset`) and the board's preset store.

A preset survives an engine's versions by name, with no migration code. A value out of range is clamped, a name the engine no longer has is skipped, and a parameter the preset doesn't name gets its default; each is reported. Another engine's preset is refused.

The factory presets are the files in `presets/`, and nothing else: each engine reads its files at compile time (`dsp/include/awva/presets/<engine>.inc`, their texts). A file that lacks a parameter or has one out of range doesn't compile. To change one, edit the file, then run `cmake -P tools/presets_inc.cmake`; the test `presets_inc` fails if that was forgotten. The contract proves that each file, loaded by name, plays bit for bit as the engine's preset.

The render goldens, the QEMU target renders and the board-vs-shell tests run in the firmware repository, against the commit it pins. A change here shows up there as a golden diff in the pin bump.

Licence: Apache-2.0 (`LICENSE`, `NOTICE`). `PROVENANCE.md` says where each file comes from. The Striso firmware that includes this repository stays GPL-3.0; Apache-2.0 code in a GPLv3 work is compatible (the FSF lists Apache-2.0 as GPLv3-compatible), not the other way round.
