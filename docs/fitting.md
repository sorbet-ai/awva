<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Fitting engines to recordings

How awva's physical-model presets are fitted, and how to measure a build against recordings or against another engine. The tools are in [`tools/fit`](../tools/fit/README.md): `awva_render` and the Python package `awvafit`.

**Sound is judged by ear.** These tools measure. A distance only earns a place in an objective after it has ranked the listener verdicts right (the back-test, below).

## The workflow

```
fetch -> segment -> features -> objective -> fit -> bake -> listen
```

1. **Fetch** the recordings into the cache:
   - `python -m awvafit sources fetch <set>` downloads them;
   - `sources import <dir>` takes copies already on disk.
   Every file is checked against its SHA-256 in `awvafit/sources.json`.
2. **Segment** them into notes. A recording is either a chromatic scale with silence between the notes (Iowa MIS) or one note per file (MTG, VSCO). `segment.notes(x, sr, method)` gives each note's onset and release:
   - the method is the instrument lane's (`plateau`, `runs`, `envelope`, `peak`, `single`);
   - a note's label (pitch, dynamic) comes from the source's file names (`labels`).
3. **Features:** measure every recorded note, and the engine's render of the same note at the same dynamic.
   - `features.notes` holds the per-note analysis the lane fitted with;
   - `features.measures` holds the standard measures (below);
   - `features.frames` measures frame by frame when two engines play identical controls.
4. **Objective:** `objective.Objective` sums the components the comparison measured, each over its scale (how much is clearly audible) and times its weight:
   - `total = Σ weight × component / scale`, in the config's order, averaged over the inputs first;
   - the instrument's config gives the terms, scales and weights.
5. **Fit:** `fit.Fit` runs CMA-ES (`cma`, BSD-3) over the engine's live parameters in a normalised space:
   - log-spaced for the positive ones, inside the engine's own ranges;
   - a checkpoint every generation (a run resumes), `best.json`, a log;
   - the hold-out set is scored at the end;
   - `robust.check` (every in-range note at three pressures must speak) and `constraints` (limits a listener sets) add penalties or act as gates.
6. **Bake** the result into the engine's preset tables (`bake`): by live parameter name, through the header's parameter table, at the value the fit rendered.
7. **Listen:** `pack` writes a listening pack, and a listener judges by ear.
   - Every engine version plays the same phrases;
   - the references come first (another engine's preset and recordings), the recordings loudness-matched (BS.1770) to the renders;
   - `index.md` says what each file is and its loudness.

## The licence rules

- **No recording is in this repository.** `sources.json` holds where each file comes from, its licence, the citation, its size and hash; the files are fetched into `$AWVA_REFS`, with `LICENSES.md` written next to them.
- **Permissive sources only.** A source whose terms restrict use is listed with `permissive: false` and is refused: the Philharmonia samples (free in music, not to be redistributed as samples).
- **Attribution where the licence asks for it.** MTG Solo Saxophones is CC BY 4.0: credit "MTG Solo Saxophones by kinwie, from the MTG (Universitat Pompeu Fabra) Freesound packs, CC BY 4.0" wherever the recordings, or works made from them, are shared. Listening packs carry the citation in their index.
- **No path, user name or machine-specific folder in a committed file.** Everything comes from the environment.

| source | licence | used for |
|---|---|---|
| University of Iowa Musical Instrument Samples (Lawrence Fritts) | "freely available ... may be downloaded and used for any projects, without restrictions" | sax, flute, alto flute, clarinets, oboe, bassoon, trumpet, trombone, horn, tuba |
| MTG Solo Saxophones (kinwie, from the MTG Freesound packs) | CC BY 4.0 | soprano, alto, tenor, baritone sax |
| VS Chamber Orchestra 2: Community Edition (Versilian Studios) | CC0 1.0 | piccolo |

## Rendering

`awva_render` renders any engine and preset from:
- a key stream (`--rec`, through the key path);
- engine controls (`--ctl`);
- a Standard MIDI File, MIDI 1.0 or MPE (`--midi`, up-converted to MIDI 2.0);
- a scripted performance (`--perf`).

What the fits add:
- `--set name=value`: a live parameter, written with its rule (clamped);
- `--settle S`: seconds of silence before the input, not written, so the parameter glide has arrived;
- `--transpose`: an instrument's register;
- `--notes`: every new press takes the next note of a list, so one key stream plays any notes;
- `--ctl-out`: the controls the engine got, which a reference render passes on to the model;
- `--manifest`: a JSON record of the render.

A render without the new options is bit for bit what it was before.

The block is 32 samples and the rate is set with `--rate`. There is no seed to set: the engines seed their noise in `init()`, so a render is the same on every run, and its FNV-1a hash in the manifest identifies it. The fit driver's seed makes CMA-ES repeatable.

## What is measured

The standard per-note measures (`features.measures`), the same code for a recording and a render:

| measure | definition |
|---|---|
| harmonic amplitudes | the energy of each harmonic's spectral lobe, dB re the harmonic total: the spectral envelope at the harmonics; `envelope` gives it in log bands |
| harmonic-to-noise ratio | harmonic lobes against the median floor between harmonics (0.3–0.7 of the way), overall and per band |
| attack | latency: from the key to the first sound (40 dB under the sustain). Rise: from −30 dB to −3 dB re the sustain. Overshoot: the first 200 ms's peak re the sustain |
| sustained pitch | a 10 ms pitch track (window-corrected normalised ACF near the note's period) and its median in cents from 250 ms on |
| settle time | from the onset until the pitch stays within 10 cents of that median |
| brightness, tilt | energy above 1.5 kHz re the total; the slope of the harmonic amplitudes over log2 frequency (dB/octave, 1.5 f0 to 8 kHz) |
| loudness | ITU-R BS.1770-4 integrated LUFS (`features.loudness`), and the gain that matches two signals |
| dynamics curves | each dynamic's level re the loudest at the same note |

The lanes' own analyses (`features.notes`) are kept exactly, so their numbers reproduce:
- `harmonic_sum`: the sax;
- `windowed`: the flute;
- `lobes`: the clarinet.

## Which distances to trust

The research lane tested candidate distances against listener verdicts, by ear, on real builds (the trumpet's Track B iteration 1, approved; its stage 2 builds and the old trumpet model, rejected; the clarinet, double reed and flute). `python -m awvafit backtest` re-runs that test from `awvafit/verdicts.json`.

**Test at the strikes a player uses.** The trumpet's stage-2 regression (tagged slow-attack) shows only at soft strikes:
- at strikes of 0.07–0.12 the rejected builds start 11–12 dB weaker in the first 80 ms;
- at 0.3 and above the builds are the same.

A set of medium and hard strikes misses it whatever the metric. `stimuli` draws test notes from the takes in `tools/harness/rec`: their held notes strike at a median of about 0.3, a quarter under 0.21. `stimuli.strike_set` is the soft-strike set (0.07, 0.12, 0.2, 0.3).

| distance | use | the back-test |
|---|---|---|
| `attack`: the 1 ms attack envelope, the first 150 ms in dB re the sustain, mean \|difference\| | **ranker** at soft strikes, or against an approved reference | soft strikes: 11 and 10 of 12 notes against the approved hybrid, 12 and 12 against the Iowa trumpet. At medium and hard strikes against recordings it prefers the rejected old trumpet (1 of 12) |
| `jtfs`: joint time-frequency scattering (kymatio; J=12, J_fr=3, Q=(8, 2), global), RMS of log coefficients | **ranker**: timbre against real recordings | right on all four trumpet pairs against the Iowa trumpet (10, 8, 9, 10 of 12) and the three against the hybrid; small margins on stage 2 |
| `mrstft`: multi-resolution STFT | secondary | right on the old-trumpet pairs, blind to stage 2 |
| `noise`: noise per pitch period: `nhr_hp` (the residual above 1.5 kHz re the note) and `psm` (how pulsed it is within the period) | **diagnostic** per dynamic, gated on the recording's noise floor; reported, never scored | the real trumpet's pp noise is pulsed (psm 0.46) where the approved build's is steady (0.07). The Iowa clarinet's and flute's pp notes sit at the room's floor (−37 to −43 dB re the note): there the residual is hiss |
| `lanes`: the lanes' frame-wise objective terms on a note | **not a ranker against recordings** | against the Iowa trumpet they prefer the rejected old trumpet (closer on 10–11 of 12 notes); against the hybrid they rank it last |
| `atk_noise`: HF noise in the first 40 ms re the sustain's | **not a ranker** | runs opposite to the verdicts: the approved trumpets have an HF burst at pp attacks that the real trumpet lacks |
| an audio-codec embedding distance (DAC) | **not a ranker against recordings** (not implemented) | 5–8 of 12 against recordings |

The lesson behind the last rows: Track B worked because of its reference (an approved sound), not because of its features. A lane objective is only as good as the reference it is pointed at. No metric orders the woodwinds' rejections (double reed v1, tagged janky; clarinet v1, tagged breathy-attack) on isolated sustained notes; the next places to look are phrases, transitions and the response to expression.

**The back-test is the gate.** For each pair:
- the approved build, the rejected one, a reference (recordings or an approved build) and a stimulus set;
- per note, whether the approved build is closer;
- the effect in natural-variation units: the distance between a recording and the recording a semitone up.

The gate rules:
- a ranker passes when it wins on more than half the notes of every pair in its declared use;
- a new metric must be right on every pair;
- `--require attack,jtfs` exits 1 unless both pass.

Add a metric to an objective only after it passes, and add new verdicts to `verdicts.json` as listeners give them. The renders behind the verdicts live outside the repository (`$AWVA_VERDICTS`).

**User-set constraints** (`constraints`) cover what recordings can't show:
- The Iowa flute rises as slowly as flute v1, which listening tagged slow-attack, so a distance to the recordings would keep the slowness.
- A constraint states the wish directly. The flute's config has two: a rise from the key of at most 50 ms at strikes of 0.4 and up, and at most 120 ms below.
- Its penalty is added to a candidate's total. A listener sets the limits; a fit never moves them.

## The regression proof

The shared core reproduces the lanes' published numbers exactly. Each lane's engine was built from its own commit with this `awva_render`; the instruments' configs ran on the shared modules. Scripts: `tools/fit/regress/`.

Every fit settles its renders for 1 s (the overrides' glide arrives). For the flute and the clarinet, whose lanes rendered without settling, the numbers below are the baseline under that rule and are the lanes' own: settling leaves the shipped presets' renders bit-identical (`--lane` reproduces the lanes' renderers exactly, to the same floats).

**The brass family**, on the captured controls (`finish --preset`):

| | v2 fit | v2 hold-out | v3 fit | v3 hold-out |
|---|---|---|---|---|
| flugelhorn | 16.697516 (16.698) | 8.626784 (8.627) | 16.466955 (16.467) | 8.591543 (8.592) |
| trombone | 24.930568 (24.931) | 18.195306 (18.195) | 24.415060 (24.415) | 17.505968 (17.506) |
| horn | 19.466482 (19.466) | 10.300300 (10.300) | 19.229011 (19.229) | 10.120410 (10.120) |
| euphonium | 18.645961 (18.646) | 10.088067 (10.088) | 18.458225 (18.458) | 9.031255 (9.031) |
| tuba | 20.674055 (20.674) | 8.507889 (8.508) | 19.830986 (19.831) | 8.629539 (8.630) |

In brackets, the published values. The 64 reference renders and their controls are bit for bit the lane's.

**The sax**, the shipped presets on their fit sets (and the hold-out on MTG):
- soprano 13.040649718500847 (published 13.0; hold-out 11.588715194450034);
- alto 15.053499621442482 (15.1; 12.889030046101531);
- tenor 9.3410680889243 (9.34);
- baritone 9.689874069320261 (9.69).

The lane's own `sobj.py` and `sax_replay` give the same floats to the last digit. The 487 reference notes' features, the key templates, and the renders are bit for bit the lane's.

**The flute**, on every recorded note:
- flute 12.543965632035073;
- alto flute 12.063942491596858;
- piccolo 6.794064050243116 (published 6.79).

The lane's `evalm.py` with its own renderer gives the same floats. The 241 reference notes are the same, from this toolkit's AIFF and WAV readers.

**The clarinet**, the shipped presets:

| | fit | hold-out |
|---|---|---|
| Bb | 19.777029717640914 | 20.57854446589729 |
| bass | 18.069460681448756 | 16.59141789140625 |

The lane's `fit.evaluate` with `clar_render` gives the same floats. Its control synthesis is ported float for float (`clarinet.job_controls`), and the renders are bit for bit `clar_render`'s.

**The back-test** reproduces the research lane's Step 0 tables: every win count and effect of the main and soft sets, and the noise-per-period table.

## Migrating an instrument lane

Each lane's tools shrink to its config module on the shared core. The owners migrate their branches; these notes say what maps where.

**Brass family** (`tools/brassfamily` → `awvafit/instruments/brassfamily.py`, done there):
- `bf_replay` → `awva_render`:
  - references: `--rec --transpose T --settle 1 --tail 1.5 --ctl-out`, or with captures `--ctl --settle 1`;
  - the trumpet engine on the reference's controls: `--ctl --set ... --settle 1`;
  - `--list` → `--params`.
- Environment: `BF_WORK` → `AWVA_FIT_WORK` (its `brassfamily/` folder), `BF_REPLAY` → `AWVA_RENDER`, `BF_CAPTURES` → `AWVA_CAPTURES`, `BF_REC` → `AWVA_REC`. `BF_INST`, `BF_TILT` and `BF_MEL_LO` become the instrument's entry in `INST`: the tilt terms through `objective(inst)`, the mel floor through `analysis(inst)`.
- `keys.py` → `awvafit.phrases` (byte for byte the same CSVs); `feats.py`, `tilt.py` → `features.frames`; `objective.py` → `terms.framewise` and `OBJECTIVE`.
- `evalset`, `finish`, `fit`, `pitch`, `bake` → the module's functions and `python -m awvafit brassfamily ...`.
- `fetch_references.py` → `sources fetch brass`; `sanity.py` → `segment` (`peak`) with `features.measures`.

**Sax** (`tools/sax` → `instruments/sax.py`):
- `sax_replay --job keys notes out ctl` → `awva_render --rec t_<set>.csv --notes ... --settle 1 --tail 1.5 --ctl-out`.
- `SAX_WORK` → `AWVA_FIT_WORK` (`sax/`); `SAX_CHROMA=1` → `fit ... --chroma`.
- `fetch_references.py` → `sources fetch sax`. The cache keeps the servers' paths: `uiowa_mis/Woodwinds/altosaxophone/...`, `mtg_solosax/MTG Solo Saxophones/...`.
- `sfeat` → `features.notes.harmonic_sum`; `sobj` → `terms.notewise.compare` and `OBJECTIVE`.
- `refs`, `keys`, `fit`, `chroma`, `levelmatch`, `finalize` → the module's functions.

**Flute** (`tools/flute` → `instruments/flute.py`):
- `replay` (the header compiled in) → `awva_render --ctl`. For the shipped presets the renders are the same.
- **Overrides now settle, as in every fit.** The lane's renderer wrote `--set` overrides without their glide (`ps = p`); `awva_render` writes them with the parameter's rule and glide, as every host does. The flute renders after a second of silence (`flute.SETTLE = 1.0`), as the brass and sax fits do, so the glide has arrived when the notes start (1 s and 3 s give the same render).
  - For the shipped presets settling changes nothing: an idle engine doesn't advance, and the renders are bit-identical with or without it. The baseline numbers are the lane's.
  - A fit's candidates with overrides render as if the values were preset, which the lane's snapping approximated.
  - `regress/flute.py --lane` renders without settling, as the lane did.
- `ref_segment`, `vsco_segment`, `ref_feats`, `fetch_samples` → `flute.refs` and `sources fetch flute`; `feat` → `features.notes.windowed`; `objective` → `terms.notewise.flute_combine` and `OBJECTIVE` (`PICCOLO` for the piccolo's weights).
- `evalm`, `fit`, `fit_picc` → `evaluate` and `fit`; the piccolo's penalties are in `Scorer`.
- `takes`, `robust` → `rises` and `robust`; the attack targets are now `CONSTRAINTS` (`constraints.MaxRise`).
- The exploratory probes (`probe`, `sweep`, `sweep2`, `obtest`, `fit_atk`) are not ported: `robust.check`, `constraints` and `measures` cover them.

**Clarinet** (`tools/clarinet` → `instruments/clarinet.py`):
- `clar_render` (synthetic controls in C++, the header compiled in) → `clarinet.job_controls` and `awva_render --ctl --nv 1`, bit for bit.
- Its overrides were snapped (`ps = p`). Now they settle, as the flute's do (`clarinet.SETTLE = 1.0`); the shipped presets' renders are unchanged by it, and `regress/clarinet.py --lane` renders without settling.
- `refs`, `notefeats` → `clarinet.refs` and `features.notes.lobes`; `objective` → `terms.notewise.clarinet_compare` and `OBJECTIVE`.
- `fit`, `evalp`, `pernote` → `fit`, `evaluate` and `python -m awvafit clarinet score`.
- `imp` and `probe` (the bore's impedance and the mouthpiece through the engine's test hooks) measure engine internals, not renders: they stay with the lane.

**Trumpet Track B** (`tools/trumpet` on main): the bore design, the calibration and the table generators build the engine's tables; they don't render or compare, so nothing moves. Its listening and comparison steps use `awva_render` and `pack`.
