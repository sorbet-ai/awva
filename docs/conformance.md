<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Conformance

Every engine gets the same checks, at two levels, without a line of its own:

- **The engine contract** (`sdk/tests/contract*.cpp`). It drives the engine directly, as `awva::VoiceEngine`, and tests it as `contract.<engine>.<check>`.
- **The component ABI** (`component/conformance/awva_conformance.cpp`). It loads a component library through `nsc.h`, awva's or anyone else's, and tests it as `conformance.<engine>.<check>`.

Run both with the rest of the tests:

```
cmake -S sdk -B build/sdk -G Ninja && cmake --build build/sdk && ctest --test-dir build/sdk -LE "example|soak"
ctest --test-dir build/sdk -R "^contract\.va\."        # one engine's contract
ctest --test-dir build/sdk -L soak                     # the long soak (AWVA_SOAK_SECONDS, default 60)
```

## How a new engine opts in

Add its traits header, `dsp/include/awva/engines/<name>.h`, and configure again. The build makes the engine's library, its contract program (`awva_engine_<name>_contract`) and its component library, and registers every check of both levels for it. The checks read what they need from the engine's `EngineInfo`:

- **its limits:** `tail_seconds` (how soon it is silent and idle after the last release) and `max_voices`;
- **its inputs:** an input it declares must change its sound in some preset, and an input it doesn't declare must make no difference;
- **its lowest rate:** `min_rate`, 11025 unless its traits declare a higher one (`static constexpr int min_rate = 22050;` for an engine unstable or out of tune below it); its component refuses lower rates and the checks don't play below it;
- **its conformance settings** (`EngineInfo::conformance`, `engine.h`), each with a default.

| setting | default | what it allows |
|---|---|---|
| `tuning.cents` | 10 | the sustained pitch against its note's |
| `tuning.low`, `tuning.high` | 48, 84 | the notes the tuning is checked on |
| `preset_tuning[]` | none | another tuning for named presets (another range, tolerance or partial) |
| `tuning.partial`, `tuning.why` | 0, none | an inharmonic preset (a bell, a glass) has no period to compare: its partial at this ratio of the note is measured instead, as a spectral peak; `why` says why the preset is checked so |
| `rate_cents` | 3 | the sustained pitch at every rate from `min_rate` to 192000 Hz against 48 kHz |
| `rate_tolerances[]` | none | a looser bound at named rates, each a known fault with its reason (and `may_not_sound`) |
| `fuzz_peak`, `fuzz_peak_why` | 64, none | the peak with extreme but legal parameter writes (the fuzz, the presets going round, the soak): only blow-ups fail. An engine louder there declares its measured peak with a small margin, and why |

An engine sets them in its traits, only where it differs:

```cpp
static constexpr Conformance conformance = {.tuning = {.cents = 8.0f}};
static constexpr PresetTuning preset_tuning[] = {{"bell", {.cents = 2.0f, .partial = 1.0f, .why = "inharmonic: the prime is the note"}}};
```

An engine whose `init()` writes every byte of its regions declares `init_writes_memory` (`engine.h`), and the memory check then proves it.

## Known failures

A check that an engine fails today is listed in `sdk/tests/known_failures.cmake`, with its numbers and its owner. Both levels report it as a warning instead of failing, so the suite stays green while the owner fixes it. The warning also says when the check passes again; the entry then goes.

- **The tag** names a check (`silence`) or a check on one preset (`tuning:flute`).
- **The scope** is one engine: `set(AWVA_KNOWN_<engine> <tag>...)`. `AWVA_KNOWN_COMPONENT` applies to every component, for the shim.
- **The current list** is in the file itself. Keep it short.

## Exact, or within a tolerance

- **Bit for bit** wherever the contract promises the same samples: determinism, independent instances, reset against a fresh instance, the host's call or block size, late events, the output level at 1, `init_writes_memory`, an undeclared input. A regression shows as one changed bit.
- **Within a tolerance** where two paths legitimately differ by inaudible amounts, or where a level is what matters. Flushing subnormals against IEEE arithmetic must give the same 24-bit PCM (the FTZ guard check). Silence means below -120 dBFS.
- **In cents** for pitch, against the engine's declared tolerance.

## The engine contract (`contract.<engine>.*`)

| check | what it guarantees |
|---|---|
| info | the declaration is complete: name, summary, channels, voices, tail, cost, inputs, regions |
| profile | it fits the host profile (`AWVA_HOST_PROFILE`, `awva_sdk/host_profile.h`): its regions, its share of the processor |
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
| presets (files) | each factory preset is its file (`presets/<engine>/`), loaded by name, bit for bit; saved, written, read and loaded again, the same values and sound |
| FTZ guard | with the host's denormal guard (`nsc/no_denormals.h`): finite, silent after the release, the same in 24-bit PCM as without it |
| blocks | through the SDK's Shell (a plugin's host side), MIDI 2.0 and MPE events played in calls of 1, 7, 32, 256 and random frames give the same samples, bit for bit |
| denormals | under the host's FTZ guard (`nsc::ScopedNoDenormals`): after the tail, no subnormal sample and nothing above -120 dBFS. Without the guard, the subnormal tail and its cost are reported (`denormals.raw`), not failed, since the guard is the agreed design |
| memory | the engine object and its regions placed in guarded buffers: init, playing, presets, parameters, reset and init at `min_rate` and 192000 Hz write nothing outside them. With `init_writes_memory`, init over regions of any bytes leaves the same bytes as over zeros and plays the same samples |
| tuning | in every preset, a held note's sustained pitch is its note's (12-TET, A4 = 440 Hz) or an octave of it, within `tuning.cents`, on the notes from `tuning.low` to `tuning.high`; for a preset with `tuning.partial`, that partial's frequency |
| re-press | every preset, on the middle of its tuning range, soft and firm: a note held, released, and pressed again 60 or 150 ms later on its voice, still sounding, speaks as a fresh press does. It comes within 12 dB of the fresh note's sustain at most 50 ms later than the fresh note does (a strike, a rise and an attack over the tail, not the held note going on) |
| rates | at 11025, 22050, 44100, 48000, 48828, 88200, 96000 and 192000 Hz from the engine's `min_rate` up, every preset: finite, bounded, sounds, silent within the tail, and note 69 within `rate_cents` of its pitch at 48 kHz (or the rate's tolerance) |
| soak | (label `soak`) every preset: random playing with parameter writes, every voice held, the decay. Finite and bounded throughout, then silent and idle; the peaks, subnormals and cost of each phase are reported |

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
- **A host's own CMake** can call `awva_conformance_tests(<prefix> <library> [KNOWN <tag>...] [LABELS <label>...])` from any of its directories, once awva's `component/` is part of its build or it includes `component/conformance/conformance.cmake`. The call registers one ctest per check, named `<prefix><check>`. `<library>` may be a generator expression such as `$<TARGET_FILE:...>`. The checks are listed when ctest runs, so the library and `awva_conformance` only need to be built by then. A plugin built with `awva_add_component` can check its own component this way, as `component/tests/external` does.
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
| note_ids | every Note On's id comes back from `ended()` exactly once (no id lost, none twice, none unknown) through voice stealing, re-struck keys, MIDI 1.0 Note On with velocity 0, and more notes than the descriptor's `max_note_ids`. `note_ids.presets`: while the presets change under the notes. `note_ids.reset`: when `reset()` comes while notes sound |
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
