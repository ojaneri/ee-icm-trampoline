#!/usr/bin/env bash
# ICM — run everything and keep the console output as evidence.
# Osvaldo Janeri Filho <janeri@gmail.com>
#
# Produces docs/evidence/*.txt from real runs: environment, unit tests,
# static checks, every build variant, the integration suite, the timing
# tables of each run, and the comparison with and without SCHED_FIFO.
# requirements/test_evidence.md quotes these files; nothing in it is typed
# by hand.
#
# Needs: a vcan0 interface, tools/setup_trampoline.sh done, and root (or
# ICM_RT_PREFIX) for SCHED_FIFO. Usage: tools/collect_evidence.sh [python]
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
PY=${1:-python3}
EV=$ROOT/docs/evidence
mkdir -p "$EV"
export ICM_RUNS_DIR=$ROOT/build/runs

run() { # run <file> <command...>: echo the command, then its output
  local out=$1; shift
  { echo "\$ $*"; "$@" 2>&1; echo "[exit status $?]"; } | tee "$EV/$out" | tail -5
}

{
  echo "date:     $(date -u '+%Y-%m-%d %H:%M:%S UTC')"
  echo "host:     $(uname -srm)"
  echo "os:       $(. /etc/os-release; echo "$PRETTY_NAME")"
  echo "cpu:      $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | xargs) x $(nproc)"
  echo "load:     $(cut -d' ' -f1-3 /proc/loadavg)"
  echo "gcc:      $(gcc --version | head -1)"
  echo "cmake:    $(cmake --version | head -1)"
  echo "python:   $($PY --version 2>&1)"
  echo "goil:     $("${TRAMPOLINE_BASE:-$ROOT/third_party/trampoline}"/goil/makefile-unix/goil --version | head -1)"
  echo "trampoline commit: $(git -C "${TRAMPOLINE_BASE:-$ROOT/third_party/trampoline}" rev-parse HEAD)"
  echo "icm commit:        $(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo 'not a git checkout')"
  echo "can interface:     $(ip -br link show "${ICM_CAN_IF:-vcan0}" 2>&1)"
} > "$EV/00_environment.txt"
cat "$EV/00_environment.txt"

cd "$ROOT"
rm -rf build/unit
run 01_unit_tests.txt bash -c "cmake -S . -B build/unit -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all' >/dev/null \
  && cmake --build build/unit >/dev/null && ctest --test-dir build/unit --output-on-failure -V | grep -E '^\S*[0-9]+: \[|tests passed|checks'"
run 02_static_checks.txt bash -c "$PY tools/check_shared_access.py && $PY tools/check_traceability.py"
$PY tools/check_traceability.py --matrix > "$EV/02b_traceability_matrix.md"

for v in nominal race_locked race_unlocked overload irq_lock; do
  run "03_build_$v.txt" tools/build.sh "$v"
done

run 04_integration_tests.txt bash -c "cd tests/integration && $PY -m unittest -v"

for d in build/runs/*/; do
  name=$(basename "$d")
  { echo "run: $name"; cat "$d/run.json"; echo; $PY tools/analyze_timing.py "$d"; } \
    > "$EV/05_timing_$name.md" 2>&1
  grep -E "^(ICM READY|DIAG|SUMMARY)" "$d/log.txt" > "$EV/06_events_$name.txt"
done

# The same nominal run without SCHED_FIFO, for the comparison in the report.
ICM_RT=0 $PY -c "
import sys; sys.path.insert(0, 'tests/integration')
import icm_runner
r = icm_runner.run('nominal', 'nominal', name='nominal-sched_other')
" && { echo "run: nominal-sched_other"; cat build/runs/nominal-sched_other/run.json; echo;
       $PY tools/analyze_timing.py build/runs/nominal-sched_other; } > "$EV/05_timing_nominal-sched_other.md"

# End-to-end detection latencies of the fault run (PR #5 review: these used
# to be computed by hand and pasted, so nothing in the repository held them).
$PY tools/fault_latencies.py build/runs/nominal-faults > "$EV/08_fault_latencies.txt"

# One dashboard frame per interesting moment of the fault run.
$PY - <<'EOF' > "$EV/07_dashboard_frames.txt"
import sys; sys.path.insert(0, 'tests/integration')
import icm_runner
r = icm_runner.Run(icm_runner.Path('build/runs/nominal-faults'))
start = r.sim("start")[0]["mono_ns"]
for label, sim_t in (("nominal, cruise", 4.5), ("M2 lost", 9.0), ("rpm out of range", 13.0),
                     ("M4 lost, right indicator requested", 23.5), ("recovered", 30.5)):
    t = r.uptime_of(start + int(sim_t * 1e9))
    frames = r.frames_between(t - 0.6, t + 0.6)
    print(f"=== simulator t = {sim_t} s: {label}")
    print(frames[0] if frames else "(no frame)")
    print()
EOF
echo "== evidence written to $EV"
