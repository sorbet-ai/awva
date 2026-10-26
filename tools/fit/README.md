<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# tools/fit: rendering engines and measuring them against recordings

One toolkit for every instrument fit: render any engine, fetch and cut recordings into notes, measure them, score a build, fit its parameters, bake them into the presets, and make listening packs. The method, the licence rules, which distances to trust, and the migration notes per instrument are in [docs/fitting.md](../../docs/fitting.md).

- **`awva_render`** (`sdk/tools/awva_render.cpp`) is the one renderer.
- **`awvafit`** (here) is the Python around it.
- Each instrument is a config module on the shared core: `awvafit/instruments/<name>.py`.

## Set up

```
cmake -S sdk -B build/sdk -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/sdk --target awva_render
python -m venv .venv-fit                                  # in the checkout, git-ignored; nothing system-wide
.venv-fit/bin/pip install -r tools/fit/requirements.txt   # (Windows: .venv-fit\Scripts\pip)
.venv-fit/bin/pip install -r tools/fit/requirements-jtfs.txt   # optional: the JTFS distance (torch, kymatio)
```

Environment (no path is written in the code):

| variable | what |
|---|---|
| `AWVA_RENDER` | the renderer, `build/sdk/awva_render` (default: `awva_render` on the PATH). An engine on another branch: build `awva_render` on that branch |
| `AWVA_FIT_WORK` | the work directory: renders, features, fits, packs (one folder per instrument) |
| `AWVA_REFS` | the recordings' cache, filled by `sources fetch` or `sources import` |
| `AWVA_CAPTURES` | optional: engine controls captured from an embedded host (`<input>.ctl`) |
| `AWVA_REC` | the recorded key streams the brass family's existing fit set uses (default `tools/harness/rec`); not test material |
| `AWVA_VERDICTS` | the renders behind the listener verdicts, for the back-test (`awvafit/verdicts.json` gives the layout) |
| `FFMPEG` | ffmpeg, for FLAC and for the decoders that use it (default: on the PATH) |

Run from `tools/fit` (or put it on `PYTHONPATH`): `python -m awvafit <command>`.

## Commands

```
python -m awvafit params trumpet 2                         # an engine's live parameters
python -m awvafit sources list                             # the recordings, their licences, the sets
python -m awvafit sources fetch sax                        # download a set, every file's hash checked
python -m awvafit sources import <dir> sax brass           # or take files already on disk, by hash
python -m awvafit segment <file.aiff> --method runs        # cut a recording into notes
python -m awvafit measure <file.aiff>                      # the standard measures of every note
python -m awvafit loudness <file>...                       # BS.1770 integrated LUFS
python -m awvafit stimuli out/notes.csv --notes 62,67      # test notes at the strikes and pressures measured from the datasets
python -m awvafit robust flute 0 59 98                     # every note at three pressures must speak
python -m awvafit backtest --require attack,jtfs           # the distances on the listener verdict pairs
python -m awvafit backtest --goodsounds                    # which distances separate Good-sounds' good takes from bad
python -m awvafit sources fetch choralebricks              # a Zenodo archive: download, check, extract
python -m awvafit phraseset list                           # the phrase sets: legato lines and swells from recordings
python -m awvafit phraseset score --family sax --limit 20  # render each line, measure its note changes against the recording
python -m awvafit pack <config.json> <out> --renderer v2=<awva_render> --renderer v3=<awva_render>

python -m awvafit brassfamily refs | score <inst> | finish <inst> --preset | fit <inst> <run> | pitch <inst> | bake <inst> <final.json>
python -m awvafit sax refs | keys | score <p> | fit <p> <run> [--chroma] | chroma <p> | levelmatch <p> | bake <hdr> P=best.json:gain ...
python -m awvafit flute refs | score <inst> | fit <inst> <run> | robust <p> | rises <p>
python -m awvafit clarinet refs | score <p> [fit|hold] | fit <p> <run>
```

`awva_render` on its own: `--engine E --preset P` with one input:
- `--rec keys.csv`
- `--ctl controls.ctl`
- `--midi file.mid` (MIDI 1.0 or MPE, as MIDI 1.0 in UMP)
- `--perf name`

Its options:
- `--set name=value`, `--transpose T`, `--notes n1,n2,...`;
- `--settle S`, `--tail S`, `--seconds S`, `--rate HZ`, `--nv N`;
- `--out` (float32), `--wav`, `--ctl-out` (the controls the engine got), `--manifest` (JSON: engine, preset, parameters, input identity, settings, outputs, hash);
- `--params` lists the live parameters.

The block is always 32 samples. Renders are deterministic: the engines seed their noise in `init()`, so the manifest's hash identifies the render.

## The package

| module | what |
|---|---|
| `render` | `awva_render` from Python: `Renderer.render(...)`, `.params(...)`; the `.ctl` layout |
| `keys`, `controls`, `phrases`, `stimuli` | the inputs:<br>- key streams as a keyboard's scanner reports them, and the default key layout;<br>- engine-level controls and the note events in them;<br>- the standard expression phrases and the listening phrases;<br>- test notes at the strikes and pressures measured from the datasets' phrase streams |
| `sources`, `sources.json`, `archives.json` | the recordings: source, licence, citation, size and SHA-256 per file, and the Zenodo archives (SHA-256, members checked by CRC-32). `fetch`, `import`, `verify`. Permissive sources only (URMP: CC0, downloaded by hand), plus Bach10 for fitting and evaluation only |
| `labels`, `segment`, `audio` | the recordings' file names (Iowa, MTG, VSCO), and cutting them into notes (the lanes' five methods). AIFF and WAV are read here; FLAC through soundfile or ffmpeg |
| `features` | `frames`: frame features of a render on its controls.<br>`notes`: the lanes' three per-note analyses, exactly.<br>`measures`: harmonics, HNR, attack, sustain pitch, settle time, brightness, tilt, dynamics.<br>`loudness`: BS.1770.<br>`ranking`: the distances tested against listener verdicts |
| `objective`, `terms` | the objective framework (terms, scales, weights from the config); the frame-wise and note-wise comparisons |
| `phraseset`, `goodsounds` | phrase sets from recordings (`awvafit-phrases/1`): rendering each line, the note changes' dip, glide and timing, the phrase distance; the back-test on Good-sounds' good/bad labels |
| `verdicts`, `verdicts.json` | the back-test: every distance on the pairs listeners judged |
| `constraints` | user-set limits a recording can't show (e.g. a maximum rise time per strike), as penalties |
| `fit`, `robust`, `bake`, `pack` | CMA-ES with checkpoints and hold-out; every note at three pressures speaks; fitted values into the headers; listening packs |
| `instruments/` | `brassfamily`, `sax`, `flute`, `clarinet` |

`regress/` holds the regression proof, one script per lane (docs/fitting.md has the numbers), and the registry's maintenance script.

## Dependencies and their licences

| package | licence | for |
|---|---|---|
| numpy, scipy | BSD-3-Clause | everything |
| cma | BSD-3-Clause | the fit driver |
| soundfile | BSD-3-Clause; its wheels bundle libsndfile (LGPL-2.1, loaded dynamically) | FLAC (optional: ffmpeg does the same) |
| torch, kymatio (main branch, pinned) | BSD-3-Clause | the JTFS distance (optional) |
| ffmpeg | LGPL/GPL, a separate program, not linked | decoding where a config uses it (the sax's reference decode) |

## Tests

```
AWVA_RENDER=build/sdk/awva_render .venv-fit/bin/python -m unittest discover -s tools/fit/tests -v
```

They run on a synthetic reference (`tests/synth.py`: generated tones, never recorded), so CI runs them on Linux, macOS and Windows without a download. With `AWVA_RENDER` set they also check the renderer:
- the inputs, the controls round trip, the transposition, the note list, the manifest;
- a frame-wise comparison;
- a CMA-ES fit that finds a filter cutoff again;
- a user constraint;
- a listening pack.
