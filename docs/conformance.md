<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Conformance

Every engine gets the same checks, at two levels, without a line of its own:

- **The engine contract** (`sdk/tests/contract*.cpp`). It drives the engine directly, as `awva::VoiceEngine`, and tests it as `contract.<engine>.<check>`.
- **The component ABI** (`component/conformance/awva_conformance.cpp`). It loads a component library through `nsc.h`, awva's or anyone else's, and tests it as `conformance.<engine>.<check>`.

Run both with the rest of the tests:

```
cmake -S sdk -B build/sdk -G Ninja && cmake --build build/sdk && ctest --test-dir build/sdk -LE example
ctest --test-dir build/sdk -R "^contract\.va\."        # one engine's contract
```

## How a new engine opts in

Add its traits header, `dsp/include/awva/engines/<name>.h`, and configure again. The build makes the engine's library, its contract program (`awva_engine_<name>_contract`) and its component library, and registers every check of both levels for it. The checks read what they need from the engine's `EngineInfo`:

- **its budget:** `tail_seconds` (how soon it is silent and idle after the last release) and `max_voices`;
- **its inputs:** an input it declares must change its sound in some preset, and an input it doesn't declare must make no difference;
- **its regions** and their sizes.

## Known failures

A check that an engine fails today is listed in `sdk/tests/known_failures.cmake`, with its numbers and its owner. Both levels report it as a warning instead of failing, so the suite stays green while the owner fixes it. The warning also says when the check passes again; the entry then goes.

- **The tag** names a check (`note_ids.reset`) or a check on one preset (`tuning:flute`).
- **The scope** is one engine: `set(AWVA_KNOWN_<engine> <tag>...)`. `AWVA_KNOWN_COMPONENT` applies to every component, for the shim.
- **The current list** is in the file itself. Keep it short.

## Exact, or within a tolerance

- **Bit for bit** wherever the contract promises the same samples: determinism, independent instances, reset against a fresh instance, the host's block size, late events, the output level at 1, an undeclared input. A regression shows as one changed bit.
- **Within a tolerance** where a level is what matters: silence is below -120 dBFS.

## The engine contract (`contract.<engine>.*`)

| check | what it guarantees |
|---|---|
| info | the declaration is complete: name, summary, channels, budget, inputs, sizes |
| init | silent and idle with no input; a note sounds |
| release | silent (< 1e-4) and idle within `tail_seconds` after the last release, every preset |
| reset | back to silence with the preset and the parameter values kept; afterwards it plays exactly as a fresh engine with them |
| determinism | the same input gives the same output, bit for bit |
| independence | two instances, interleaved block by block, play as each alone (no globals) |
| fuzz | random playing and parameter writes, NaN and Inf included, in every preset: finite and bounded |
| parameters | a sane table (names, ranges, kinds); writes clamp, round and ignore NaN; presets inside the ranges; a parameter taken to its extremes and back sounds as untouched |
| heroes | every hero is one of the parameters, every preset override names a preset |
| presets | names and numbering; switching while notes sound stays finite and bounded |
| voice stealing | note jumps on held voices, instant re-strikes, every voice at once: bounded, then silent within the tail |
| inputs | an undeclared input makes no difference, bit for bit; a declared one changes the sound in some preset |
| cpu | the worst case (every voice at full pressure) runs well under real time on the PC |

## The component ABI (`awva_conformance`)

`awva_conformance` checks any library that exports `nsc_entry()`. It needs only `nsc.h`, the loader and doctest, so a plugin or an embedded host can run it in its own CI on the components it ships:

```
cmake -S component/conformance -B build/conformance -G Ninja && cmake --build build/conformance
build/conformance/awva_conformance path/to/engine.so                 # every check
build/conformance/awva_conformance path/to/engine.so -tc=note_ids*   # some
build/conformance/awva_conformance path/to/engine.so -r=junit > conformance.xml
```

- **`--known=TAG,...`:** the component's known failures, reported instead of failed.
- **`--expect=HASH`:** pin the render hash.
- **A host's own CMake** can include `component/conformance/conformance.cmake` and call `awva_conformance_tests(<prefix> <library> [KNOWN <tag>...])`, which registers one ctest per check.
- **Configured on its own,** `-DAWVA_CONFORMANCE_LIBRARIES=a.so;b.so` does the same.

| check | what it guarantees |
|---|---|
| descriptor | complete and consistent: ids, titles, version; parameter names and ids unique, ranges finite, kinds valid, defaults inside their ranges; presets named; heroes name parameters; `param_index` finds every id and refuses an unknown one |
| rates | `init()` takes every rate in `min_rate .. max_rate` (the standard rates among them, and both ends) and returns -1 outside. At each rate a chord is finite, bounded and sounds, and is silent and idle within `tail_seconds` |
| init | silent and idle with no input; a note sounds, every preset |
| reset | back to silence with the parameters and the preset kept; then exactly as a fresh instance with them |
| parameters | the declared defaults are the values after `init()`, bit for bit; writes clamp, round by kind, ignore NaN, refuse an index out of range; every preset's values are inside the ranges |
| presets | each preset is set and reads back; switching every 0.1 s while notes sound stays finite and bounded |
| determinism | the same events give the same samples; two instances interleaved share nothing |
| blocks | a host with blocks of 1, 7, 33, 256 frames and random sizes gives each event when it sees it, late ones at their negative frames. The result is bit for bit the render with every event on time, at 44.1 and 48 kHz |
| note_ids | every Note On's id comes back from `ended()` exactly once (no id lost, none twice, none unknown) through voice stealing, re-struck keys, MIDI 1.0 Note On with velocity 0, and more notes than `NSC_MAX_NOTE_IDS`. `note_ids.presets`: while the presets change under the notes. `note_ids.reset`: when `reset()` comes while notes sound |
| output | (with a parameter `output`) at 1 the samples are untouched, at 0.5 and 2 exactly half and double |
| silence | after the tail, no subnormal sample (a subnormal tail costs a host time for nothing heard) and nothing above -120 dBFS; whether it is exactly 0 is reported |
| memory | random events, parameter writes (extremes, NaN) and preset changes at `min_rate`, 48 kHz and `max_rate`. In host blocks of random sizes the output is finite and bounded, and nothing is written outside the three regions (guards before and after each) |
| voices | `process_voices()`: silent at rest, a pressed voice sounds, the same controls give the same samples |
| hash | the standard performance's render hash (FNV-1a 64 of the float bits, as `awva_render`'s), printed; with `--expect`, checked |

The standard performance is about 4 s at the rate under test:

- MIDI 2.0 notes on 8 channels, each with its velocity, pressure, pitch bend and CC74;
- a key struck again while it sounds;
- MPE in MIDI 1.0, with one Note Off sent as a Note On of velocity 0;
- the sustain pedal.

Its events are off the 2 ms grid and off block starts. Every Note On carries an id.

## Render hashes

The render goldens stay where they are: a project that embeds awva pins its commit and checks `awva_render --expect HASH` (or writes with `--golden-name N` and `AWVA_GOLDEN_OUT`), and `examples/` pins its own hashes on every OS. Hashes are FNV-1a 64 of the float bits. `awva_conformance`'s `hash` check uses the same function, so a host can pin a component's render in its own CI with `--expect`.
