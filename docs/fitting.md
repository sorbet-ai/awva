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
6. **Bake** the result into the engine's presets (`bake`): by live parameter name, at the value the fit rendered: into its preset tables through the header's parameter table, or, for an engine whose presets are files, into its preset file (`bake_preset_file`; then `cmake -P tools/presets_inc.cmake`).
7. **Listen:** `pack` writes a listening pack, and a listener judges by ear.
   - Every engine version plays the same phrases;
   - the references come first (another engine's preset and recordings), the recordings loudness-matched (BS.1770) to the renders;
   - `index.md` says what each file is and its loudness.

## The licence rules

- **No recording is in this repository.** `sources.json` holds where each file comes from, its licence, the citation, its size and hash; the files are fetched into `$AWVA_REFS`, with `LICENSES.md` written next to them.
- **Permissive sources only.** A source whose terms restrict use is listed with `permissive: false` and is refused: the Philharmonia samples (free in music, not to be redistributed as samples).
- **One recorded exception, `use: fit-eval`.** Bach10 states no licence. Permission has been asked of their authors, and meanwhile the owner allows them for fitting and evaluation only, never redistributed. They are never fetched (they have no public download either). Files already on disk may be imported and read, and every command that reads them prints the restriction.
- **Archives are pinned whole.** ChoraleBricks and Good-sounds are Zenodo archives: `archives.json` holds each one's URL, size, SHA-256 and Zenodo's MD5. Every extracted file is checked against the CRC-32 the archive records for it (`sources verify`), and a file not extracted is read from the archive.
- **Attribution where the licence asks for it.** MTG Solo Saxophones is CC BY 4.0: credit "MTG Solo Saxophones by kinwie, from the MTG (Universitat Pompeu Fabra) Freesound packs, CC BY 4.0" wherever the recordings, or works made from them, are shared. Listening packs carry the citation in their index.
- **No path, user name or machine-specific folder in a committed file.** Everything comes from the environment.

| source | licence | used for |
|---|---|---|
| University of Iowa Musical Instrument Samples (Lawrence Fritts) | "freely available ... may be downloaded and used for any projects, without restrictions" | sax, flute, alto flute, clarinets, oboe, bassoon, trumpet, trombone, horn, tuba |
| MTG Solo Saxophones (kinwie, from the MTG Freesound packs) | CC BY 4.0 | soprano, alto, tenor, baritone sax |
| VS Chamber Orchestra 2: Community Edition (Versilian Studios) | CC0 1.0 | piccolo |
| ChoraleBricks 1.1.0 (Balke, Berndt, Mueller; Zenodo 15081740) | CC BY 4.0 (the sheet music excluded) | phrase sets: legato lines and swells, 13 wind instruments |
| Good-sounds (MTG, Universitat Pompeu Fabra; Zenodo 820937) | CC BY 4.0 | the good/bad back-test |
| URMP (University of Rochester; Dryad doi:10.5061/dryad.ng3r749) | CC0 1.0 (scholarly citation asked) | phrase sets, including bowed strings; downloaded by hand (Dryad gates it), checked by its MD5 |
| Bach10 (University of Rochester) | none stated: fitting and evaluation only, pending the authors | phrase sets, when on disk |

## Rendering

`awva_render` renders any engine and preset from:
- a key stream (`--rec`, through the key path);
- engine controls (`--ctl`);
- a Standard MIDI File, MIDI 1.0 or MPE (`--midi`, as MIDI 1.0 in UMP);
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

A set of medium and hard strikes misses it whatever the metric. `stimuli` draws test notes from real playing: the strikes and pressures of the phrase sets' key streams, whose control is inferred from the recordings (ChoraleBricks: the held notes' median strike runs from 0.32 for the flute to 0.91 for the clarinet; the builder anchors inferred pressure at 0.4, so the pressure spread is relative). `stimuli.strike_set` is the soft-strike set (0.07, 0.12, 0.2, 0.3). Proper recorded takes on a pressure-sensing controller will come later; the takes in `tools/harness/rec` are not test material.

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

## Phrase sets: what isolated notes can't show

The step-0 back-test found the woodwinds' rejections invisible on isolated sustained notes. Phrases show the note changes. A phrase set (`awvafit.phraseset`) takes legato lines and swells from recordings and gives each as a key stream that any engine plays, with the recording to compare it with.

- **The sets:** ChoraleBricks now (2,072 items: brass, sax, clarinet, flute, double reed); URMP and Bach10 under the same format once they are on disk. They are built by the research lane's `build_phrases.py` from each recording's notes and f0 annotations. The control is inferred, not measured: pressure follows the recording's level, a phrase's first note strikes as fast as its recorded attack rises, connected notes press softly before the last key lets go.
- **The manifest** (`awvafit-phrases/1`, at `$AWVA_REFS/phrases/<set>/manifest.json`), per item:
  - `engine`, `preset` (`awva_render`'s names) and `transpose` (the keys are pressed at the note minus it);
  - `stream`, the key stream, relative to the manifest;
  - `audio0_s`, the stream-clock time where a render's audio starts (the stream's first row; `awva_render --rec` starts there);
  - `source` and `reference`: the recording (`path` relative to the source's root, `start_s` the recording's time of stream time 0);
  - `notes` (onset, offset, MIDI note, cents, level), the builder's `transitions` and, for swells, `swell`.
  The reader also takes the builder's first, unversioned form.
- **Playing one:** `python -m awvafit phraseset render --id <item>` runs `awva_render --rec <stream> --transpose T --settle 1 --tail 1`, on the item's engine and preset or on `--engine/--preset`. Every engine plays the same line as the recording.
- **Measuring:** `phraseset score` renders each item and measures each note change on the render and on the recording (resampled to 48 kHz), with the same code:

| measure | definition |
|---|---|
| level dip | the 10 ms level's dip at the change, re the mean of the two notes' levels around it (dB), and its width below −3 dB (ms) |
| level step | from the first note's level to the second's (dB) |
| glide | the pitch's 10 → 90 % of the interval (ms), the overshoot past the target (cents), and the shape: the glide as a fraction of the interval every 10 ms from −60 to +120 ms |
| onset timing | when the second note's pitch arrives (within 50 cents, periodic), on the stream clock |

  The phrase distance (`PHRASE_OBJECTIVE`) takes, over an item's note changes, the medians of: the glide shapes' RMS difference (% of the interval), the glide times', the arrival times', the dips' and the steps' absolute differences. On the recordings the toolkit's dips agree with the builder's to a median 0.3 dB and its glide times to 10 ms (its own pitch track against the annotated f0).
- **Not yet trusted.** These are new distances. Like any, they enter an objective only after a back-test: listener verdicts on phrases are what would test them.
- **The glide time is fragile.** Where the pitch drops out through a change, its 10 → 90 % time jumps: the builder's own glide times are bimodal (the flute's median 17 ms, its upper quartile 145 ms). The glide shape, which skips the frames without a pitch, is the steadier measure.
- **A first baseline** (`phraseset score --kind legato_line --limit 20`, each family's shipped presets on its lane's engine build; medians over the lines):

| family | lines (note changes) | glide shape | glide | onset | dip | dip width | step | total |
|---|---|---|---|---|---|---|---|---|
| sax | 20 (126) | 43 % | 25 ms | 30 ms | 2.9 dB | 30 ms | 5.9 dB | 6.92 |
| clarinet | 20 (122) | 34 % | 10 ms | 20 ms | 11.0 dB | 20 ms | 3.6 dB | 7.47 |
| flute | 20 (131) | 40 % | 58 ms | 30 ms | 4.7 dB | 38 ms | 3.8 dB | 9.46 |
| brass (euphonium, trombone, ...) | 20 (128) | 33 % | 18 ms | 20 ms | 5.1 dB | 28 ms | 3.4 dB | 6.59 |

  One reading for the lanes, a measurement and not a verdict: the clarinet dips deeper at its legato changes than its recordings do (median 25.6 against 18.0 dB, over 51 changes).

## Calibrated streams: each engine plays the recording's control

A phrase set's default stream infers the control once, for every engine. Three of its rules mislead an engine:

- **Strike:** from the acoustic rise (strike = K π P / (2 rise), K fitted on finger presses). An acoustic attack rises in 20–30 ms, so most tongued notes strike at 0.7–1.0. On a ChoraleBricks trumpet line, the recording's attacks overshoot the sustain by a median +0.7 dB, the renders by +8 and +11 dB (up to +20).
- **Pressure:** P = 0.4 · 10^((L − Lmed)/40). That is right only for an engine whose level grows 2 dB per dB of pressure, and the anchor 0.4 ignores where the engine speaks and how bright it is there.
- **Legato:** a change counts as connected only where the level dips under 6 dB, which real slurs rarely do. Most slurs come out as one key let go and the next pressed 30–40 ms later.

`awvafit.control` builds the stream per engine from the recording (`python -m awvafit control <item> --engine E --preset P --out s.csv`; `python -m awvafit strike` calibrates only the strikes). The render function is pluggable: `awva_render --rec` here, or any other player of key streams.

1. **Pressure sweep.** Isolated notes at three of the phrase's pitches, 12 pressures from 0.04 to 1, strike 0.2. Each note gives:
   - its sustain level;
   - its brightness (the spectral centroid over f0);
   - its periodicity (the autocorrelation peak at the period, median over 40 ms frames).

   The oscillation threshold is the lowest pressure from which periodicity stays at 0.9 or more. The floor is 1.25 × the threshold.
2. **Anchor.** The pressure whose brightness matches the recording's median note, at least the floor. It is refined once in the phrase itself, because an isolated note is not as bright as the same note in a line.
3. **Pressure.** Every 10 ms of a note maps the recording's level (relative to the phrase's median note) to the pressure at which the engine's own level curve reaches the anchor's level plus that difference. It is clipped to between the floor and 1. Per-note dynamics and the motion within a note follow the recording through the engine's curve. Within a note, the pressure follows the recording's level under four limits:
   - the level is smoothed over 70 ms;
   - it goes at most 12 dB under the note's own level;
   - the pressure moves at most 1.5 per second, which keeps the scanner's velocity at 0.05 or less;
   - the floor is also held where the engine's level comes within 30 dB of its loudest.

   The key path plays a velocity that rises inside a held key as a second strike: following the raw 10 ms level made both clarinets click.
4. **Legato.** At a connected change of pitch (the next onset less than 0.12 s after the note's end), the next key goes down at its onset on another key, and the last one lets go 30 ms later (`--overlap`). The breath is held. Repeated pitches and real gaps re-tongue, released 20 ms early. From 50 ms before a slurred note's end, the old key's pressure follows the recording's dip down as fast as it falls: a falling pressure is a negative velocity, never a strike.
5. **Strike.** A strike sweep on isolated notes gives a curve of attack overshoot against strike. The overshoot is the attack's peak, −10 to +100 ms around the onset, over the sustain's 99th-percentile peak.
   - Each note's pressure rises over its own ramp, set apart from the strike (`keys.Stream.press(ramp=)`: the velocity over the rise is scaled to peak at the strike). The ramp starts at the recording's onset rise (10 → 90 % of the 5 ms envelope) and is refined in the phrase until the render's rise matches it. A recording that speaks by a quick ramp with no strong attack gets a soft strike on a quick ramp, not a hard strike.
   - Each note gets the strike whose overshoot matches the recording's own for that note (the manifest's `notes[].attack_db`, the same measure).
   - Refinement passes in the phrase follow, and each note keeps its best pass.
   - Slurred notes are calibrated too, so a tongued legato re-attacks on the held breath as far as the recording does.
   - Only the rises are rewritten (`strike.restrike`): each hold keeps the original pressure at the same clock time, and each note keeps its end.

Measured, on the audition builds' dry output:

- **Trumpet line:** median attack overshoot +7.8 and +10.5 dB before, +1.3 dB after, against the recording's +0.7. The median per-note error is 0.9 dB.
- **Trombone line:**
  - The level error falls from 5.0 to 2.5 dB RMS, and the per-note level range from 14.6 to 8.7 dB (the recording's: 6.8 dB).
  - The brightness rises from 2.15 to 2.6, against the recording's 2.96.
  - Both builds speak periodically at every pressure swept, so their threshold is below 0.04, and the default stream left them dark rather than near threshold.
- **Across nine instruments:** brass dynamics and attacks come within about 1 dB of the recordings, where the default streams were 8–20 dB too hard. The oboe's slur gaps of 55–68 dB come down to 9–10 dB, against the recording's 18.

## The listening gate: nothing reaches a listener unless the measures say it is better

`python -m awvafit gate --inst I --candidate X --baseline Y` decides, before anyone listens, whether a new build is closer to the recordings than the one before it.

- **Builds:** `<engine>:<preset>` (through `awva_render --rec`), or `cmd[label]:<command>` for any other player of key streams. The command gets `{csv}`, `{out}`, `{octave}`, `{transpose}` and `{legato}`, and writes the dry output to `{out}`.
- **Voicing:** the recordings are one instrument playing one line, so by default both builds play the keys in mono legato (`awva_render --rec --legato`): one voice, and a key pressed while one is held slurs to it, as a wind player's fingers do on one breath. With a voice per key, the old note's release would still sound over each slur's onset and swamp the onset measures. A build that can't play mono legato runs with `--voicing poly`, and its baseline does too. The onsets are then measured only where no old voice overlaps them: the first note, and notes pressed 100 ms or more after the last key let go.
- **Held out:** a third of each set's performances of the instrument (`awvafit/gate_set.json`, `gate.held_out_tracks()`), the lowest-pitched one among them. URMP files one recording under several pieces, so its tracks are grouped into performances by their level envelope's fingerprint. **No fit may use any phrase of these tracks.** The gate enforces it: every run names what the candidate was fitted on (`--fit-set <manifest>`, or `--no-fit`), and refuses, naming the tracks, a fit set that uses a held-out recording. Build fit sets with `awvafit.gate_set.fit_tracks_allowed(items)`, which leaves those recordings out.
- **Phrases:** from those performances, up to 8 ChoraleBricks and 8 URMP legato lines per instrument, a quarter of each from the low register, the rest in SHA-1 order, with the listening kits' phrases left out. Each phrase is played through a stream calibrated on that very build (`awvafit.control`), so the comparison is the engine's.
- **The range check** (`awvafit.range_check`): every note of the instrument's practical range, held soft, mid and loud and pressed short, each an isolated press. Each must:
  - speak (periodic, within 25 dB of the median note);
  - sit on its partial: YIN's pitch, searched a twelfth either side, within ±15 c;
  - not flip: off its median by more than 50 c for at most 5 % of the sustain, and never 30 ms in a row;
  - start within 150 ms.

  A candidate that breaks a note its baseline plays fails, whatever its phrases say. A failing note the baseline shares is reported ("both fail ..."), not counted as a regression.
- **Measures** (`awvafit.gate`, the clarinet and double reed lanes' analyses together):

| group | measures |
|---|---|
| dynamics | per-note level re the phrase median; the phrase's level range |
| harmonics | h1–h10 re the total in the sustain's middle third; their spread across notes; their change over the note |
| noise | the aperiodic part (a one-period comb): its level re the tone, its band shape, its pulsing within the period above 1.5 kHz |
| onset | the rise (10 → 90 %); the overshoot over the sustain; the noise before the tone speaks |
| slurs | the dip at each connected change and its recovery |
| texture | band-envelope modulation in 0.5–8 kHz carriers at 4–20, 20–80 and 80–300 Hz |

- **Floors:** every measure's floor is the recording's own spread, either between its notes of the same pitch or between the phrase's halves. A measure's score is its distance from the recording in floors. A group's score is the mean of its measures, and the total is the mean of the groups.
- **Verdicts:**
  - *fail* first, if the candidate breaks a note the baseline plays;
  - *identical*: the two builds are within a floor of each other on every measure, so there is nothing to hear and no kit is made;
  - *pass*: the candidate's total is lower, it is better on most groups, and it is worse on no group by more than a floor;
  - *fail*: anything else.
- **Fit on what the gate scores:** `awvafit.gate.objective(build, fit_set, voicing='mono')` scores one build exactly as the gate does: the same features, R's floors, per-measure medians over the phrases, group means and their mean. It returns `dict(total, groups, measures, phrases)` in floors, lower closer.
  - `build` is a `factory(item) -> render(csv)`, or a gate spec.
  - `fit_set` is phraseset Items or a fit-set manifest. A fit set that uses any held-out recording is refused.
  - By default every call calibrates each phrase on the build it scores, as the gate does, and deletes that calibration when it returns.
  - `streams_frozen=True` with the same `streams={}` dict calibrates each phrase once, on the first build, and reuses it. That is the trap: the calibration fits the build that played it, so a fit scored on its baseline's calibration learns that calibration, not the recordings. The bowed lane's violin fit lowered its own loss this way and then failed the gate. Freeze only to compare builds that share a calibration, and recalibrate before trusting a total.
  - R's features are computed once per process.

  A lane's own objective can disagree with the gate: the double reed v3 fit scored well on its own measures and lost to v2 at the gate. So fit on this.
- **The rule:** a listening kit is made only after a pass, and every lane runs the gate before handing a kit on. The recordings and the renders are high-passed alike, below min(60 Hz, 0.8 × the phrase's lowest note), to remove URMP's rumble.

## The Good-sounds back-test

`python -m awvafit backtest --goodsounds` asks which distances separate Good-sounds' deliberately bad takes from its good ones, for the trumpet, clarinet and flute sessions (the research lane's design, kept):
- **References:** the session's good-sound takes at each semitone. A take's score is its distance to the closest one, so the dynamic doesn't decide.
- **Good comparisons:** the session's good-attack (good-timbre, good-air) takes, which are not references.
- **Per bad class:** AUC = P(score(bad) > score(good)) over the pairs at the same semitone; 1 means the distance always puts the bad take farther, 0.5 is chance.
- **Caveat:** only the attack session compares like with like. The other sessions were recorded on other days than the references, so a distance can separate them by the day rather than the fault; those rows are marked confounded.

The results (`backtest --goodsounds`, every distance; they reproduce the research lane's run exactly). Mean AUC over the 11 clean classes, how many of them reach 0.75, and the mean over every class:

| distance | clean classes: mean AUC | at 0.75 or more | every class |
|---|---|---|---|
| `mrstft` | 0.78 | 7 of 11 | 0.68 |
| `lanes` | 0.75 | 5 of 11 | 0.63 |
| `noise` | 0.74 | 4 of 11 | 0.65 |
| `jtfs` | 0.72 | 5 of 11 | 0.62 |
| `attack` | 0.68 | 4 of 11 | 0.52 |
| `atk_noise` | 0.55 | 2 of 11 | 0.50 |

What it says:
- **Gross faults are easy, subtle ones are not.** The trumpet's airy attacks are separated by every distance (`attack` and `mrstft` 1.00, `jtfs` 0.97); the clarinet's tongue-blocked attack by none (0.54–0.74).
- **The ranks differ from the listener verdicts'.** On expert-labelled single notes, the spectral distances (`mrstft`, the lanes' terms) separate best; on the listener verdicts between engine builds, they don't (the lanes' terms prefer the rejected trumpet). The two tests measure different things: Good-sounds asks whether a distance sees a player's fault in a recording; the verdicts ask whether it orders builds the way a listener does. A distance should pass both before it gates.
- **Attack HF noise is near chance here too,** as it was on the verdicts.

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
