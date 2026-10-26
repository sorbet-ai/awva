<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# awva documentation

awva's engines are per-note sound engines: each note gets a pitch and its own continuous controls (pressure, the pressure's velocity, two per-note controllers called tilts), and the engine gets global inputs: the instrument's motion and a pedal. They are header-only C++ (`dsp/include/awva/`), the same code on an embedded device and on a PC.

- **[Playing with MIDI 2.0 and MPE](midi.md):** how MIDI 2.0, MPE and plain MIDI 1.0 reach the engines' controls.
- **The engine contract:** `dsp/include/awva/engine.h` (the `VoiceEngine` concept, `EngineInfo`, the memory regions). The SDK's contract suite (`sdk/tests/contract.cpp`) checks every engine against it.
- **The component ABI:** [`component/README.md`](../component/README.md), each engine as its own library through a C interface.
- **Using an engine in your own program:** from plain C or through the SDK, with the examples in [`examples/`](../examples/): the repository's [README](../README.md#use-an-engine-in-your-own-program).
- **Building and testing:** the repository's [README](../README.md).

## The engines

Each page has a reference part generated from the engine's `EngineInfo` (inputs, presets, parameters, hero parameters, budget and memory) by `awva_docs` (`sdk/tools/awva_docs.cpp`), followed by a hand-written account of how it works and how it plays. The test `docs.up_to_date` fails when the generated parts are stale; to update them, build the SDK and run `awva_docs docs`.

<!-- awva_docs: generated from the engine's EngineInfo (sdk/tools/awva_docs.cpp); run it to update, don't edit -->

| engine | what it is | presets | parameters |
|---|---|---|---|
| [accordion](engines/accordion.md) | free-reed accordion: registers, musette tuning, reed chambers, cassotto, a shared bellows (9 presets) | 9 | 42 |
| [bowed](engines/bowed.md) | bowed strings: friction-driven waveguide strings, modal body, section (9 presets) | 9 | 38 |
| [brass](engines/brass.md) | brass: a lip source and a physical bore, bell, mutes (9 presets) | 9 | 43 |
| [ep](engines/ep.md) | electric pianos: modal tines and reeds, pickups, damper (14 presets) | 14 | 25 |
| [modal](engines/modal.md) | tuned percussion: banks of damped resonators, struck or rubbed (6 presets) | 6 | 21 |
| [reed](engines/reed.md) | single reed: clarinet to saxophone, one model, tilt morphs the bore | 0 | 0 |
| [sine](engines/sine.md) | the minimal example engine: one sine per voice, level from pressure | 0 | 0 |
| [trumpet](engines/trumpet.md) | physical trumpet: lips, mouthpiece and bore, played like the brass trumpet (1 preset) | 1 | 45 |
| [va](engines/va.md) | virtual analog: saw/pulse/sub oscillators, sync, ladder-style filter, envelopes (8 presets) | 8 | 55 |
| [wind](engines/wind.md) | woodwinds: reed and jet driven bores, cylindrical and conical (9 presets) | 9 | 37 |

<!-- awva_docs: end -->

- [Writing a host](writing_a_host.md): a plugin or an embedded instrument, on the engine component (ABI 2).
