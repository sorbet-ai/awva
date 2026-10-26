# Provenance

This records where each file of this repository comes from. The repository was split out of an embedded instrument firmware on 2026-10-25, with a fresh history.

**Licence:** Apache-2.0 (`LICENSE`, `NOTICE`); every file says `SPDX-License-Identifier: Apache-2.0`, copyright Sorbet AI. Apache-2.0 code can be included in GPL-3.0 projects.

## Written for this project (from 2026-10-16)

| files | what |
|---|---|
| `dsp/include/awva/engine.h`, `adapter.h`, `engine_set.h`, `params.h`, `placement.h` | the engine contract and its helpers |
| `dsp/include/awva/ump.h`, `midi_in.h`, `voice_allocator.h`, `legato.h` | the UMP profile and decoder, the MIDI 1.0/MPE up-converter, the voice allocator, mono legato |
| `dsp/include/awva/voice_{va,modal,bowed,reed,sine,wind,ep,brass,trumpet,accordion}.h`, `voice_wind_tune.h`, `trumpet_{bore,cal,seed}.h` | the engines |
| `dsp/include/awva/engines/*.h`, `effects/fx.h` | the engines' and the effects' contract traits |
| `dsp/include/awva/preset.h`, `presets/<engine>/*.preset.json`, `dsp/include/awva/presets/*.inc` (generated from them), `tools/presets_inc.cmake` | the preset format and the factory presets as files. JSON has no comments, so the preset files carry no SPDX line: they are Apache-2.0, copyright Sorbet AI, like the rest. |
| `dsp/include/awva/fx.h` | the master effects (drive, phaser, chorus, tremolo, delay, Dattorro plate, limiter). |
| `sdk/`, `tools/harness/` (performances, `harness_util.h`, `rec/` recorded performances) | the SDK, its tests, the renderer, the scripted performances |
| `tools/trumpet/` | the trumpet's bore fits, calibration and warm-start table generators |
| `tools/fit/`, `docs/fitting.md` | the fitting toolkit, consolidated from the instrument lanes' tools (all written for this project), and the distances tested against listener verdicts. `tools/fit/awvafit/sources.json` lists recordings by URL and hash; no recording is in the repository |
| `docs/`, `sdk/tools/awva_docs.cpp` | the documentation: reference tables generated from the engines' EngineInfo, the engines' accounts written from their headers' own comments |
| `examples/` | programs that use an engine: from plain C through the component ABI (`c_host`), and a MIDI file through the SDK's MIDI in (`mpe_file`) |

The following records rules and behaviour that the originating firmware also implements: whether these are re-statements of MIDI/MPE practice or derivations is a question for a legal check.
- `midi_in.h` and `voice_allocator.h` hold the MIDI-in mapping and the voice-stealing rule. They were extracted from that firmware, where they had been rewritten in this project.
- `controls.h` describes the controls that firmware writes.

## Rewritten for the split

These were split out of a synth port that stays in the originating firmware. For this repository they were written again from the sources named, keeping the same arithmetic in the same order: every render golden is bit-identical.

They were rewritten by an agent that had seen the originals. This is **not a clean-room rewrite**; the owner's legal check covers that.

| file | from |
|---|---|
| `controls.h` | trivial structs: the block length, five per-voice and eight global controls |
| `svf.h` | the trapezoidal (TPT) state-variable filter: V. Zavalishin, *The Art of VA Filter Design* (2012), ch. 4; A. Simper, *Linear Trapezoidal Integrated SVF* (Cytomic, 2013). `svf_pade` uses the [7/6] Padé approximant of tan. |
| `dspmath.h` | `rmax`/`rmin`, and the `real` type: first principles. `fastpow2` and `tan0`: see the next section |

## Third-party, permissively licensed (kept bit for bit, with attribution)

| piece | origin | licence |
|---|---|---|
| `dspmath.h` `fastpow2` | Paul Mineiro, *fastapprox* (`fastpow2`), the formula and its constants | BSD-3-Clause, notice below |
| `dspmath.h` `tan0` coefficients (0.2033, 0.31755) | Faust `fast.lib` by Piers Titus van der Torren | Apache-2.0 |
| `third_party/doctest` | doctest | MIT (its own LICENSE) |

These two formulas can't be replaced without changing every golden. Both licences are permissive and compatible with Apache-2.0; their notices are kept (`NOTICE`). The tan0 coefficients come from `fast.lib`, a file its author licenses Apache-2.0.

fastapprox notice (BSD-3-Clause):

> Copyright (C) 2012 Paul Mineiro. All rights reserved. Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met: Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution. Neither the name of Paul Mineiro nor the names of other contributors may be used to endorse or promote products derived from this software without specific prior written permission. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
