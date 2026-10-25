# Provenance

This records where each file of this repository comes from. The repository was split out of the Striso firmware fork on 2026-10-25, with a fresh history: no commit here holds code ported from the upstream Striso firmware. The fork keeps its full history.

**Licence:** Apache-2.0 (`LICENSE`, `NOTICE`); every file says `SPDX-License-Identifier: Apache-2.0`, copyright Sorbet AI. The Striso firmware that takes this repository as a submodule stays GPL-3.0: including Apache-2.0 code in a GPLv3 work is compatible.

## Written for this project (from 2026-10-16)

| files | what |
|---|---|
| `dsp/include/awva/engine.h`, `adapter.h`, `engine_set.h`, `params.h`, `placement.h` | the engine contract and its helpers |
| `dsp/include/awva/ump.h`, `midi_in.h`, `voice_allocator.h`, `legato.h` | the UMP profile and decoder, the MIDI 1.0/MPE up-converter, the voice allocator, mono legato |
| `dsp/include/awva/voice_{va,modal,bowed,reed,sine,wind,ep,brass,trumpet,accordion}.h`, `voice_wind_tune.h`, `trumpet_{bore,cal,seed}.h` | the engines |
| `dsp/include/awva/engines/*.h`, `effects/fx.h` | the engines' and the effects' contract traits |
| `dsp/include/awva/fx.h` | the master effects (drive, phaser, chorus, tremolo, delay, Dattorro plate, limiter). The upstream `synth.dsp` has no such chain: only a stereo width delay, which is not here. |
| `sdk/`, `tools/harness/` (performances, `harness_util.h`, `rec/` key recordings) | the SDK, its tests, the renderer, the scripted performances |
| `tools/trumpet/` | the trumpet's bore fits, calibration and warm-start table generators |
| `docs/`, `sdk/tools/awva_docs.cpp` | the documentation: reference tables generated from the engines' EngineInfo, the engines' accounts written from their headers' own comments |
| `examples/` | programs that use an engine: from plain C through the component ABI (`c_host`), and a MIDI file through the SDK's MIDI in (`mpe_file`) |

The following records rules and behaviour that the upstream firmware's `synth_control.cpp` also implements: whether these are re-statements of MIDI/MPE practice or derivations is a question for a legal check.
- `midi_in.h` and `voice_allocator.h` hold the MIDI-in mapping and the voice-stealing rule. They were extracted from the fork's `synth_control.cpp`, where they had been rewritten in this project.
- `controls.h` describes the controls the upstream firmware writes.

## Rewritten for the split

These were split out of `strisy.h`, the port of the upstream `synth.dsp` that stays in the firmware. For this repository they were written again from the sources named, keeping the same arithmetic in the same order: every render golden is bit-identical.

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
| `dspmath.h` `tan0` coefficients (0.2033, 0.31755) | Faust `fast.lib` by Piers Titus van der Torren (in the upstream Striso repository) | Apache-2.0 |
| `third_party/doctest` | doctest | MIT (its own LICENSE) |

These two formulas can't be replaced without changing every golden. Both licences are permissive and compatible with Apache-2.0; their notices are kept (`NOTICE`). The tan0 coefficients are the only piece taken from the upstream Striso repository, from a file the upstream itself licenses Apache-2.0 (`fast.lib`), not from its GPL code.

fastapprox notice (BSD-3-Clause):

> Copyright (C) 2012 Paul Mineiro. All rights reserved. Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met: Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution. Neither the name of Paul Mineiro nor the names of other contributors may be used to endorse or promote products derived from this software without specific prior written permission. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## Not here (they stay in the firmware fork, GPL)

- `strisy.h`, the port of `synth.dsp`, and its traits `engines/strisy.h`.
- `fastpow.h`, Paul Mineiro's file as the upstream ships it.
- `ump_out.h`, the board's MIDI 2.0 output edge.
- The board-only tests: the VOICE=all memory test and the `ump_out` tests.

The engines' comments still mention `synth.dsp` and `strisy` where they compare behaviour; that is a description, not code.
