# The musical test performances: sources and licences

The `.ump` files here (UMP text: MIDI 2.0 packets, one a line, in frames at 48 kHz) are derived works: control streams inferred
from the note annotations and the level of recordings. They contain no audio.

## ChoraleBricks (every performance in this folder)

- **Source:** ChoraleBricks 1.1.0, `01_AudioAndAnnotations` (solo wind and brass tracks of chorales, with note
  and f0 annotations). Each file's track and time span are in `index.json` (`reference`,
  `reference_start_s`, `reference_end_s`).
- **Authors:** Stefan Balke, Axel Berndt, Meinard Müller.
- **Publication:** "ChoraleBricks: A Modular Multitrack Dataset for Wind Music Research", Transactions of
  the International Society for Music Information Retrieval (TISMIR), 2025.
- **Data:** Zenodo, doi:10.5281/zenodo.15081740.
- **Licence:** Creative Commons Attribution 4.0 International (CC BY 4.0),
  https://creativecommons.org/licenses/by/4.0/
- **Changes made:** the notes, pitches (the f0 annotation re the track's tuning) and levels of a phrase of
  one track were turned into Note On/Off, Poly Pressure and Per-Note Pitch messages
  (`awvafit.perform`); nothing of the audio is kept.
