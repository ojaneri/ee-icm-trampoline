# ICM requirements and signal dictionary

Osvaldo Janeri Filho · Norton Dezonet (@dezonet) · Vinicius Antunes\
Project #2 — Automotive Instrument Cluster Module Using Trampoline RTOS\
Sistemas Embarcados, PPGEE UTFPR Ponta Grossa — Prof. Max Mauro Dias Santos

The system is an instrument cluster (ICM): it receives vehicle signals over
CAN, validates, scales and filters them, and shows them to the driver, all
under a deterministic real-time schedule on Trampoline (OSEK/VDX, aligned with
AUTOSAR OS 4.2). This file is the contract the code and the tests are held
to. Every requirement has an identifier; every test names the identifiers it
covers; `tools/check_traceability.py` fails CI if a requirement has no test or
a test names a requirement that is not here.

Four families:

- **ICM-F** functional: what the cluster receives and shows
- **ICM-T** timing: periods, deadlines, overrun handling
- **ICM-S** safety and fault handling: timeouts, ranges, integrity, DTCs
- **ICM-D** data and synchronisation: the shared data structure

Where a number appears, the constant that implements it lives in
`src/icm_config.h` under a name close to the requirement's wording.

## 1. Context and scope

The professor's brief fixes two CAN messages: **M1 Engine Speed every
500 ms** and **M2 Vehicle Speed every 1000 ms**. A cluster also needs fuel
level, coolant temperature, turn signals and warning lamps, and the brief
lists them as vehicle inputs without assigning them to messages. We added two
messages for them, **M3 Engine Status** and **M4 Body Status**, and say so
wherever they appear: they are our design, not the brief's.

There is no hardware. The cluster runs on Trampoline's POSIX target, inside a
Linux process; CAN is a SocketCAN virtual interface (`vcan0`) fed by
`sim/can_sim.py`; the display is a text frame on the terminal. Section 7 lists
what that simulation can and cannot stand in for.

## 2. Signal dictionary

All frames are classic CAN 2.0A (11-bit identifiers), DLC 8, Intel (little
endian) byte order. The same dictionary in machine-readable form is
`dbc/icm.dbc`, usable with cantools, SavvyCAN or any CAN tool, including on a
real bus through a USB-CAN adapter such as a CANable.

### 2.1 Messages

| Msg | CAN id | Name | Sender (role) | Period | Timeout (3 × period) | Source |
|---|---|---|---|---|---|---|
| M1 | 0x100 | EngineSpeed | ECM (engine) | 500 ms | 1500 ms | professor |
| M2 | 0x200 | VehicleSpeed | ABS (wheel speed) | 1000 ms | 3000 ms | professor |
| M3 | 0x300 | EngineStatus | ECM (engine) | 1000 ms | 3000 ms | ours |
| M4 | 0x400 | BodyStatus | BCM (body) | 100 ms | 300 ms | ours |

Until a message has been received for the first time, its timeout is at
least 2000 ms (ICM-S-01b).

### 2.2 Common frame layout

| Byte | Content |
|---|---|
| 0..5 | signals, per message (2.3) |
| 6 | bits 0..3: alive counter, +1 per frame, wraps 15 → 0; bits 4..7: 0 |
| 7 | CRC-8 SAE J1850 (poly 0x1D, init 0xFF, final XOR 0xFF) over `[id & 0xFF, id >> 8, byte0 .. byte6]` |

Including the identifier in the CRC is the "Data ID" idea of AUTOSAR E2E
Profile 1: a frame delivered under the wrong identifier fails the check rather
than being decoded as another message.

### 2.3 Signals

| Signal | Msg | Bits | Type | Factor | Offset | Unit | Valid physical range | Valid raw range | SNA (not available) |
|---|---|---|---|---|---|---|---|---|---|
| EngineSpeed | M1 | 0..15 | uint16 | 0.25 | 0 | rpm | 0 .. 8000 | 0 .. 32000 | 0xFFFF |
| VehicleSpeed | M2 | 0..15 | uint16 | 0.01 | 0 | km/h | 0 .. 250 | 0 .. 25000 | 0xFFFF |
| FuelLevel | M3 | 0..7 | uint8 | 0.5 | 0 | % | 0 .. 100 | 0 .. 200 | 0xFF |
| CoolantTemp | M3 | 8..15 | uint8 | 1 | −40 | °C | −40 .. 150 | 0 .. 190 | 0xFF |
| TurnLeft | M4 | 0 | bit | | | | 0/1 | | |
| TurnRight | M4 | 1 | bit | | | | 0/1 | | |
| Hazard | M4 | 2 | bit | | | | 0/1 | | |
| HighBeam | M4 | 3 | bit | | | | 0/1 | | |
| WarnOilPressure | M4 | 8 | bit | | | | 0/1 | | |
| WarnBattery | M4 | 9 | bit | | | | 0/1 | | |
| WarnBrake | M4 | 10 | bit | | | | 0/1 | | |
| WarnCheckEngine | M4 | 11 | bit | | | | 0/1 | | |
| WarnABS | M4 | 12 | bit | | | | 0/1 | | |
| WarnAirbag | M4 | 13 | bit | | | | 0/1 | | |
| WarnSeatbelt | M4 | 14 | bit | | | | 0/1 | | |
| WarnDoorOpen | M4 | 15 | bit | | | | 0/1 | | |

Why these resolutions. 0.25 rpm and 0.01 km/h are finer than any gauge can
show; they are there so the scaling step of the pipeline is a real
multiplication and so the out-of-range band (raw above the valid maximum, up
to SNA) is wide. The ranges come from the brief: speed 0–250 km/h, engine
0–8000 rpm. Fuel and coolant ranges are typical of a passenger car sender.

## 3. Functional requirements

**ICM-F-01** The ICM shall receive M1 (0x100) and take EngineSpeed from it.

**ICM-F-02** The ICM shall receive M2 (0x200) and take VehicleSpeed from it.

**ICM-F-03** The ICM shall receive M3 (0x300) and take FuelLevel and
CoolantTemp from it.

**ICM-F-04** The ICM shall receive M4 (0x400) and take the turn signal and
warning lamp bits from it.

**ICM-F-05** Every valid raw value shall be scaled to its physical value with
the factor and offset of section 2.3.

**ICM-F-06** Analog values shall be low-pass filtered before display:
first-order filter, time constants 0.20 s (speed), 0.08 s (rpm), 5 s (fuel,
against slosh), 1 s (coolant). After start-up or after a fault the filter
restarts from the first valid sample instead of ramping up from zero.

**ICM-F-07** The ICM shall display speedometer, tachometer, fuel gauge,
coolant gauge, left/right turn indicators, hazard, warning lamps and text
messages. Turn indicators blink at 90 ± 30 flashes per minute (UNECE R48;
ours: 86/min). The display is refreshed every 50 ms.

**ICM-F-08** The ICM shall raise driver warnings from valid values: low fuel
at ≤ 10 % (cleared above 12 %), engine overheat at ≥ 115 °C (cleared below
110 °C), redline at ≥ 6500 rpm. A warning is never raised from an invalid
value.

## 4. Timing requirements

The SystemCounter tick is 10 ms on the POSIX target (section 7).

| Id | Task | Activation | Period / deadline |
|---|---|---|---|
| ICM-T-01 | Task_Acquisition | ALARM_Acquisition | 10 ms |
| ICM-T-02 | Task_Processing | ALARM_Processing | 20 ms |
| ICM-T-03 | Task_Display | ALARM_Display | 50 ms |
| ICM-T-04 | Task_Diagnostics | ALARM_Diagnostics | 100 ms |
| ICM-T-05 | Task_Logging | ALARM_Logging | 1000 ms |
| ICM-T-06 | Task_CAN_RX | EV_CAN_RX from ISR_CAN_RX | 10 ms from reception |

**ICM-T-01** Task_Acquisition shall be released every 10 ms (1 tick) by an
alarm on SystemCounter.

**ICM-T-02** Task_Processing shall be released every 20 ms (2 ticks).

**ICM-T-03** Task_Display shall be released every 50 ms (5 ticks).

**ICM-T-04** Task_Diagnostics shall be released every 100 ms (10 ticks).

**ICM-T-05** Task_Logging shall be released every 1000 ms (100 ticks).

**ICM-T-06** Every accepted CAN frame shall be decoded and its values stored
in ICM_Data within 10 ms of the controller receiving it. Task_CAN_RX is an
extended task woken by the event EV_CAN_RX, set by the category 2 interrupt
ISR_CAN_RX.

**ICM-T-07** Scheduling shall be fixed-priority and fully preemptive, with
priorities in rate-monotonic order and Task_CAN_RX above all periodic tasks.
Deadlines are implicit (D = T): every job shall finish before its task's next
release. The task set shall be schedulable by response-time analysis using
measured worst-case execution times.

**ICM-T-08** A task overrun (an activation that arrives while the previous
job of the same task is still running) shall be detected through E_OS_LIMIT
in ErrorHook, and every job whose measured response time exceeds its deadline
shall be counted. Either one shall confirm DTC 0x31.

**ICM-T-09** An overrunning task shall not cause any task of higher priority
to miss a deadline.

## 5. Safety and fault handling requirements

**ICM-S-01** A message not received for more than 3 periods (section 2.1)
shall put its signals in TIMEOUT, and the corresponding DTC (0x01..0x04) shall
be confirmed on the next diagnostics cycle.

**ICM-S-01b** Before the first frame of a message is received, its timeout
shall be at least 2000 ms, so that the other ECUs' boot time does not raise a
fault at every ignition-on (as `ComFirstTimeout` in AUTOSAR COM).

**ICM-S-02** A raw value above its valid range (section 2.3), SNA included,
shall be treated as OUT_OF_RANGE, never scaled or displayed, and shall confirm
the signal's range DTC (0x11..0x14) after 3 consecutive diagnostics cycles.

**ICM-S-03** A frame whose CRC is wrong, whose DLC is not 8, or whose alive
counter repeats the previous frame's shall be discarded and counted. A jump of
the counter by more than 3 shall be counted as lost frames, and the frame
accepted. E2E errors within the last second shall confirm DTC 0x21 after 3
cycles.

**ICM-S-04** Faults shall be debounced into DTCs with the confirm/heal counts
of the table below, evaluated every 100 ms by Task_Diagnostics. Every
confirmation and every healing shall be reported with a timestamp.

| DTC | Name | Confirm after | Heal after |
|---|---|---|---|
| 0x01 | M1_ENGINE_SPEED_TIMEOUT | 1 cycle | 10 cycles |
| 0x02 | M2_VEHICLE_SPEED_TIMEOUT | 1 cycle | 10 cycles |
| 0x03 | M3_ENGINE_STATUS_TIMEOUT | 1 cycle | 10 cycles |
| 0x04 | M4_BODY_STATUS_TIMEOUT | 1 cycle | 10 cycles |
| 0x11 | SPEED_OUT_OF_RANGE | 3 cycles | 10 cycles |
| 0x12 | RPM_OUT_OF_RANGE | 3 cycles | 10 cycles |
| 0x13 | FUEL_OUT_OF_RANGE | 3 cycles | 10 cycles |
| 0x14 | TEMP_OUT_OF_RANGE | 3 cycles | 10 cycles |
| 0x21 | CAN_E2E_ERROR | 3 cycles | 10 cycles |
| 0x22 | CAN_RX_OVERRUN | 1 cycle | 10 cycles |
| 0x31 | TASK_DEADLINE_OVERRUN | 1 cycle | 50 cycles |

Timeouts confirm in one cycle because the 3-period timeout already is the
debounce; a deadline overrun confirms in one cycle because it is a fact, not
a noisy measurement, and heals slowly so a task that overruns every other
period does not make it flicker.

**ICM-S-05** Fail-safe display: a signal that is not VALID shall be shown as
`---`, never as a number (stale or zero); when M4 is lost the turn
indicators and the bus-requested warning lamps shall go dark; any confirmed
DTC shall light the FAULT lamp and show a text message.

**ICM-S-06** CAN frames lost because the receive FIFO (8 frames, controller)
or the driver queue (32 frames) was full shall be counted, and shall confirm
DTC 0x22. No frame shall be lost silently: frames sent = frames received +
frames counted as lost.

## 6. Data and synchronisation requirements

**ICM-D-01** All signal data shared between tasks shall live in one structure,
ICM_Data (speed, rpm, fuel, temperature, indicators, fault flags), accessed
only through accessor functions that hold ICM_Resource for the whole copy.
No accessor shall return a pointer into ICM_Data.

**ICM-D-02** ICM_Resource shall use the OSEK priority ceiling protocol, with
the ceiling computed by GOIL from the tasks that declare the resource. No task
shall hold two resources at once.

**ICM-D-03** With OS STATUS = EXTENDED, no OS service call shall fail at run
time (no ErrorHook call other than the E_OS_LIMIT of ICM-T-08).

**ICM-D-04** Every copy of ICM_Data shall be checked for consistency
(sequence numbers written before and after the payload) and the number of
torn copies shall be zero. The check shall be shown to work: in a build
without the resource, the same test shall find torn copies.

## 7. Platform assumptions and limits of the simulation

These are not requirements on the ICM but on how far its tests can be
trusted. They are repeated in the report.

1. **Tick.** Trampoline's POSIX port gets its tick from ViPER, a helper
   process that sends SIGUSR2 every 10 ms using `usleep`. The tick therefore
   drifts (measured +0.5 % under SCHED_FIFO, +2.4 % without on a loaded
   host). Timing requirements are checked exactly in ticks and with a ±5 %
   tolerance in wall-clock time.
2. **Interrupts are signals.** An ISR is a Unix signal handler; masking
   interrupts is `sigprocmask`. Signals do not queue, which mirrors a
   pending bit on a real interrupt controller.
3. **The host scheduler.** Linux decides when the Trampoline process runs.
   Under SCHED_FIFO the latency tail is short (IRQ p99 ≈ 0.15 ms); without
   it, on a loaded machine, it is not (p99 ≈ 1.9 ms). The test runner uses
   SCHED_FIFO when it has the privilege and records which mode each run used.
4. **CAN.** The bus is `vcan0`: no bit timing, no arbitration, no error
   frames, no bus-off. The controller (filter, FIFO, overrun, interrupt) is
   emulated in software (`src/can_ctrl.c`). A physical bus can replace
   `vcan0` with no code change (`ICM_CAN_IF=can0`), but the timing of a
   real transceiver has not been measured.
5. **Display.** Text frames on a terminal, not gauges. See the comment at the
   top of `src/dashboard.h` for why.
6. **Execution times** are those of an x86-64 server core, two to three
   orders of magnitude faster than a cluster MCU. The schedulability margin
   shown here would shrink on target hardware; the method to recompute it
   (`tools/analyze_timing.py`) would not change.
