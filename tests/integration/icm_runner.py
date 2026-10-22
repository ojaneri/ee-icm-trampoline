"""Runs the cluster against a simulated bus and collects what it left behind.

Osvaldo Janeri Filho <janeri@gmail.com>

One run = one build variant + one simulator scenario. The cluster is started
first, the simulator as soon as the cluster prints "ICM READY", and the cluster
stops itself one second after the scenario's last frame (ICM_RUN_SECONDS).

Three details that are easy to get wrong with the POSIX target:

* stdin must be a pipe that stays open. StartOS() forks, and the parent
  process loops on getchar() to turn keys into interrupts. On /dev/null that
  loop spins at 100 % CPU on EOF; on an open pipe it blocks, as on a terminal.
* VIPER_PATH must point at the ViPER build, or the OS refuses to start.
* Real-time scheduling. On a loaded host the Linux scheduler, not Trampoline,
  decides when our process runs. When we have the privilege we run the
  cluster under SCHED_FIFO (chrt -f 50); ViPER and the CAN controller thread
  inherit it. Which mode was used is recorded in the run directory, and the
  evidence reports both (see requirements/test_evidence.md).
"""
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "sim"))

import analyze_timing  # noqa: E402
import can_sim  # noqa: E402

DIAG_RE = re.compile(
    r"DIAG t=(?P<t>[\d.]+) mono_ns=(?P<mono>\d+) DTC 0x(?P<code>[0-9A-F]{2}) "
    r"(?P<name>\S+) (?P<state>CONFIRMED|HEALED)")
LOG_RE = re.compile(r"^LOG (.*)$", re.M)
SUMMARY_TASK_RE = re.compile(
    r"SUMMARY task=(?P<task>\S+) jobs=(?P<jobs>\d+) deadline_misses=(?P<misses>\d+) "
    r"os_overruns=(?P<overruns>\d+)")


def trampoline_base() -> Path:
    return Path(os.environ.get("TRAMPOLINE_BASE", ROOT / "third_party" / "trampoline"))


def rt_prefix() -> list:
    """chrt prefix for SCHED_FIFO, if allowed. ICM_RT=0 disables it."""
    if os.environ.get("ICM_RT", "1") == "0":
        return []
    custom = os.environ.get("ICM_RT_PREFIX")
    if custom is not None:
        return shlex.split(custom)
    if shutil.which("chrt") and os.geteuid() == 0:
        return ["chrt", "-f", "50"]
    return []


class Run:
    """Everything a run produced, parsed."""

    def __init__(self, outdir: Path):
        self.dir = outdir
        self.log = (outdir / "log.txt").read_text(errors="replace")
        self.meta = json.loads((outdir / "run.json").read_text())
        self.events = []
        ev = outdir / "events.jsonl"
        if ev.exists():
            self.events = [json.loads(line) for line in ev.read_text().splitlines() if line]
        self.diag = [dict(m.groupdict(), t=float(m["t"]), mono=int(m["mono"]))
                     for m in DIAG_RE.finditer(self.log)]
        self.logs = [self._kv(m.group(1)) for m in LOG_RE.finditer(self.log)]
        self.summary = {m["task"]: {k: int(m[k]) for k in ("jobs", "misses", "overruns")}
                        for m in SUMMARY_TASK_RE.finditer(self.log)}
        self.frames = self._frames(self.log)
        self.timing = analyze_timing.analyze(outdir) if (outdir / "jobs.csv").exists() else None

    @staticmethod
    def _frames(log: str) -> list:
        """Dashboard frames as (uptime_s, text). A frame is the run of lines
        starting with '|' or '+' around a '| ICM Instrument Cluster' header."""
        frames, cur, t = [], None, None
        for line in log.replace("\r", "").split("\n"):
            if line.startswith("| ICM Instrument Cluster"):
                m = re.search(r"t=\s*([\d.]+) s", line)
                t = float(m.group(1)) if m else None
                cur = ["+" + "-" * 70 + "+", line]
            elif cur is not None and (line.startswith("|") or line.startswith("+")):
                cur.append(line)
            elif cur is not None:
                frames.append((t, "\n".join(cur)))
                cur = None
        if cur is not None:
            frames.append((t, "\n".join(cur)))
        return frames

    def uptime_of(self, mono_ns: int) -> float:
        """Cluster uptime (the t= of frames and logs) at a CLOCK_MONOTONIC
        instant, e.g. one stamped by the simulator."""
        first = self.logs[0]
        t0 = first["mono"] - int(first["t"] * 1e9)
        return (mono_ns - t0) / 1e9

    def frames_between(self, t_from: float, t_to: float) -> list:
        return [f for t, f in self.frames if t is not None and t_from <= t < t_to]

    @staticmethod
    def _kv(line: str) -> dict:
        d = {}
        for tok in line.split():
            if "=" in tok:
                k, v = tok.split("=", 1)
                d[k] = v
        d["t"] = float(d["t"])
        d["mono"] = int(d["mono_ns"])
        return d

    def summary_value(self, key: str) -> int:
        m = re.search(rf"SUMMARY .*?\b{key}=(\d+)", self.log)
        if not m:
            raise AssertionError(f"no SUMMARY {key} in {self.dir}/log.txt")
        return int(m.group(1))

    def sim(self, event: str, **match) -> list:
        return [e for e in self.events if e.get("event") == event and
                all(e.get(k) == v for k, v in match.items())]

    def sim_end_mono(self) -> int:
        ends = self.sim("end")
        return ends[0]["mono_ns"] if ends else 2**63

    def dtc_events(self, name: str, state: str = None) -> list:
        return [d for d in self.diag if d["name"] == name and
                (state is None or d["state"] == state)]


def run(variant: str, scenario: str = None, name: str = None, seconds: float = None,
        iface: str = None) -> Run:
    iface = iface or os.environ.get("ICM_CAN_IF", "vcan0")
    exe = ROOT / "build" / variant / "icm_exe"
    if not exe.exists():
        raise RuntimeError(f"{exe} missing; run tools/build.sh {variant}")
    if seconds is None:
        seconds = can_sim.SCENARIOS[scenario]["duration"] + 1.0 if scenario else 10.0

    base = Path(os.environ.get("ICM_RUNS_DIR", ROOT / "build" / "runs"))
    outdir = base / (name or f"{variant}-{scenario or 'idle'}")
    shutil.rmtree(outdir, ignore_errors=True)
    outdir.mkdir(parents=True)

    env = dict(os.environ,
               VIPER_PATH=str(trampoline_base() / "viper"),
               ICM_RUN_SECONDS=str(int(round(seconds))),
               ICM_DASH="plain",
               ICM_TRACE_DIR=str(outdir),
               ICM_CAN_IF=iface)
    prefix = rt_prefix()
    meta = {"variant": variant, "scenario": scenario, "seconds": seconds,
            "iface": iface, "sched": "SCHED_FIFO 50" if prefix else "SCHED_OTHER",
            "cmd": prefix + [str(exe)]}
    (outdir / "run.json").write_text(json.dumps(meta, indent=2))

    with open(outdir / "log.txt", "wb") as log:
        proc = subprocess.Popen(prefix + [str(exe)], stdin=subprocess.PIPE,
                                stdout=log, stderr=subprocess.STDOUT,
                                env=env, cwd=outdir)
        deadline = time.monotonic() + 15.0
        while b"ICM READY" not in (outdir / "log.txt").read_bytes():
            if proc.poll() is not None or time.monotonic() > deadline:
                proc.kill()
                raise RuntimeError(f"cluster did not start, see {outdir}/log.txt")
            time.sleep(0.01)

        sim = None
        if scenario:
            cmd = [sys.executable, str(ROOT / "sim" / "can_sim.py"),
                   "--iface", iface, "--scenario", scenario,
                   "--events", str(outdir / "events.jsonl")]
            if seconds < can_sim.SCENARIOS[scenario]["duration"] + 1.0:
                cmd += ["--duration", str(seconds - 1.0)]
            sim = subprocess.Popen(cmd)
        try:
            proc.wait(timeout=seconds + 30)
        finally:
            if proc.poll() is None:
                proc.kill()
            proc.stdin.close()
            if sim is not None:
                sim.wait(timeout=60)
    return Run(outdir)


_cache = {}


def cached(variant: str, scenario: str = None, seconds: float = None) -> Run:
    """Each (variant, scenario) runs once per test session; several test
    classes look at the same nominal run from different angles."""
    key = (variant, scenario, seconds)
    if key not in _cache:
        _cache[key] = run(variant, scenario, seconds=seconds)
    return _cache[key]
