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
#   slur           4 clicks in 48 gentle slurs (the first by -7, 46 ms after the press: as the held key
#                  lets go), through mono's player (mono.h, a fingered family's 15 ms+ pitch move);
#                  the lips' and the loop delay's reset on a new note (voice_trumpet.h: jump) is the
#                  next suspect
#   roll           1 click in 24 rolls (the fast octave down)
set(AWVA_KNOWN_trumpet slur roll)

# The same with the engine built and played at 16 voices (contract16.<engine>.*), on top of the above:
#   set(AWVA_KNOWN16_<engine> <tag> ...)
# va: its owner
#   silence:keys   the FTZ guard's random 16-voice performance: 31318 samples (0.65 s) above 1e-4 after
#                  the declared tail (10 s + 0.2 s) on the keys preset; at 10 voices it is silent
set(AWVA_KNOWN16_va "silence:keys")
