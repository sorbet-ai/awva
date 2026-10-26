#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
#
# Rewrites sdk/tests/goldens/renders.txt from a build of the SDK (sdk/CMakeLists.txt: the golden
# tests), after an intended change of an engine's output or of the MIDI mapping. Say in the commit
# what changed and by how much (awva_render --compare against the old render).
# Usage: tools/goldens/update.sh <build dir> [ctest -j]
set -euo pipefail
cd "$(dirname "$0")/../.."
build="$1"
jobs="${2:-8}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
AWVA_GOLDEN_OUT="$tmp" ctest --test-dir "$build" -L golden -j "$jobs" --output-on-failure > "$tmp/ctest.log" || {
  tail -40 "$tmp/ctest.log"
  echo "update.sh: a golden render failed its other checks (finite, peak, silent after): nothing written" >&2
  exit 1
}
{
  echo "# Golden render checksums: <ctest name> <FNV-1a 64 of the render's float bits> (sdk/CMakeLists.txt)."
  echo "# Written by tools/goldens/update.sh after an intended change of the output."
  cat "$tmp"/*.hash | LC_ALL=C sort
} > sdk/tests/goldens/renders.txt
echo "update.sh: $(grep -vc '^#' sdk/tests/goldens/renders.txt) goldens in sdk/tests/goldens/renders.txt"
