#!/usr/bin/env bash
# ICM — fetch and build the Trampoline toolchain (GOIL + ViPER).
# Osvaldo Janeri Filho <janeri@gmail.com>
#
# Trampoline is pinned to one commit. An unpinned RTOS means a build that
# passes today and fails next month for reasons that have nothing to do with
# this project; the commit below is the one every result in
# requirements/test_evidence.md was produced with.
#
# Usage: tools/setup_trampoline.sh [destination]   (default third_party/trampoline)
set -euo pipefail

TRAMPOLINE_REPO=https://github.com/TrampolineRTOS/trampoline.git
TRAMPOLINE_COMMIT=09e2dcb1206fb6ebaa73c84558f26ac21f57b2dd

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEST=${1:-$ROOT/third_party/trampoline}

if [ ! -d "$DEST/.git" ]; then
  echo "== cloning Trampoline @ $TRAMPOLINE_COMMIT into $DEST"
  mkdir -p "$DEST"
  git -C "$DEST" init -q
  git -C "$DEST" remote add origin "$TRAMPOLINE_REPO"
  git -C "$DEST" fetch -q --depth 1 origin "$TRAMPOLINE_COMMIT"
  git -C "$DEST" checkout -q FETCH_HEAD
fi

if [ ! -x "$DEST/goil/makefile-unix/goil" ]; then
  echo "== building GOIL (the OIL compiler; takes about a minute)"
  (cd "$DEST/goil/makefile-unix" && ./build.py >/dev/null)
fi
"$DEST/goil/makefile-unix/goil" --version | head -1

if [ ! -x "$DEST/viper/viper" ]; then
  echo "== building ViPER (the virtual timer that ticks SystemCounter)"
  make -s -C "$DEST/viper"
fi
echo "== Trampoline ready in $DEST"
