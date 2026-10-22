#!/usr/bin/env bash
# ICM — generate the OS configuration with GOIL and build one variant.
# Osvaldo Janeri Filho <janeri@gmail.com>
#
# Usage: tools/build.sh [variant]
#
#   nominal        the product. No test hooks compiled in.
#   race_locked    Task_Display copies ICM_Data slowly (12 ms window) while
#                  holding ICM_Resource. test_race.py expects 0 torn copies.
#   race_unlocked  same window, ICM_Resource taken out of the accessors.
#                  test_race.py expects torn copies: the negative control.
#   overload       Task_Diagnostics burns 150 ms per 100 ms period between
#                  t = 5 s and t = 10 s. test_overload.py expects E_OS_LIMIT
#                  on that task and no deadline miss on the others.
#   irq_lock       Task_Diagnostics locks all interrupts for 200 ms once, at
#                  t = 3 s. test_overrun.py expects lost frames, counted.
#
# The binary lands in build/<variant>/icm_exe.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
VARIANT=${1:-nominal}
TPL=${TRAMPOLINE_BASE:-$ROOT/third_party/trampoline}
GOIL=$TPL/goil/makefile-unix/goil

case "$VARIANT" in
  nominal)       CFLAGS="" ;;
  race_locked)   CFLAGS="-DICM_RACE_WINDOW_US=12000u" ;;
  race_unlocked) CFLAGS="-DICM_RACE_WINDOW_US=12000u -DICM_UNLOCKED_SHARED_DATA" ;;
  overload)      CFLAGS="-DICM_FAULT_OVERLOAD_DIAG_MS=150" ;;
  irq_lock)      CFLAGS="-DICM_FAULT_IRQ_LOCK_MS=200" ;;
  *) echo "unknown variant: $VARIANT" >&2; exit 2 ;;
esac

if [ ! -x "$GOIL" ]; then
  echo "GOIL not found at $GOIL; run tools/setup_trampoline.sh first" >&2
  exit 1
fi

OUT=$ROOT/build/$VARIANT
mkdir -p "$OUT"
sed -e "s|@TPL_BASE@|$TPL|g" \
    -e "s|@ICM_SRC@|$ROOT/src|g" \
    -e "s|@ICM_CFLAGS@|$CFLAGS|g" \
    "$ROOT/src/icm.oil" > "$OUT/icm.oil"

cd "$OUT"
echo "== GOIL: generating the OS configuration for '$VARIANT'"
"$GOIL" --target=posix/linux --templates="$TPL/goil/templates/" icm.oil
echo "== compiling and linking"
./make.py -s
echo "== built $OUT/icm_exe"
