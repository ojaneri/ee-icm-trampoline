#!/usr/bin/env python3
"""Turns the cluster's timing trace into numbers and markdown tables.

Osvaldo Janeri Filho <janeri@gmail.com>

Input is the directory a run left behind (jobs.csv and ticks.csv, written by
ShutdownHook through src/icm_trace.c). Output is what goes into
requirements/test_evidence.md: per-task period, jitter, response and execution
time, deadline misses, plus the schedulability check (utilisation against the
Liu & Layland bound, and response-time analysis with measured execution times).

Two clocks, kept apart on purpose:

  ticks  the OS view. Releases happen on SystemCounter ticks; whether the
         n-th release of a 20 ms task lands exactly 2 ticks after the
         previous one is a property of Trampoline and the OIL file.
  ns     the wall-clock view, CLOCK_MONOTONIC. Includes ViPER's tick drift
         and whatever the Linux scheduler does to our process.

Usage: analyze_timing.py RUN_DIR [--markdown]
"""
import argparse
import csv
import json
import math
import statistics
import sys
from pathlib import Path

TICK_MS = 10.0
PERIOD_MS = {
    "Task_Acquisition": 10.0,
    "Task_Processing": 20.0,
    "Task_Display": 50.0,
    "Task_Diagnostics": 100.0,
    "Task_Logging": 1000.0,
}
DEADLINE_MS = dict(PERIOD_MS, Task_CAN_RX=10.0)
# OIL priorities (higher = more urgent). Used for the response-time analysis.
PRIORITY = {
    "Task_CAN_RX": 6,
    "Task_Acquisition": 5,
    "Task_Processing": 4,
    "Task_Display": 3,
    "Task_Diagnostics": 2,
    "Task_Logging": 1,
}
# Task_CAN_RX is sporadic. Its minimum inter-arrival time in the simulator's
# schedule is the 3 ms stagger between message offsets; used as T in the
# response-time analysis, which over-counts its interference (on purpose).
CAN_RX_MIN_INTERARRIVAL_MS = 3.0
TASK_ORDER = ["Task_CAN_RX", "Task_Acquisition", "Task_Processing",
              "Task_Display", "Task_Diagnostics", "Task_Logging"]


def pct(values, p):
    if not values:
        return float("nan")
    s = sorted(values)
    k = min(len(s) - 1, max(0, int(math.ceil(p / 100.0 * len(s))) - 1))
    return s[k]


def load_jobs(run_dir):
    jobs = {}
    with open(Path(run_dir) / "jobs.csv") as f:
        for row in csv.DictReader(f):
            r = {k: (v if k == "task" else int(v)) for k, v in row.items()}
            jobs.setdefault(r["task"], []).append(r)
    return jobs


def load_ticks(run_dir):
    with open(Path(run_dir) / "ticks.csv") as f:
        return [int(r["ns"]) for r in csv.DictReader(f)]


def tick_stats(ticks):
    d = [(b - a) / 1e6 for a, b in zip(ticks[1:], ticks[2:]) if a and b]
    if not d:
        return {}
    return {
        "ticks": len(ticks) - 1,
        "mean_ms": statistics.fmean(d),
        "stdev_ms": statistics.pstdev(d),
        "min_ms": min(d),
        "max_ms": max(d),
        "p99_ms": pct(d, 99),
        "drift_pct": (statistics.fmean(d) - TICK_MS) / TICK_MS * 100.0,
    }


def task_stats(name, rows):
    resp = [(r["end_ns"] - r["release_ns"]) / 1e3 for r in rows]
    execu = [r["exec_ns"] / 1e3 for r in rows]
    delay = [(r["start_ns"] - r["release_ns"]) / 1e3 for r in rows]
    st = {
        "jobs": len(rows),
        "deadline_ms": DEADLINE_MS[name],
        "resp_mean_us": statistics.fmean(resp) if resp else float("nan"),
        "resp_p99_us": pct(resp, 99),
        "resp_max_us": max(resp) if resp else float("nan"),
        "exec_mean_us": statistics.fmean(execu) if execu else float("nan"),
        "exec_max_us": max(execu) if execu else float("nan"),
        "start_delay_p99_us": pct(delay, 99),
        "start_delay_max_us": max(delay) if delay else float("nan"),
        "preemptions_max": max((r["preemptions"] for r in rows), default=0),
        "misses": sum(1 for x in resp if x > DEADLINE_MS[name] * 1000.0),
    }
    if name in PERIOD_MS:
        period_ticks = int(PERIOD_MS[name] / TICK_MS)
        rel = [r["release_tick"] for r in rows]
        tick_gaps = [b - a for a, b in zip(rel, rel[1:])]
        st["period_ms"] = PERIOD_MS[name]
        st["release_gaps_exact"] = all(g == period_ticks for g in tick_gaps)
        st["release_gap_counts"] = {g: tick_gaps.count(g) for g in sorted(set(tick_gaps))}
        st["start_in_release_tick_pct"] = 100.0 * sum(
            1 for r in rows if r["start_tick"] == r["release_tick"]) / max(1, len(rows))
        wall = [(b["start_ns"] - a["start_ns"]) / 1e6 for a, b in zip(rows, rows[1:])]
        st["period_wall_mean_ms"] = statistics.fmean(wall) if wall else float("nan")
        st["period_wall_min_ms"] = min(wall) if wall else float("nan")
        st["period_wall_max_ms"] = max(wall) if wall else float("nan")
        st["period_wall_stdev_ms"] = statistics.pstdev(wall) if wall else float("nan")
    else:
        isr = [(r["aux_ns"] - r["release_ns"]) / 1e3 for r in rows if r["aux_ns"]]
        st["irq_latency_p50_us"] = pct(isr, 50)
        st["irq_latency_p99_us"] = pct(isr, 99)
        st["irq_latency_max_us"] = max(isr) if isr else float("nan")
    return st


def schedulability(stats):
    """Utilisation and response-time analysis from measured execution times.

    C is the largest net execution time observed (exec_max). B, the blocking
    term of the priority ceiling protocol, is bounded by the longest
    execution time of any lower-priority task that uses ICM_Resource. That is
    pessimistic (a critical section is one memcpy, a whole job is much more),
    which is the safe direction to be wrong in.
    """
    tasks = [t for t in TASK_ORDER if t in stats]
    C = {t: stats[t]["exec_max_us"] / 1000.0 for t in tasks}
    T = {t: PERIOD_MS.get(t, CAN_RX_MIN_INTERARRIVAL_MS) for t in tasks}
    U = sum(C[t] / T[t] for t in tasks)
    n = len(tasks)
    bound = n * (2 ** (1.0 / n) - 1)

    rta = {}
    for t in tasks:
        hp = [j for j in tasks if PRIORITY[j] > PRIORITY[t]]
        lp = [j for j in tasks if PRIORITY[j] < PRIORITY[t]]
        B = max((C[j] for j in lp), default=0.0)
        R = C[t] + B
        for _ in range(1000):
            R_next = C[t] + B + sum(math.ceil(R / T[j]) * C[j] for j in hp)
            if abs(R_next - R) < 1e-9:
                break
            R = R_next
            if R > 100.0 * DEADLINE_MS[t]:  # diverging: higher priorities saturate the CPU
                R = float("inf")
                break
        rta[t] = {"C_ms": C[t], "B_ms": B, "R_ms": R,
                  "D_ms": DEADLINE_MS[t], "ok": R <= DEADLINE_MS[t]}
    return {"U": U, "rm_bound": bound, "n": n, "rta": rta}


def analyze(run_dir):
    jobs = load_jobs(run_dir)
    ticks = load_ticks(run_dir)
    stats = {t: task_stats(t, jobs[t]) for t in TASK_ORDER if jobs.get(t)}
    return {"ticks": tick_stats(ticks), "tasks": stats,
            "sched": schedulability(stats)}


def markdown(a):
    out = []
    tk = a["ticks"]
    if tk:
        out.append("| SystemCounter tick | value |\n|---|---|")
        out.append(f"| ticks recorded | {tk['ticks']} |")
        out.append(f"| mean period | {tk['mean_ms']:.4f} ms ({tk['drift_pct']:+.2f} % vs 10 ms) |")
        out.append(f"| stdev / min / p99 / max | {tk['stdev_ms']:.3f} / {tk['min_ms']:.3f} / "
                   f"{tk['p99_ms']:.3f} / {tk['max_ms']:.3f} ms |")
        out.append("")
    out.append("| Task | T (ms) | jobs | release gaps (ticks) | started in release tick | "
               "wall period mean / min / max (ms) | start delay p99 (us) | "
               "response mean / p99 / max (us) | exec mean / max (us) | misses |")
    out.append("|---|---|---|---|---|---|---|---|---|---|")
    for t, s in a["tasks"].items():
        if "period_ms" in s:
            gaps = ", ".join(f"{g}x{c}" for g, c in s["release_gap_counts"].items())
            out.append(
                f"| {t} | {s['period_ms']:.0f} | {s['jobs']} | {gaps} | "
                f"{s['start_in_release_tick_pct']:.1f} % | "
                f"{s['period_wall_mean_ms']:.3f} / {s['period_wall_min_ms']:.3f} / "
                f"{s['period_wall_max_ms']:.3f} | {s['start_delay_p99_us']:.1f} | "
                f"{s['resp_mean_us']:.1f} / {s['resp_p99_us']:.1f} / {s['resp_max_us']:.1f} | "
                f"{s['exec_mean_us']:.1f} / {s['exec_max_us']:.1f} | {s['misses']} |")
        else:
            out.append(
                f"| {t} | sporadic (D={s['deadline_ms']:.0f}) | {s['jobs']} | - | - | - | "
                f"irq p99 {s['irq_latency_p99_us']:.1f} | "
                f"{s['resp_mean_us']:.1f} / {s['resp_p99_us']:.1f} / {s['resp_max_us']:.1f} | "
                f"{s['exec_mean_us']:.1f} / {s['exec_max_us']:.1f} | {s['misses']} |")
    sc = a["sched"]
    out.append("")
    out.append(f"Utilisation U = {sc['U']*100:.3f} % (Liu & Layland bound for n = {sc['n']}: "
               f"{sc['rm_bound']*100:.1f} %)")
    out.append("")
    out.append("| Task | C (ms) | B (ms) | R (ms) | D (ms) | R <= D |")
    out.append("|---|---|---|---|---|---|")
    for t, r in sc["rta"].items():
        out.append(f"| {t} | {r['C_ms']:.3f} | {r['B_ms']:.3f} | {r['R_ms']:.3f} | "
                   f"{r['D_ms']:.0f} | {'yes' if r['ok'] else 'NO'} |")
    return "\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Analyse an ICM timing trace")
    ap.add_argument("run_dir")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)
    a = analyze(args.run_dir)
    if args.json:
        print(json.dumps(a, indent=2, default=str))
    else:
        print(markdown(a))
    return 0


if __name__ == "__main__":
    sys.exit(main())
