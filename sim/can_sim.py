#!/usr/bin/env python3
"""ICM CAN bus simulator.

Osvaldo Janeri Filho <janeri@gmail.com>

Plays the other ECUs of the car: puts M1..M4 on a SocketCAN interface at their
nominal periods, with the values of a simple drive cycle, and injects the
faults a scenario asks for (dropped messages, out-of-range values, corrupted
CRC, frozen alive counter, frame bursts).

Only the standard library is used (AF_CAN sockets have been in Python's
socket module since 3.3), so the simulator runs anywhere the cluster does.

Every frame sent and every scenario phase is written to --events as one JSON
line with its CLOCK_MONOTONIC time. The cluster logs in the same clock, which
is what lets the tests measure detection latency end to end (last M2 frame on
the bus -> DTC confirmed in the cluster).

Usage:
    can_sim.py --iface vcan0 --scenario faults --events sim_events.jsonl
    can_sim.py --list
"""
import argparse
import json
import math
import socket
import struct
import sys
import time

CAN_FRAME_FMT = "=IB3x8s"  # struct can_frame: id, dlc, pad, data[8]

# --------------------------------------------------------------------------
# Signal dictionary: must agree with src/can_db.c, src/icm_config.h and
# dbc/icm.dbc (tests/integration/test_dbc_consistency.py checks it does).
# --------------------------------------------------------------------------
IDS = {"M1": 0x100, "M2": 0x200, "M3": 0x300, "M4": 0x400}
PERIOD_S = {"M1": 0.500, "M2": 1.000, "M3": 1.000, "M4": 0.100}
# Real ECUs do not all transmit on the same millisecond; neither do we.
OFFSET_S = {"M1": 0.000, "M2": 0.003, "M3": 0.006, "M4": 0.009}

SPEED_FACTOR = 0.01   # km/h per bit
RPM_FACTOR = 0.25     # rpm per bit
FUEL_FACTOR = 0.5     # % per bit
TEMP_OFFSET = -40.0   # degC

TURN_LEFT, TURN_RIGHT, TURN_HAZARD = 0x01, 0x02, 0x04
WARN_OIL, WARN_BATTERY, WARN_BRAKE, WARN_MIL = 0x01, 0x02, 0x04, 0x08
WARN_ABS, WARN_AIRBAG, WARN_SEATBELT, WARN_DOOR = 0x10, 0x20, 0x40, 0x80


def crc8_j1850(data: bytes) -> int:
    """CRC-8 SAE J1850 (poly 0x1D, init 0xFF, xorout 0xFF), as in can_db.c."""
    crc = 0xFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1D) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc ^ 0xFF


def raw_from_physical(v: dict) -> dict:
    """Physical values of the drive cycle -> raw signal values."""
    return {
        "engine_speed_raw": int(round(v["rpm"] / RPM_FACTOR)),
        "vehicle_speed_raw": int(round(v["speed_kmh"] / SPEED_FACTOR)),
        "fuel_raw": int(round(v["fuel_pct"] / FUEL_FACTOR)),
        "coolant_raw": int(round(v["coolant_c"] - TEMP_OFFSET)),
        "turn_bits": v["turn_bits"],
        "warning_bits": v["warning_bits"],
    }


def pack(msg: str, raw: dict, counter: int, corrupt_crc: bool = False) -> bytes:
    """Builds the 8 data bytes of one ICM frame."""
    data = bytearray(8)
    if msg == "M1":
        struct.pack_into("<H", data, 0, raw["engine_speed_raw"] & 0xFFFF)
    elif msg == "M2":
        struct.pack_into("<H", data, 0, raw["vehicle_speed_raw"] & 0xFFFF)
    elif msg == "M3":
        data[0] = raw["fuel_raw"] & 0xFF
        data[1] = raw["coolant_raw"] & 0xFF
    elif msg == "M4":
        data[0] = raw["turn_bits"] & 0xFF
        data[1] = raw["warning_bits"] & 0xFF
    data[6] = counter & 0x0F
    can_id = IDS[msg]
    crc = crc8_j1850(bytes([can_id & 0xFF, (can_id >> 8) & 0xFF]) + bytes(data[:7]))
    data[7] = crc ^ 0xA5 if corrupt_crc else crc
    return bytes(data)


# --------------------------------------------------------------------------
# Drive cycle: 30 s, repeated if a scenario runs longer.
# --------------------------------------------------------------------------
GEARS = [(20, 110.0), (40, 60.0), (65, 42.0), (90, 32.0), (999, 26.0)]


def _smooth(x: float) -> float:
    return (1.0 - math.cos(math.pi * min(max(x, 0.0), 1.0))) / 2.0


def drive_cycle(t: float) -> dict:
    tc = t % 30.0
    if tc < 2.0:
        speed = 0.0
    elif tc < 14.0:
        speed = 120.0 * _smooth((tc - 2.0) / 12.0)
    elif tc < 20.0:
        speed = 120.0
    elif tc < 28.0:
        speed = 120.0 * (1.0 - _smooth((tc - 20.0) / 8.0))
    else:
        speed = 0.0

    ratio = next(r for limit, r in GEARS if speed < limit)
    rpm = max(800.0, speed * ratio)

    turn = 0
    if 3.0 <= tc < 6.0:
        turn = TURN_LEFT
    elif 22.0 <= tc < 25.0:
        turn = TURN_RIGHT
    elif tc >= 28.0:
        turn = TURN_HAZARD

    # Bulb check at ignition-on, then seatbelt until the car moves.
    warn = 0
    if t < 1.0:
        warn |= WARN_OIL | WARN_BATTERY | WARN_MIL | WARN_ABS | WARN_AIRBAG
    if t < 2.0:
        warn |= WARN_SEATBELT | WARN_BRAKE
    if t < 1.0:
        warn |= WARN_DOOR

    return {
        "speed_kmh": speed,
        "rpm": rpm,
        "fuel_pct": max(0.0, 65.0 - 0.05 * t),
        "coolant_c": 90.0 - 70.0 * math.exp(-t / 8.0),
        "turn_bits": turn,
        "warning_bits": warn,
    }


def warnings_cycle(t: float) -> dict:
    """Driver-warning scenario: low fuel from the start, an overheat episode."""
    v = drive_cycle(t)
    v["fuel_pct"] = 8.0
    if 3.0 <= t < 8.0:
        v["coolant_c"] = 90.0 + 35.0 * (t - 3.0) / 5.0      # up to 125 degC
    elif 8.0 <= t < 10.0:
        v["coolant_c"] = 125.0
    elif t >= 10.0:
        v["coolant_c"] = 100.0
    if 5.0 <= t < 9.0:
        v["rpm"] = 7000.0  # redline
    return v


# --------------------------------------------------------------------------
# Scenarios. Each phase is active in [start, end) seconds after the start.
# --------------------------------------------------------------------------
SCENARIOS = {
    "nominal": {
        "duration": 30.0,
        "cycle": drive_cycle,
        "phases": [],
    },
    "faults": {
        "duration": 32.0,
        "cycle": drive_cycle,
        "phases": [
            {"name": "m2_dropout", "start": 5.0, "end": 10.0, "drop": ["M2"]},
            {"name": "rpm_out_of_range", "start": 11.0, "end": 14.0,
             "raw": {"M1": {"engine_speed_raw": 36000}}},          # 9000 rpm
            {"name": "speed_out_of_range", "start": 15.0, "end": 17.0,
             "raw": {"M2": {"vehicle_speed_raw": 30000}}},         # 300 km/h
            {"name": "m3_crc_corrupt", "start": 18.0, "end": 19.5,
             "corrupt": ["M3"]},
            # The frozen-counter episode must heal (1 s of good M4 after
            # 20.8 s) before the dropout starts, or the two would merge into
            # one long DTC and the dropout could not be timed on its own.
            {"name": "m4_frozen_counter", "start": 20.0, "end": 20.8,
             "freeze_counter": ["M4"]},
            {"name": "m4_dropout", "start": 22.5, "end": 24.5, "drop": ["M4"]},
            # 200 frames as fast as the sender can go. Robustness, not
            # overrun: the ISR drains the FIFO faster than any sender fills
            # it. Overrun needs a blocked ISR, see the "babble" scenario.
            {"name": "burst", "start": 26.0, "end": 26.05, "burst": 200},
        ],
    },
    "warnings": {
        "duration": 15.0,
        "cycle": warnings_cycle,
        "phases": [],
    },
    # A "babbling idiot": the engine ECU sends M1 every millisecond for 4 s.
    # Used with the irq_lock build, where the cluster cannot service its CAN
    # interrupt for 200 ms in the middle of it.
    "babble": {
        "duration": 8.0,
        "cycle": drive_cycle,
        "phases": [
            {"name": "m1_babbling", "start": 2.0, "end": 6.0,
             "period": {"M1": 0.001}},
        ],
    },
}


def active_phases(scn: dict, t: float) -> list:
    return [p for p in scn["phases"] if p["start"] <= t < p["end"]]


class Simulator:
    def __init__(self, iface: str, events_path: str | None):
        self.sock = socket.socket(socket.PF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
        self.sock.bind((iface,))
        self.events = open(events_path, "w") if events_path else None
        self.counter = {m: 0 for m in IDS}

    def log(self, **ev):
        if self.events:
            ev["mono_ns"] = time.monotonic_ns()
            self.events.write(json.dumps(ev) + "\n")

    def send(self, msg: str, data: bytes):
        self.sock.send(struct.pack(CAN_FRAME_FMT, IDS[msg], 8, data))

    def run(self, scenario: str, duration: float | None = None):
        scn = SCENARIOS[scenario]
        duration = duration or scn["duration"]
        t0 = time.monotonic()
        due = {m: t0 + OFFSET_S[m] for m in IDS}
        seen_phases = set()
        bursts_done = set()
        self.log(event="start", scenario=scenario, duration=duration)

        while True:
            msg = min(due, key=due.get)
            when = due[msg]
            if when - t0 >= duration:
                break
            delay = when - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            t = when - t0

            phases = active_phases(scn, t)
            for p in scn["phases"]:
                if p["name"] not in seen_phases and p["start"] <= t:
                    seen_phases.add(p["name"])
                    self.log(event="phase_begin", name=p["name"], t=p["start"])
                if p["name"] in seen_phases and t >= p["end"] and \
                        (p["name"] + ":end") not in seen_phases:
                    seen_phases.add(p["name"] + ":end")
                    self.log(event="phase_end", name=p["name"], t=p["end"])

            raw = raw_from_physical(scn["cycle"](t))
            for p in phases:
                if "burst" in p and p["name"] not in bursts_done:
                    bursts_done.add(p["name"])
                    self.burst("M1", raw, p["burst"])

            drop = any(msg in p.get("drop", []) for p in phases)
            corrupt = any(msg in p.get("corrupt", []) for p in phases)
            freeze = any(msg in p.get("freeze_counter", []) for p in phases)
            for p in phases:
                raw.update(p.get("raw", {}).get(msg, {}))

            if not drop:
                if not freeze:
                    self.counter[msg] = (self.counter[msg] + 1) & 0x0F
                self.send(msg, pack(msg, raw, self.counter[msg], corrupt))
                self.log(event="tx", msg=msg, t=round(t, 4),
                         counter=self.counter[msg], corrupt=corrupt,
                         frozen=freeze)
            period = PERIOD_S[msg]
            for p in phases:
                period = p.get("period", {}).get(msg, period)
            due[msg] += period

        self.log(event="end")
        if self.events:
            self.events.close()

    def burst(self, msg: str, raw: dict, n: int):
        """n frames back to back, valid and in sequence: a babbling sender."""
        for _ in range(n):
            self.counter[msg] = (self.counter[msg] + 1) & 0x0F
            try:
                self.send(msg, pack(msg, raw, self.counter[msg]))
            except OSError:
                break  # socket buffer full: the bus itself is saturated
        self.log(event="burst", msg=msg, frames=n)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--iface", default="vcan0")
    ap.add_argument("--scenario", default="nominal", choices=sorted(SCENARIOS))
    ap.add_argument("--duration", type=float, default=None)
    ap.add_argument("--events", default=None, help="JSON lines output")
    ap.add_argument("--list", action="store_true", help="list scenarios")
    args = ap.parse_args(argv)

    if args.list:
        for name, scn in sorted(SCENARIOS.items()):
            print(f"{name:10s} {scn['duration']:5.1f} s  "
                  + ", ".join(p["name"] for p in scn["phases"]))
        return 0

    Simulator(args.iface, args.events).run(args.scenario, args.duration)
    return 0


if __name__ == "__main__":
    sys.exit(main())
