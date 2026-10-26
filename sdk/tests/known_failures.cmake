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

# every component (the shim, component/include/nsc/shim.h): the note ids' owner
#   note_ids.reset  reset() starts the MIDI side afresh and drops the ids it tracks: the ids of the
#                   notes sounding at a reset never come back from ended() (68 of 260 in the check)
set(AWVA_KNOWN_COMPONENT note_ids.reset)
