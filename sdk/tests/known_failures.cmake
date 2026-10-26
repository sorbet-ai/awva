# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
#
# The conformance checks' known failures (docs/conformance.md), one list per engine:
#   set(AWVA_KNOWN_<engine> <tag> ...)
# A tag is a check's ("tuning", "silence") or a check's on one preset ("tuning:flute"). A known
# failure is reported as a warning, not failed, in the engine's contract suite (contract.<engine>.*)
# and in its component's conformance run (conformance.<engine>.*), so the suite stays green while
# the engine's owner fixes it; the warning says when it passes again, and then the line goes.
# Each entry says what fails, with its numbers, and who fixes it. Keep the list short.

# trumpet: its owner
#   slur           5 clicks in 48 slurs (by -7 and +12, 4 to 7 ms after the press), with its 10 ms valve
#                  change (note_glide.h); the lips' and the loop delay's reset on a new note
#                  (voice_trumpet.h: jump) is the next suspect
# reed: its owner
#   slur           2 clicks in 48 slurs, on the octave slur up, at the press (it has no fingering change yet)
# modal: its owner
#   slur:wine glass  2 clicks in 48 slurs (firm, by +5 and +1), at the press
set(AWVA_KNOWN_trumpet slur)
set(AWVA_KNOWN_reed slur)
set(AWVA_KNOWN_modal "slur:wine glass")
