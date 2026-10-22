# Test evidence

Osvaldo Janeri Filho <janeri@gmail.com>\
Project #2 — Automotive Instrument Cluster Module Using Trampoline RTOS

Everything below is console output from real runs, produced by
`tools/collect_evidence.sh` and kept verbatim in [`docs/evidence/`](../docs/evidence/).
Nothing here is typed by hand except the prose and the choice of excerpts;
each excerpt names the file it comes from.

## Environment

From [`00_environment.txt`](../docs/evidence/00_environment.txt):

```text
date:     2026-10-22 23:06:49 UTC
host:     Linux 6.8.0-101-generic x86_64
os:       Ubuntu 24.04.4 LTS
cpu:      AMD EPYC Processor x 8
load:     11.94 14.13 15.55
gcc:      gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
cmake:    cmake version 3.28.3
python:   Python 3.12.3
goil:     /root/icm-work/trampoline/goil/makefile-unix/goil : 3.1.16, build with GALGAS 3.7.0
trampoline commit: 09e2dcb1206fb6ebaa73c84558f26ac21f57b2dd
icm commit:        8871bdf5ff37733fbc1d3a9cf3b2406f8843f456
can interface:     vcan0            UNKNOWN        <NOARP,UP,LOWER_UP>
```

Note the load average: about 12 on 8 cores. The machine is a shared server
running other jobs the whole time. Every run below was made under those
conditions; the cluster ran under SCHED_FIFO (`chrt -f 50`) except where it
says SCHED_OTHER.

## 1. Unit tests (host, ASan + UBSan)

[`01_unit_tests.txt`](../docs/evidence/01_unit_tests.txt), 4 suites, 25 tests,
373 checks:

```text
1: [  OK  ] test_ICM_S_03_crc8_check_value
1: [  OK  ] test_ICM_F_01_to_04_golden_frames_decode
1: [  OK  ] test_ICM_F_01_encode_decode_roundtrip
1: [  OK  ] test_ICM_S_03_every_single_bit_flip_is_rejected
1: [  OK  ] test_ICM_S_03_crc_covers_the_identifier
1: [  OK  ] test_ICM_S_03_bad_dlc_and_unknown_id
1: [  OK  ] test_ICM_S_03_alive_counter
1: 97 checks, 0 failed
2: [  OK  ] test_ICM_S_02_speed_range_boundaries
2: [  OK  ] test_ICM_S_02_rpm_range_boundaries
2: [  OK  ] test_ICM_S_02_fuel_and_temp_boundaries
2: [  OK  ] test_ICM_F_05_scaling
2: [  OK  ] test_ICM_S_01_timeout_boundary_and_wrap
2: [  OK  ] test_ICM_S_01_status_combination
2: [  OK  ] test_ICM_F_06_lowpass_filter
2: [  OK  ] test_ICM_F_08_warning_hysteresis
2: 49 checks, 0 failed
3: [  OK  ] test_ICM_S_04_confirm_after_n_failures
3: [  OK  ] test_ICM_S_04_intermittent_fault_does_not_confirm
3: [  OK  ] test_ICM_S_04_heal_after_m_passes
3: [  OK  ] test_ICM_S_04_dtc_table
3: 190 checks, 0 failed
4: [  OK  ] test_ICM_F_07_fixed_width_frame
4: [  OK  ] test_ICM_F_07_shows_values
4: [  OK  ] test_ICM_S_05_invalid_signal_shows_dashes
4: [  OK  ] test_ICM_F_07_blink_rate
4: [  OK  ] test_ICM_S_05_body_loss_darkens_indicators
4: [  OK  ] test_ICM_F_08_warning_lamps
4: 37 checks, 0 failed
100% tests passed, 0 tests failed out of 4
```

## 2. Static checks

[`02_static_checks.txt`](../docs/evidence/02_static_checks.txt):

```text
shared-access check: ICM_Data is only reachable through icm_data.c accessors; every accessor is balanced; every user task declares ICM_Resource in icm.oil
traceability: 28 requirements, all covered; no test names a requirement that does not exist
```

The full requirement × test matrix is
[`02b_traceability_matrix.md`](../docs/evidence/02b_traceability_matrix.md).

## 3. GOIL and build

Each variant goes through GOIL (OIL → OS configuration) and the generated build
([`03_build_nominal.txt`](../docs/evidence/03_build_nominal.txt) and siblings).
GOIL's verdict on `src/icm.oil` is `No warning, no error.` for all five.

## 4. Integration suite

[`04_integration_tests.txt`](../docs/evidence/04_integration_tests.txt):

```text
Ran 46 tests in 148.593s

OK
```

The sections below are the numbers behind those 46 verdicts.

## 5. Timing, nominal traffic (ICM-T-01..07)

[`05_timing_nominal-nominal.md`](../docs/evidence/05_timing_nominal-nominal.md),
31 s, 30 s drive cycle on the bus, SCHED_FIFO:

| SystemCounter tick | value |
|---|---|
| ticks recorded | 3100 |
| mean period | 10.0480 ms (+0.48 % vs 10 ms) |
| stdev / min / p99 / max | 0.043 / 9.268 / 10.162 / 10.895 ms |

| Task | T (ms) | jobs | release gaps (ticks) | started in release tick | wall period mean / min / max (ms) | start delay p99 (us) | response mean / p99 / max (us) | exec mean / max (us) | misses |
|---|---|---|---|---|---|---|---|---|---|
| Task_CAN_RX | sporadic (D=10) | 420 | - | - | - | irq p99 139.1 | 52.4 / 148.3 / 157.8 | 4.5 / 31.8 | 0 |
| Task_Acquisition | 10 | 3090 | 1x3089 | 100.0 % | 10.048 / 9.269 / 10.895 | 17.3 | 9.9 / 30.0 / 133.5 | 4.9 / 127.7 | 0 |
| Task_Processing | 20 | 1545 | 2x1544 | 100.0 % | 20.096 / 19.894 / 20.954 | 32.5 | 16.9 / 37.6 / 141.5 | 4.0 / 26.1 | 0 |
| Task_Display | 50 | 618 | 5x617 | 100.0 % | 50.240 / 49.922 / 51.082 | 38.9 | 72.7 / 139.9 / 319.9 | 55.9 / 174.9 | 0 |
| Task_Diagnostics | 100 | 309 | 10x308 | 100.0 % | 100.480 / 100.260 / 101.350 | 167.4 | 88.6 / 177.3 / 202.0 | 9.4 / 49.5 | 0 |
| Task_Logging | 1000 | 31 | 100x30 | 100.0 % | 1004.787 / 1003.701 / 1005.995 | 196.4 | 135.6 / 209.9 / 209.9 | 13.7 / 36.2 | 0 |

Utilisation U = 2.869 % (Liu & Layland bound for n = 6: 73.5 %)

| Task | C (ms) | B (ms) | R (ms) | D (ms) | R <= D |
|---|---|---|---|---|---|
| Task_CAN_RX | 0.032 | 0.175 | 0.207 | 10 | yes |
| Task_Acquisition | 0.128 | 0.175 | 0.334 | 10 | yes |
| Task_Processing | 0.026 | 0.175 | 0.360 | 20 | yes |
| Task_Display | 0.175 | 0.050 | 0.410 | 50 | yes |
| Task_Diagnostics | 0.050 | 0.036 | 0.446 | 100 | yes |
| Task_Logging | 0.036 | 0.000 | 0.446 | 1000 | yes |

How to read it:

- **Release gaps** `1x3089` means all 3089 gaps between consecutive releases
  of Task_Acquisition were exactly 1 tick. Every periodic task shows a single
  gap value equal to its period: the alarms never skipped and never
  double-fired. **Started in release tick: 100 %**: no job ever waited into
  the next tick.
- **Wall period** is that same schedule seen through CLOCK_MONOTONIC. The
  +0.48 % on every task is ViPER's tick drift (10.048 ms per tick), not a
  scheduling effect: the ratio is the same for every period.
- **Response** is from the tick that released the job to the job's last
  instruction, which includes waiting for higher-priority tasks; the worst,
  0.32 ms for Task_Display, is 0.6 % of its 50 ms deadline.
- **Task_CAN_RX** response is from the controller taking the frame off the
  bus to the values being in ICM_Data: 0.158 ms worst case, deadline 10 ms.
- The **RTA** uses the worst measured execution times and the pessimistic
  blocking bound; every R is far below its D.

### The same run without SCHED_FIFO

[`05_timing_nominal-sched_other.md`](../docs/evidence/05_timing_nominal-sched_other.md):

| SystemCounter tick | value |
|---|---|
| ticks recorded | 3100 |
| mean period | 10.1819 ms (+1.82 % vs 10 ms) |
| stdev / min / p99 / max | 0.675 / 6.280 / 12.651 / 23.212 ms |

| Task | ... | started in release tick | wall period mean / min / max (ms) | ... | response mean / p99 / max (us) | ... | misses |
|---|---|---|---|---|---|---|---|
| Task_CAN_RX | | - | - | | 107.9 / 1346.1 / 2915.9 (irq p99 1336.7) | | 0 |
| Task_Acquisition | | 100.0 % | 10.182 / 6.280 / 23.213 | | 10.3 / 31.2 / 120.1 | | 0 |

Same code, same scenario, and still zero misses and 100 % of the jobs on
their release tick: in the OS's own time base the schedule is identical. What
changes is the host: one tick arrived 23 ms after the previous one, and the
interrupt latency tail grows tenfold (p99 0.14 → 1.34 ms). That is the Linux
scheduler deciding when the Trampoline process runs, on a machine with a load
of 12, and it is the clearest statement of what the POSIX target can and
cannot show: **the RTOS's determinism is measurable here, the hardware's is
not**.

## 6. Fault handling (ICM-S-01..06)

Fault scenario, [`06_events_nominal-faults.txt`](../docs/evidence/06_events_nominal-faults.txt)
(`t` is cluster uptime; the simulator started ≈ 0.05 s after it):

```text
ICM READY mono_ns=7301157606445260 can=online race_window_us=0
DIAG t=7.142 mono_ns=7301164748216522 DTC 0x02 M2_VEHICLE_SPEED_TIMEOUT CONFIRMED
DIAG t=10.960 mono_ns=7301168565837270 DTC 0x02 M2_VEHICLE_SPEED_TIMEOUT HEALED
DIAG t=11.261 mono_ns=7301168867318240 DTC 0x12 RPM_OUT_OF_RANGE CONFIRMED
DIAG t=14.978 mono_ns=7301172584515947 DTC 0x12 RPM_OUT_OF_RANGE HEALED
DIAG t=15.280 mono_ns=7301172886231537 DTC 0x11 SPEED_OUT_OF_RANGE CONFIRMED
DIAG t=17.993 mono_ns=7301175599000195 DTC 0x11 SPEED_OUT_OF_RANGE HEALED
DIAG t=18.294 mono_ns=7301175900452210 DTC 0x21 CAN_E2E_ERROR CONFIRMED
DIAG t=20.304 mono_ns=7301177909899987 DTC 0x04 M4_BODY_STATUS_TIMEOUT CONFIRMED
DIAG t=21.810 mono_ns=7301179416692527 DTC 0x04 M4_BODY_STATUS_TIMEOUT HEALED
DIAG t=22.714 mono_ns=7301180320279590 DTC 0x21 CAN_E2E_ERROR HEALED
DIAG t=22.814 mono_ns=7301180420701799 DTC 0x04 M4_BODY_STATUS_TIMEOUT CONFIRMED
DIAG t=25.526 mono_ns=7301183132569575 DTC 0x04 M4_BODY_STATUS_TIMEOUT HEALED
DIAG t=32.255 mono_ns=7301189861635273 DTC 0x04 M4_BODY_STATUS_TIMEOUT CONFIRMED
DIAG t=33.059 mono_ns=7301190665755829 DTC 0x01 M1_ENGINE_SPEED_TIMEOUT CONFIRMED
SUMMARY snapshots=1020 torn=0 race_window_us=0
SUMMARY can_pushed=623 can_hw_dropped=0 can_drained=623 can_sw_dropped=0 can_popped=623
SUMMARY task=Task_CAN_RX jobs=622 deadline_misses=0 os_overruns=0 max_response_us=199.4 max_exec_us=24.2
...
SUMMARY os_api_errors=0 last_error=0 last_service=0 ticks=3300
```

The last two lines are not faults of the scenario: the simulator stops at
32 s and the cluster keeps running until 33 s, so M4 and then M1 time out, as
they should.

End-to-end latencies, frame on the bus (simulator's CLOCK_MONOTONIC stamp) to
DTC confirmed (cluster's stamp, same clock), computed from the run directory:

```text
M2 timeout: last M2 at sim t=4.003 -> CONFIRMED after 3.102 s
M2 heal: first M2 back at t=10.003 -> HEALED after 0.920 s
RPM 9000: first bad M1 at t=11.000 -> CONFIRMED after 0.224 s
Speed 300: first bad M2 at t=15.003 -> CONFIRMED after 0.240 s
CRC: 2 corrupt M3 frames sent, cluster crc counter = 2; CONFIRMED after 0.250 s
Frozen counter: 8 M4 frames, cluster cnt = 8; M4 TIMEOUT CONFIRMED 0.258 s after first frozen frame
M4 timeout: last M4 at t=22.409 -> CONFIRMED after 0.369 s
burst: M1 rx=264/27/30/292 ovr=0
```

| Fault | Requirement | Expected | Measured |
|---|---|---|---|
| M2 lost (1000 ms message) | ICM-S-01 | > 3 s (3 periods), ≤ 3.3 s | 3.102 s |
| M2 back | ICM-S-04 | heal after 10 good cycles (1 s) | 0.920 s |
| 9000 rpm | ICM-S-02 | 3 cycles (0.3 s) + one acquisition | 0.224 s |
| 300 km/h | ICM-S-02 | idem | 0.240 s |
| bad CRC on M3 | ICM-S-03 | every bad frame counted | 2 sent, 2 counted |
| frozen alive counter on M4 | ICM-S-03 | every stale frame discarded, then M4 TIMEOUT | 8 sent, 8 discarded; timeout 0.258 s |
| M4 lost (100 ms message) | ICM-S-01 | > 0.3 s, ≤ 0.55 s | 0.369 s |
| 200-frame burst | ICM-S-06 | no loss | 200 extra M1 received, 0 lost |

The 0.920 s heal is under the nominal 1 s because the first diagnostics cycle
after the frame (at most 100 ms later, the cycle is not aligned with the bus)
already counts as passing cycle 1; the tenth comes 0.9–1.0 s after the frame.

Why 0.224 s for a 3-cycle (300 ms) debounce: the first bad value is seen on
the first diagnostics cycle after reception, which counts as cycle 1; cycles 2
and 3 follow at 100 ms each. So the bound is 200–300 ms after the frame.

### What the driver saw

[`07_dashboard_frames.txt`](../docs/evidence/07_dashboard_frames.txt), frames
taken from the same run's log:

```text
=== simulator t = 9.0 s: M2 lost
+----------------------------------------------------------------------+
| ICM Instrument Cluster - Trampoline OSEK/VDX         t=    9.15 s    |
+----------------------------------------------------------------------+
|                                                                      |
| SPEED [????????????????????????????????????????]    --- km/h         |
| RPM   [############............................]   2353 rpm          |
| FUEL  [#############.......]  64.7 %  TEMP [###.......]   63 C       |
| LAMPS  ...   ...   ...   ...   ...   ...   ....   ....               |
|        ....   ....  [FAULT]                                          |
| MSG   COMM LOST: VEHICLE SPEED (M2)                                  |
|                                                                      |
|                                                                      |
+----------------------------------------------------------------------+
| CAN rx 19/5/10/92  crc 0  cnt 0  ovr 0                               |
| RTOS overruns 0  deadline misses 0  api errors 0  torn 0             |
+----------------------------------------------------------------------+

=== simulator t = 13.0 s: rpm out of range
+----------------------------------------------------------------------+
| ICM Instrument Cluster - Trampoline OSEK/VDX         t=   13.17 s    |
+----------------------------------------------------------------------+
|                                                                      |
| SPEED [##################......................]  114.9 km/h         |
| RPM   [????????????????????????????????????????]    --- rpm          |
| FUEL  [#############.......]  64.6 %  TEMP [####......]   73 C       |
| LAMPS  ...   ...   ...   ...   ...   ...   ....   ....               |
|        ....   ....  [FAULT]                                          |
| MSG   SENSOR SIGNAL OUT OF RANGE                                     |
|                                                                      |
|                                                                      |
+----------------------------------------------------------------------+
| CAN rx 27/9/14/132  crc 0  cnt 0  ovr 0                              |
| RTOS overruns 0  deadline misses 0  api errors 0  torn 0             |
+----------------------------------------------------------------------+
```

The engine was at 9000 rpm on the bus; the tachometer says `---`, not 9000
and not 0 (ICM-S-05).

## 7. Race condition test and negative control (ICM-D-01..04)

[`06_events_race_locked-nominal.txt`](../docs/evidence/06_events_race_locked-nominal.txt)
and [`06_events_race_unlocked-nominal.txt`](../docs/evidence/06_events_race_unlocked-nominal.txt),
20 s each, a 12 ms window in the middle of Task_Display's copy of ICM_Data:

```text
race_locked:   SUMMARY snapshots=617 torn=0 race_window_us=12000
race_unlocked: SUMMARY snapshots=617 torn=199 race_window_us=12000
```

617 snapshots in each run, about 400 of them Task_Display's slow ones. With
ICM_Resource: none torn. Without it: 199, half of the slow copies, as
predicted (architecture.md §5). The race exists, the check sees it, and the
resource removes it.

The price of the lock, in the locked run
([`05_timing_race_locked-nominal.md`](../docs/evidence/05_timing_race_locked-nominal.md)):

| Task | started in release tick | start delay p99 (us) | response max (us) | misses |
|---|---|---|---|---|
| Task_CAN_RX | - | - | 11910.7 | 11 |
| Task_Acquisition | 100.0 % | 2034.5 | 2446.6 | 0 |
| Task_Processing | 100.0 % | 2040.2 | 2216.2 | 0 |
| Task_Display | 100.0 % | 41.5 | 12283.6 | 0 |
| Task_Diagnostics | 0.0 % | 12176.7 | 12276.8 | 0 |

While Display holds the resource for 12 ms, everything below the ceiling
waits: Acquisition and Processing up to 2.4 ms (they are released on the next
tick, 10 ms into the window), Diagnostics and Logging for the whole window
(they are released on the same tick as Display and come after it). Task_CAN_RX,
whose deadline is 10 ms, misses it 11 times: a 12 ms blocking term is larger
than its deadline, the response-time analysis says so (B = 12.2 ms > D), and
the deadline monitor reports it. The product's critical sections are one
`memcpy`; this run is the reason.

## 8. Overload and isolation (ICM-T-08, ICM-T-09)

[`05_timing_overload-nominal.md`](../docs/evidence/05_timing_overload-nominal.md):
Task_Diagnostics spins 150 ms per job between t = 5 s and 10 s.

| Task | release gaps (ticks) | started in release tick | response max (us) | misses |
|---|---|---|---|---|
| Task_CAN_RX | - | - | 83.0 | 0 |
| Task_Acquisition | 1x1789 | 100.0 % | 72.4 | 0 |
| Task_Processing | 2x894 | 100.0 % | 79.4 | 0 |
| Task_Display | 5x357 | 100.0 % | 182.6 | 0 |
| Task_Diagnostics | **10x128, 20x25** | 100.0 % | 150121.1 | **25** |
| Task_Logging | 100x17 | 72.2 % | 49773.9 | 0 |

```text
DIAG t=5.173 mono_ns=7301196055298396 DTC 0x31 TASK_DEADLINE_OVERRUN CONFIRMED
DIAG t=15.067 mono_ns=7301205948679102 DTC 0x31 TASK_DEADLINE_OVERRUN HEALED
SUMMARY task=Task_Diagnostics jobs=154 deadline_misses=25 os_overruns=25 max_response_us=150121.1 max_exec_us=149788.4
```

The 25 overruns are visible three ways that agree: 25 `E_OS_LIMIT` caught by
ErrorHook (`os_overruns=25`), 25 measured responses over 100 ms
(`deadline_misses=25`), and 25 release gaps of 20 ticks instead of 10 in the
trace (each lost activation leaves a hole in the grid). Every task above
Diagnostics is untouched. Task_Logging, the only one below it, starts up to
50 ms late, as fixed-priority scheduling says it may, and still finishes far
inside its 1000 ms.

## 9. Receive overrun (ICM-S-06)

[`06_events_irq_lock-babble.txt`](../docs/evidence/06_events_irq_lock-babble.txt):
all interrupts locked for 200 ms at t = 3 s while M1 arrives every 1 ms.

```text
DIAG t=3.214 mono_ns=7301212244595936 DTC 0x22 CAN_RX_OVERRUN CONFIRMED
DIAG t=3.214 mono_ns=7301212244630942 DTC 0x31 TASK_DEADLINE_OVERRUN CONFIRMED
DIAG t=5.103 mono_ns=7301214133109846 DTC 0x22 CAN_RX_OVERRUN HEALED
SUMMARY can_pushed=3908 can_hw_dropped=196 can_drained=3908 can_sw_dropped=0 can_popped=3908
SUMMARY task=Task_CAN_RX jobs=3879 deadline_misses=1 os_overruns=0 max_response_us=189646.6 max_exec_us=105.8
```

4104 frames were sent (simulator log). The controller accepted 3908 and
dropped 196 with its FIFO full; 3908 were drained by the ISR and popped by
the task: **3908 + 196 = 4104, nothing lost silently**. The frames that
waited out the lock were 190 ms late, which the Task_CAN_RX deadline monitor
caught (DTC 0x31).

The same run shows the limitation recorded in requirements §7: during the
lock no tick is counted either, so Task_Acquisition's wall-clock period peaks
at 200.1 ms
([`05_timing_irq_lock-babble.md`](../docs/evidence/05_timing_irq_lock-babble.md))
while its release grid in ticks stays exact (`1x889`). Twenty ticks became one.

## 10. Summary

| Requirement family | Evidence | Result |
|---|---|---|
| ICM-F (functional) | unit tests, `test_signals.py`, `test_warnings.py`, `test_dbc_consistency.py` | pass |
| ICM-T (timing) | §5, §8, §9 | 0 misses in nominal and fault runs; overruns detected and isolated |
| ICM-S (faults) | §6, §9 | every fault detected within its bound, every lost frame accounted for |
| ICM-D (data) | §2, §7 | 0 torn copies with the resource, 199 without |
