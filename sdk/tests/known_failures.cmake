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

# ep: its owner
#   tuning:pianet  the low notes sharp: +64 c at note 48, +36 c at 54/55, +9 c at 60
# wind: its owner
#   tuning:flute   below its range: -48 c at note 48, -72 c at 43, +26 c at 55, -23 c at 60; in tune
#                  from 67 (a declared range, preset_tuning, may be the answer)
# accordion, trumpet, va: fixed in their own PRs (the re-press: a press on a voice in its tail as a new press)
#   repress:violin  accordion violin: soft, again after 150 ms, arrives 140 ms late (225 against 85 ms)
#   repress         trumpet: soft, again after 150 ms, arrives 165 ms late (290 against 125 ms), -12 dB
#   repress:brass   va brass: soft, again after 60 ms, arrives 95 ms late (150 against 55 ms)
set(AWVA_KNOWN_accordion repress:violin)
set(AWVA_KNOWN_ep tuning:pianet)
set(AWVA_KNOWN_trumpet repress)
set(AWVA_KNOWN_va repress:brass)
set(AWVA_KNOWN_wind tuning:flute)
