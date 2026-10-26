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
# modal: its owner
#   tuning:bell    flat by -9 c at note 48 growing to -16 c at 84 (an inharmonic bell: a declared
#                  tolerance or range may be the answer)
# wind: its owner
#   tuning:flute   below its range: -48 c at note 48, -72 c at 43, +26 c at 55, -23 c at 60; in tune
#                  from 67 (a declared range, preset_tuning, may be the answer)
set(AWVA_KNOWN_ep tuning:pianet)
set(AWVA_KNOWN_modal tuning:bell)
set(AWVA_KNOWN_wind tuning:flute)
