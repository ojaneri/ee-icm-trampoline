/*
 * ICM — Instrument Cluster Module on Trampoline RTOS
 * Project #2, Sistemas Embarcados, UTFPR — Prof. Max Mauro Dias Santos
 * Osvaldo Janeri Filho <janeri@gmail.com>
 *
 * Every number the application depends on lives here, so the requirements
 * document and the code can be checked against one file. Each constant names
 * the requirement it implements (see requirements/icm_requirements.md).
 */
#ifndef ICM_CONFIG_H
#define ICM_CONFIG_H

/* ------------------------------------------------------------------------
 * Time base
 * ------------------------------------------------------------------------
 * The Trampoline POSIX port drives SystemCounter from ViPER, a helper
 * process that sends SIGUSR2 every 10 ms (machines/posix/tpl_machine_posix.c,
 * tpl_viper_start_auto_timer(signal_for_counters, 10000)). One counter tick is
 * therefore 10 ms and every period below is a whole number of ticks. We did not
 * patch the port to get a finer tick: the professor's task set is 10 ms at its
 * fastest, which is exactly one tick, and keeping upstream Trampoline untouched
 * means anyone can reproduce the build from a clean clone.
 */
#define ICM_TICK_MS                10u

/* Task periods in ticks (ICM-T-01..05). Deadlines are implicit: D = T. */
#define ICM_PERIOD_ACQUISITION     1u    /*   10 ms */
#define ICM_PERIOD_PROCESSING      2u    /*   20 ms */
#define ICM_PERIOD_DISPLAY         5u    /*   50 ms */
#define ICM_PERIOD_DIAGNOSTICS     10u   /*  100 ms */
#define ICM_PERIOD_LOGGING         100u  /* 1000 ms */

/*
 * All periodic alarms are armed with the same first expiry. That puts every
 * task's release on the same tick at start-up and again every 1000 ms (the
 * hyperperiod), which is the critical instant of Liu & Layland: the worst-case
 * response time of each task happens there. We want the tests to exercise the
 * worst case, not dodge it with clever phasing.
 */
#define ICM_ALARM_FIRST_EXPIRY     10u   /* ticks after Task_Init runs */

/*
 * Task_CAN_RX is sporadic, so it has no period to inherit a deadline from.
 * 10 ms (one tick) from the frame leaving the bus to its values being in
 * ICM_Data: Task_Acquisition samples every tick, so anything slower would make
 * a fresh frame wait a whole extra acquisition cycle (ICM-T-06).
 */
#define ICM_CAN_RX_DEADLINE_MS     10u

/* ------------------------------------------------------------------------
 * CAN messages (ICM-F-01..04, signal dictionary in the requirements)
 * ------------------------------------------------------------------------ */
#define ICM_CAN_ID_M1_ENGINE_SPEED   0x100u  /* professor's M1 */
#define ICM_CAN_ID_M2_VEHICLE_SPEED  0x200u  /* professor's M2 */
#define ICM_CAN_ID_M3_ENGINE_STATUS  0x300u  /* our extension: fuel, coolant */
#define ICM_CAN_ID_M4_BODY_STATUS    0x400u  /* our extension: turn, lamps   */

#define ICM_CAN_PERIOD_M1_MS       500u
#define ICM_CAN_PERIOD_M2_MS       1000u
#define ICM_CAN_PERIOD_M3_MS       1000u
#define ICM_CAN_PERIOD_M4_MS       100u

/*
 * Reception timeout = 3 transmission periods (ICM-S-01). Two consecutive lost
 * frames are tolerated, the third is a fault. This is the usual compromise in
 * AUTOSAR COM deadline monitoring: one period would flag every late frame,
 * five would leave the driver looking at a frozen speedometer for 5 s.
 */
#define ICM_TIMEOUT_FACTOR         3u
#define ICM_TIMEOUT_M1_TICKS  (ICM_TIMEOUT_FACTOR * ICM_CAN_PERIOD_M1_MS / ICM_TICK_MS)
#define ICM_TIMEOUT_M2_TICKS  (ICM_TIMEOUT_FACTOR * ICM_CAN_PERIOD_M2_MS / ICM_TICK_MS)
#define ICM_TIMEOUT_M3_TICKS  (ICM_TIMEOUT_FACTOR * ICM_CAN_PERIOD_M3_MS / ICM_TICK_MS)
#define ICM_TIMEOUT_M4_TICKS  (ICM_TIMEOUT_FACTOR * ICM_CAN_PERIOD_M4_MS / ICM_TICK_MS)

/*
 * Until the first frame of a message arrives, the timeout is at least 2 s
 * (ICM-S-01b). The other ECUs boot at their own pace, and the body message's
 * 300 ms cyclic timeout would otherwise flag a fault on every ignition-on.
 * Same idea as ComFirstTimeout in AUTOSAR COM. Found the hard way: the first
 * fault run confirmed M4_BODY_STATUS_TIMEOUT 0.4 s after start-up, before the
 * simulator had sent anything.
 */
#define ICM_FIRST_TIMEOUT_TICKS    (2000u / ICM_TICK_MS)

/* ------------------------------------------------------------------------
 * Signal ranges (ICM-S-02). Raw = physical / factor - offset.
 * ------------------------------------------------------------------------ */
#define ICM_SPEED_FACTOR           0.01f   /* km/h per bit */
#define ICM_SPEED_MAX_KMH          250.0f
#define ICM_SPEED_MAX_RAW          25000u

#define ICM_RPM_FACTOR             0.25f   /* rpm per bit */
#define ICM_RPM_MAX                8000.0f
#define ICM_RPM_MAX_RAW            32000u

#define ICM_FUEL_FACTOR            0.5f    /* % per bit */
#define ICM_FUEL_MAX_PCT           100.0f
#define ICM_FUEL_MAX_RAW           200u

#define ICM_TEMP_OFFSET_C          (-40.0f)
#define ICM_TEMP_MIN_C             (-40.0f)
#define ICM_TEMP_MAX_C             150.0f
#define ICM_TEMP_MAX_RAW           190u    /* 190 - 40 = 150 degC */

/* "Signal not available", the J1939 convention for a sender that knows its
 * own sensor is broken. Treated as out-of-range by the cluster. */
#define ICM_SNA_U16                0xFFFFu
#define ICM_SNA_U8                 0xFFu

/* ------------------------------------------------------------------------
 * Filters (ICM-F-06). First-order low-pass, alpha = dt / (tau + dt), run in
 * Task_Processing every 20 ms.
 * ------------------------------------------------------------------------ */
#define ICM_DT_PROCESSING_S        0.020f
#define ICM_TAU_SPEED_S            0.20f   /* needle damping, still feels live   */
#define ICM_TAU_RPM_S              0.08f   /* tachometer must follow blips       */
#define ICM_TAU_FUEL_S             5.0f    /* hides fuel slosh in corners        */
#define ICM_TAU_TEMP_S             1.0f

/* ------------------------------------------------------------------------
 * Driver warnings (ICM-F-08)
 * ------------------------------------------------------------------------ */
#define ICM_LOW_FUEL_ON_PCT        10.0f
#define ICM_LOW_FUEL_OFF_PCT       12.0f   /* 2 % hysteresis: no flicker */
#define ICM_OVERHEAT_ON_C          115.0f
#define ICM_OVERHEAT_OFF_C         110.0f
#define ICM_REDLINE_RPM            6500.0f

/*
 * Turn indicator blink: UNECE R48 asks for 90 +/- 30 flashes per minute. The
 * display task runs every 50 ms, so the phase flips every 7 frames = 350 ms,
 * i.e. 700 ms per flash, 86 flashes/min.
 */
#define ICM_BLINK_HALF_FRAMES      7u

/* ------------------------------------------------------------------------
 * Diagnostics (ICM-S-04). Counted in Task_Diagnostics cycles of 100 ms.
 * ------------------------------------------------------------------------ */
#define ICM_DTC_CONFIRM_CYCLES     3u    /* 300 ms of continuous failure */
#define ICM_DTC_HEAL_CYCLES        10u   /* 1 s of continuous pass       */
/* Event-type monitors (E2E errors, overruns) fail for this many cycles after
 * the last error: one second, the period of the slowest message. */
#define ICM_DIAG_ERROR_WINDOW      10u

/* ------------------------------------------------------------------------
 * Virtual CAN controller
 * ------------------------------------------------------------------------ */
#define ICM_CAN_HW_FIFO_DEPTH      8u    /* power of two */
#define ICM_CAN_SW_QUEUE_DEPTH     32u   /* power of two */
#define ICM_CAN_MAX_COUNTER_DELTA  3u    /* AUTOSAR E2E MaxDeltaCounter */

#endif /* ICM_CONFIG_H */
