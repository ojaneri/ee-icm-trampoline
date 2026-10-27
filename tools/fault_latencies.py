#!/usr/bin/env python3
"""End-to-end fault detection latencies of a "faults" run.

Osvaldo Janeri Filho <janeri@gmail.com>

For each fault of the scenario: the instant the simulator put the relevant
frame on the bus (or the last good one before the fault), and the instant
the cluster confirmed or healed the DTC. Both are CLOCK_MONOTONIC stamps from
two processes on the same machine, so the latency is a subtraction.

Usage: fault_latencies.py RUN_DIR      (e.g. build/runs/nominal-faults)
Output goes to docs/evidence/08_fault_latencies.txt via collect_evidence.sh.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests" / "integration"))
import icm_runner  # noqa: E402


def main(argv):
    run = icm_runner.Run(Path(argv[1]))

    def tx(msg):
        return [e for e in run.sim("tx") if e["msg"] == msg]

    def last_before(msg, t):
        return [e for e in tx(msg) if e["t"] < t][-1]

    def first_from(msg, t):
        return [e for e in tx(msg) if e["t"] >= t][0]

    def latency(name, state, ref):
        d = [d for d in run.dtc_events(name, state) if d["mono"] > ref["mono_ns"]][0]
        return (d["mono"] - ref["mono_ns"]) / 1e9

    last_log = run.logs[-1]
    m2_last = last_before("M2", 5.0)
    m2_back = first_from("M2", 10.0)
    m1_bad = first_from("M1", 11.0)
    m2_bad = first_from("M2", 15.0)
    corrupt = [e for e in tx("M3") if e["corrupt"]]
    frozen = [e for e in tx("M4") if e["frozen"]]
    m4_last = last_before("M4", 22.5)

    print(f"run: {run.dir}")
    print(f"M2 timeout: last M2 at sim t={m2_last['t']:.3f} -> CONFIRMED after "
          f"{latency('M2_VEHICLE_SPEED_TIMEOUT', 'CONFIRMED', m2_last):.3f} s")
    print(f"M2 heal: first M2 back at t={m2_back['t']:.3f} -> HEALED after "
          f"{latency('M2_VEHICLE_SPEED_TIMEOUT', 'HEALED', m2_back):.3f} s")
    print(f"RPM 9000: first bad M1 at t={m1_bad['t']:.3f} -> CONFIRMED after "
          f"{latency('RPM_OUT_OF_RANGE', 'CONFIRMED', m1_bad):.3f} s")
    print(f"Speed 300: first bad M2 at t={m2_bad['t']:.3f} -> CONFIRMED after "
          f"{latency('SPEED_OUT_OF_RANGE', 'CONFIRMED', m2_bad):.3f} s")
    print(f"CRC: {len(corrupt)} corrupt M3 frames sent, cluster crc counter = "
          f"{last_log['crc']}; CONFIRMED after "
          f"{latency('CAN_E2E_ERROR', 'CONFIRMED', corrupt[0]):.3f} s")
    print(f"Frozen counter: {len(frozen)} M4 frames, cluster cnt = {last_log['cnt']}; "
          f"M4 TIMEOUT CONFIRMED "
          f"{latency('M4_BODY_STATUS_TIMEOUT', 'CONFIRMED', frozen[0]):.3f} s after first frozen frame")
    print(f"M4 timeout: last M4 at t={m4_last['t']:.3f} -> CONFIRMED after "
          f"{latency('M4_BODY_STATUS_TIMEOUT', 'CONFIRMED', m4_last):.3f} s")
    print(f"burst: M1 rx={last_log['rx']} ovr={last_log['ovr']}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
