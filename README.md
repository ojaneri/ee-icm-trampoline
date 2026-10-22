# ICM — Instrument Cluster Module on Trampoline RTOS

Project #2 — Automotive Instrument Cluster Module Using Trampoline RTOS\
Sistemas Embarcados, PPGEE UTFPR Ponta Grossa — Prof. Max Mauro Dias Santos\
Osvaldo Janeri Filho <janeri@gmail.com>

[![CI](https://github.com/ojaneri/ee-icm-trampoline/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/ojaneri/ee-icm-trampoline/actions/workflows/ci.yml)

The badge tracks `main`. It goes green when the unit tests pass under the
sanitizers, the static checks pass, and the whole RTOS integration suite
passes on a virtual CAN bus on a GitHub runner: timing, faults, the race test
and its negative control, overload and overrun.

An automotive instrument cluster prototype with **deterministic real-time
control**: six OSEK tasks on [Trampoline](https://github.com/TrampolineRTOS/trampoline)
(OSEK/VDX, API aligned with AUTOSAR OS 4.2), CAN input with interrupt and
event, a shared data structure under the priority ceiling protocol, fault
diagnosis, and a test suite that measures what it claims: periods, jitter,
response times, deadline misses, detection latencies, and the absence of
race conditions, with a negative control that shows the race when the lock is
removed.

There is no hardware. Trampoline runs on its POSIX target inside a Linux
process, CAN is a SocketCAN virtual interface fed by a simulator, and the
dashboard is a text frame. Everything that this changes about the results is
listed in [requirements §7](requirements/icm_requirements.md#7-platform-assumptions-and-limits-of-the-simulation)
and in the report.

A frame from a real run (the fault scenario, while M3 arrives with a bad CRC;
copied from `build/runs/nominal-faults/log.txt`):

```
+----------------------------------------------------------------------+
| ICM Instrument Cluster - Trampoline OSEK/VDX         t=   19.20 s    |
+----------------------------------------------------------------------+
|                                                                      |
| SPEED [###################.....................]  120.0 km/h         |
| RPM   [################........................]   3120 rpm          |
| FUEL  [#############.......]  64.2 %  TEMP [#####.....]   82 C       |
| LAMPS  ...   ...   ...   ...   ...   ...   ....   ....               |
|        ....   ....  [FAULT]                                          |
| MSG   CAN DATA CORRUPTED (E2E)                                       |
|                                                                      |
|                                                                      |
+----------------------------------------------------------------------+
| CAN rx 39/15/18/192  crc 2  cnt 0  ovr 0                             |
| RTOS overruns 0  deadline misses 0  api errors 0  torn 0             |
+----------------------------------------------------------------------+
```

## Architecture in one picture

```
 sim/can_sim.py ──vcan0──> virtual CAN controller ──IRQ──> ISR_CAN_RX ──EV_CAN_RX──> Task_CAN_RX   (event, prio 6)  receive
  M1 0x100  500 ms         (filter, 8-frame FIFO)          (cat. 2)                       │
  M2 0x200 1000 ms                                                                       ▼
  M3 0x300 1000 ms                                                          ICM_Data  [ICM_Resource, ceiling]
  M4 0x400  100 ms                                                                       ▲
                     Task_Acquisition 10 ms (5) validate · Task_Processing 20 ms (4) scale+filter
                     Task_Display 50 ms (3) display · Task_Diagnostics 100 ms (2) DTCs, deadlines
                     Task_Logging 1000 ms (1)
```

Priorities are rate-monotonic, with the CAN receive task above all periodic
tasks. Why each number is what it is, and why there is no race on ICM_Data,
is in [requirements/architecture.md](requirements/architecture.md).

## The professor's deliverables, and where they are

| # | Deliverable | Where |
|---|---|---|
| 1 | ICM requirements and signal dictionary | [requirements/icm_requirements.md](requirements/icm_requirements.md), [dbc/icm.dbc](dbc/icm.dbc) |
| 2 | Tasks, events, alarms, ISR and resources | [requirements/architecture.md](requirements/architecture.md) |
| 3 | OIL configuration and Trampoline application | [src/icm.oil](src/icm.oil), [src/icm_app.c](src/icm_app.c) and the modules next to it |
| 4 | CAN / sensor input simulation | [sim/can_sim.py](sim/can_sim.py), [src/can_ctrl.c](src/can_ctrl.c) |
| 5 | Functional automotive dashboard | [src/dashboard.c](src/dashboard.c) |
| 6 | Timing, deadline and fault-handling tests | [tests/](tests/), [requirements/test_evidence.md](requirements/test_evidence.md) |
| 7 | GitHub repository, technical report, demo | this repository, [docs/relatorio_tecnico.md](docs/relatorio_tecnico.md), "Running the demo" below |

## Layout

```
src/
  icm.oil             OSEK OIL configuration (tasks, events, alarms, ISR, resources, counter)
  icm_app.c           task, ISR and hook bodies
  icm_data.[ch]       ICM_Data and the only functions that touch it (ICM_Resource)
  icm_types.h         the ICM_Data structure, split by writer
  can_ctrl.[ch]       virtual CAN controller: SocketCAN thread, FIFO, overrun, IRQ
  can_db.[ch]         frame layout, CRC-8 J1850, alive counter      (pure C)
  signal_proc.[ch]    validate, scale, filter, hysteresis            (pure C)
  diag.[ch]           DTC debouncing                                 (pure C)
  dashboard.[ch]      text cluster renderer                          (pure C)
  icm_trace.[ch]      timing trace: releases, response, execution time
  icm_config.h        every number, next to the requirement it implements
sim/can_sim.py        the other ECUs: drive cycle and fault scenarios on vcan0
dbc/icm.dbc           the signal dictionary for CAN tools
requirements/
  icm_requirements.md requirements ICM-F/T/S/D and signal dictionary
  architecture.md     OS objects, priorities, race-freedom argument, RTA
  test_evidence.md    console output of the real runs
tests/
  unit/               host tests of the pure modules (CTest, ASan/UBSan)
  integration/        RTOS + bus runs: timing, signals, faults, warnings,
                      race (with negative control), overload, overrun, DBC
tools/
  setup_trampoline.sh fetch Trampoline at a pinned commit, build GOIL and ViPER
  build.sh            GOIL + compile one variant (nominal and four test builds)
  analyze_timing.py   trace -> periods, jitter, response times, RTA
  check_shared_access.py  static proof that ICM_Data has one door
  check_traceability.py   every requirement has a test, every test a requirement
  collect_evidence.sh run everything, keep the output in docs/evidence/
docs/
  relatorio_tecnico.md    technical report (pt-BR)
  evidence/               raw console output quoted by the evidence document
```

## Running it

Linux (or WSL2 with a kernel that has `vcan`), GCC, Python 3.10+, CMake 3.14+.

```bash
# 1. Trampoline toolchain, pinned (about a minute; GOIL is compiled from source)
tools/setup_trampoline.sh

# 2. host unit tests
cmake -S . -B build/unit && cmake --build build/unit && ctest --test-dir build/unit

# 3. the cluster, through GOIL
tools/build.sh nominal

# 4. a virtual CAN bus
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0

# 5. integration tests (all five build variants first)
for v in nominal race_locked race_unlocked overload irq_lock; do tools/build.sh $v; done
pip install cantools            # optional, for the DBC consistency test
cd tests/integration && python3 -m unittest -v
```

Run the integration tests as root (or set `ICM_RT_PREFIX="sudo -E chrt -f 50"`)
to get SCHED_FIFO; without it they still pass, with a longer latency tail.

### Running the demo

Two terminals:

```bash
# terminal 1: the cluster, live dashboard (q to quit)
VIPER_PATH=third_party/trampoline/viper build/nominal/icm_exe

# terminal 2: the other ECUs
python3 sim/can_sim.py --scenario faults     # or nominal, warnings; --list
```

`ICM_DASH=plain` prints one frame per second instead of redrawing;
`ICM_CAN_IF=can0` points the cluster at a real bus (a USB-CAN adapter such as
a CANable shows up as a SocketCAN interface).

## Results

From the runs in [requirements/test_evidence.md](requirements/test_evidence.md)
(Ubuntu 24.04, 8-core x86-64 server, SCHED_FIFO):

| | |
|---|---|
| Unit tests | 4 suites, all pass under ASan + UBSan |
| Integration tests | 46, all pass |
| Requirements covered | 28 of 28 |
| Periodic releases on the exact tick | 100 % of jobs, every task |
| Deadline misses, nominal and fault runs | 0 |
| CAN frame → ICM_Data, worst case | 0.2 ms (deadline 10 ms) |
| CPU utilisation | ≈ 1.5 % (Liu & Layland bound for 6 tasks: 73.5 %) |
| M2 loss detected after last frame | 3.0–3.3 s (timeout: 3 periods = 3 s) |
| Torn ICM_Data copies with / without the resource | 0 / 199 (of ~400 slow copies) |

## Team workflow

Issues → branches → commits → pull requests → review → merge, as in the
professor's diagram. CI runs the unit tests, the static checks and the whole
RTOS integration suite on a virtual CAN bus on every push and pull request.
