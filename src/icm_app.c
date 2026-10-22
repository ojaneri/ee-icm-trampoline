/*
 * ICM — Instrument Cluster Module on Trampoline RTOS (OSEK/VDX)
 * Project #2, Sistemas Embarcados, UTFPR — Prof. Max Mauro Dias Santos
 * Osvaldo Janeri Filho <janeri@gmail.com>
 *
 * Tasks, ISR and hooks. The OS objects are declared in icm.oil; this file
 * gives them bodies. Data flow, one stage per task:
 *
 *   bus -> ISR_CAN_RX -> Task_CAN_RX       receive  (decode, E2E check)
 *                     -> Task_Acquisition  validate (range, timeout)
 *                     -> Task_Processing   scale + filter
 *                     -> Task_Display      display
 *          Task_Diagnostics                faults -> DTCs, deadline monitor
 *          Task_Logging                    1 Hz record for the evidence
 *
 * Stages talk only through ICM_Data (icm_data.c), never directly.
 */
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "can_ctrl.h"
#include "can_db.h"
#include "dashboard.h"
#include "diag.h"
#include "icm_config.h"
#include "icm_data.h"
#include "icm_trace.h"
#include "signal_proc.h"
#include "tpl_os.h"

DeclareTask(Task_Init);
DeclareTask(Task_CAN_RX);
DeclareTask(Task_Acquisition);
DeclareTask(Task_Processing);
DeclareTask(Task_Display);
DeclareTask(Task_Diagnostics);
DeclareTask(Task_Logging);
DeclareTask(Task_Shutdown);

DeclareAlarm(ALARM_Acquisition);
DeclareAlarm(ALARM_Processing);
DeclareAlarm(ALARM_Display);
DeclareAlarm(ALARM_Diagnostics);
DeclareAlarm(ALARM_Logging);
DeclareAlarm(ALARM_Shutdown);

DeclareEvent(EV_CAN_RX);
DeclareEvent(EV_CAN_ERROR);

DeclareResource(RES_CONSOLE);

/* Must match SOURCE of ISR_CAN_RX in icm.oil. SIGQUIT because, of the five
 * signals the POSIX port accepts as ISR sources, SIGUSR2 is the OS tick and
 * SIGTERM/SIGTRAP are what the virtual platform sends for keys 'a' and 'b'. */
#define ICM_CAN_IRQ_SIGNAL SIGQUIT

/* Test-only knobs, set by tools/build.sh for the race and overload builds. */
#ifndef ICM_RACE_WINDOW_US
#define ICM_RACE_WINDOW_US 0u
#endif

typedef enum { DASH_LIVE, DASH_PLAIN, DASH_OFF } dash_mode_t;

static uint64_t t0_ns;
static dash_mode_t dash_mode;

static double uptime_s(void)
{
  return (double)(icm_now_ns() - t0_ns) / 1e9;
}

/* ------------------------------------------------------------------------
 * Console. stdout is a shared device: Task_Logging (priority 1) can be
 * preempted in the middle of a line by Task_Display (priority 3), and the two
 * outputs would interleave. Each message is formatted on the caller's own
 * stack and written with one write() while holding RES_CONSOLE. Formatting
 * happens outside the critical section to keep it short.
 * ------------------------------------------------------------------------ */
static void console_write(const char *buf, size_t len)
{
  GetResource(RES_CONSOLE);
  (void)!write(STDOUT_FILENO, buf, len);
  ReleaseResource(RES_CONSOLE);
}

static void console_printf(const char *fmt, ...)
{
  char line[512];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  if (n > 0) {
    console_write(line, (size_t)n < sizeof line ? (size_t)n : sizeof line - 1);
  }
}

/* Only for contexts where no other task can be running: Task_Init (highest
 * priority, before any alarm is armed) and the hooks. */
static void console_direct(const char *fmt, ...)
{
  char line[512];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  if (n > 0) {
    (void)!write(STDOUT_FILENO, line,
                 (size_t)n < sizeof line ? (size_t)n : sizeof line - 1);
  }
}

static void spin_ms(unsigned ms)
{
  uint64_t end = icm_now_ns() + (uint64_t)ms * 1000000ull;
  while (icm_now_ns() < end) {
  }
}

int main(void)
{
  StartOS(OSDEFAULTAPPMODE);
  return 0;
}

/* ------------------------------------------------------------------------
 * Hooks
 * ------------------------------------------------------------------------ */

/*
 * The CAN controller thread is created here and not in main(). StartOS() on
 * the POSIX port forks: the parent becomes the keyboard reader of the virtual
 * platform and the child runs the OS. A thread created before StartOS() would
 * stay behind in the parent. StartupHook runs in the child, after the
 * interrupt handlers are installed and before the first task.
 */
void StartupHook(void)
{
  icm_trace_init();
  t0_ns = icm_now_ns();

  const char *mode = getenv("ICM_DASH");
  if (mode != NULL && strcmp(mode, "off") == 0) {
    dash_mode = DASH_OFF;
  } else if (mode != NULL && strcmp(mode, "plain") == 0) {
    dash_mode = DASH_PLAIN;
  } else if (mode != NULL && strcmp(mode, "live") == 0) {
    dash_mode = DASH_LIVE;
  } else {
    dash_mode = isatty(STDOUT_FILENO) ? DASH_LIVE : DASH_PLAIN;
  }

  const char *ifname = getenv("ICM_CAN_IF");
  if (ifname == NULL) {
    ifname = "vcan0";
  }
  if (can_ctrl_start(ifname, ICM_CAN_IRQ_SIGNAL) != 0) {
    console_direct("ICM WARNING: cannot open CAN interface %s, running with "
                   "no bus (every message will time out)\r\n", ifname);
  }
}

void ShutdownHook(StatusType error)
{
  can_ctrl_stop();
  uint32_t snaps, torn;
  icm_data_consistency_stats(&snaps, &torn);
  console_direct("\r\nICM SHUTDOWN status=%u uptime=%.2f s\r\n",
                 (unsigned)error, uptime_s());
  console_direct("SUMMARY snapshots=%u torn=%u race_window_us=%u\r\n", snaps,
                 torn, (unsigned)ICM_RACE_WINDOW_US);
  uint32_t pushed, hw_dropped, drained, sw_dropped, popped;
  can_ctrl_totals(&pushed, &hw_dropped, &drained, &sw_dropped, &popped);
  console_direct("SUMMARY can_pushed=%u can_hw_dropped=%u can_drained=%u "
                 "can_sw_dropped=%u can_popped=%u\r\n",
                 pushed, hw_dropped, drained, sw_dropped, popped);
  const char *dir = getenv("ICM_TRACE_DIR");
  icm_trace_dump(dir != NULL ? dir : ".");
}

/*
 * When an alarm tries to activate a task whose previous job has not finished,
 * ActivateTask fails with E_OS_LIMIT (ACTIVATION = 1) and the OS calls
 * ErrorHook with the service and task recorded. That is the OSEK way of saying
 * "this task overran its period", and it is the primary deadline monitor
 * (ICM-T-08). The response-time measurement in icm_trace.c is the second one.
 */
void ErrorHook(StatusType error)
{
  uint8_t service = (uint8_t)OSErrorGetServiceId();
  int task = -1;
  if (service == OSServiceId_ActivateTask) {
    task = (int)OSError_ActivateTask_TaskID();
  }
  icm_trace_error_hook((uint8_t)error, service, task);
}

void PreTaskHook(void)
{
  icm_trace_pre_task_hook();
}

void PostTaskHook(void)
{
  icm_trace_post_task_hook();
}

/* Runs on every tick, in the counter interrupt: the time base of the trace. */
ALARMCALLBACK(TickProbe)
{
  icm_trace_tick();
}

/* ------------------------------------------------------------------------
 * Task_Init — priority 10, autostart, runs once.
 * Arms the periodic alarms with SetRelAlarm rather than OIL autostart so the
 * release grid is known to the trace (first expiry tick) and the run length
 * can come from the environment.
 * ------------------------------------------------------------------------ */
TASK(Task_Init)
{
  uint32_t now = icm_trace_now_tick();
  uint32_t first = now + ICM_ALARM_FIRST_EXPIRY;

  console_direct("ICM Instrument Cluster Module - Trampoline OSEK/VDX, "
                 "tick %u ms\r\n", ICM_TICK_MS);

  SetRelAlarm(ALARM_Acquisition, ICM_ALARM_FIRST_EXPIRY, ICM_PERIOD_ACQUISITION);
  SetRelAlarm(ALARM_Processing, ICM_ALARM_FIRST_EXPIRY, ICM_PERIOD_PROCESSING);
  SetRelAlarm(ALARM_Display, ICM_ALARM_FIRST_EXPIRY, ICM_PERIOD_DISPLAY);
  SetRelAlarm(ALARM_Diagnostics, ICM_ALARM_FIRST_EXPIRY, ICM_PERIOD_DIAGNOSTICS);
  SetRelAlarm(ALARM_Logging, ICM_ALARM_FIRST_EXPIRY, ICM_PERIOD_LOGGING);

  icm_trace_set_release_grid(ICM_T_ACQUISITION, first, ICM_PERIOD_ACQUISITION);
  icm_trace_set_release_grid(ICM_T_PROCESSING, first, ICM_PERIOD_PROCESSING);
  icm_trace_set_release_grid(ICM_T_DISPLAY, first, ICM_PERIOD_DISPLAY);
  icm_trace_set_release_grid(ICM_T_DIAGNOSTICS, first, ICM_PERIOD_DIAGNOSTICS);
  icm_trace_set_release_grid(ICM_T_LOGGING, first, ICM_PERIOD_LOGGING);

  const char *run = getenv("ICM_RUN_SECONDS");
  if (run != NULL && atoi(run) > 0) {
    TickType ticks = (TickType)(atoi(run) * (1000 / ICM_TICK_MS));
    SetRelAlarm(ALARM_Shutdown, ticks, 0);
  }

  console_direct("ICM READY mono_ns=%llu can=%s race_window_us=%u\r\n",
                 (unsigned long long)icm_now_ns(),
                 can_ctrl_online() ? "online" : "offline",
                 (unsigned)ICM_RACE_WINDOW_US);
  TerminateTask();
}

TASK(Task_Shutdown)
{
  ShutdownOS(E_OK);
  TerminateTask();
}

/* ------------------------------------------------------------------------
 * ISR_CAN_RX — category 2. Does the minimum: empties the controller FIFO
 * into the driver queue and wakes Task_CAN_RX. Decoding is the task's job,
 * so the time spent with interrupts masked stays a handful of copies.
 * ------------------------------------------------------------------------ */
ISR(ISR_CAN_RX)
{
  if (can_ctrl_isr_drain() > 0u) {
    SetEvent(Task_CAN_RX, EV_CAN_RX);
  }
  if (can_ctrl_overrun_pending()) {
    SetEvent(Task_CAN_RX, EV_CAN_ERROR);
  }
}

/* ------------------------------------------------------------------------
 * Task_CAN_RX — extended task, priority 6, event driven. Receive stage.
 * ------------------------------------------------------------------------ */
static void handle_frame(const can_ctrl_rx_t *rx, uint32_t tick,
                         uint8_t last_counter[], bool counter_seen[])
{
  icm_can_values_t v;
  switch (can_db_decode(&rx->frame, &v)) {
  case CAN_DB_OK:
    if (counter_seen[v.msg]) {
      switch (can_db_check_counter(last_counter[v.msg], v.counter)) {
      case CAN_DB_COUNTER_REPEATED:
        icm_data_rx_count(ICM_RX_ERR_COUNTER_REPEAT, 1u);
        return; /* stale data, keep the previous value */
      case CAN_DB_COUNTER_JUMP:
        icm_data_rx_count(ICM_RX_COUNTER_JUMP, 1u);
        break;
      default:
        break;
      }
    }
    last_counter[v.msg] = v.counter;
    counter_seen[v.msg] = true;
    icm_data_rx_store(&v, tick);
    break;
  case CAN_DB_BAD_CRC:
    icm_data_rx_count(ICM_RX_ERR_CRC, 1u);
    break;
  case CAN_DB_BAD_DLC:
    icm_data_rx_count(ICM_RX_ERR_DLC, 1u);
    break;
  default:
    break; /* unknown id: the acceptance filter should have stopped it */
  }
}

TASK(Task_CAN_RX)
{
  static uint8_t last_counter[ICM_MSG_COUNT];
  static bool counter_seen[ICM_MSG_COUNT];

  for (;;) {
    EventMaskType events;
    WaitEvent(EV_CAN_RX | EV_CAN_ERROR);
    GetEvent(Task_CAN_RX, &events);
    ClearEvent(events);

    can_ctrl_rx_t rx;
    bool have = can_ctrl_pop(&rx);
    icm_trace_job_begin_at(ICM_T_CAN_RX, have ? rx.hw_rx_ns : icm_now_ns(),
                           have ? rx.isr_ns : 0u);
    uint32_t tick = icm_trace_now_tick();
    while (have) {
      handle_frame(&rx, tick, last_counter, counter_seen);
      have = can_ctrl_pop(&rx);
    }
    if (events & EV_CAN_ERROR) {
      uint32_t hw = can_ctrl_take_hw_overruns();
      uint32_t sw = can_ctrl_take_sw_overruns();
      if (hw > 0u) icm_data_rx_count(ICM_RX_HW_OVERRUN, hw);
      if (sw > 0u) icm_data_rx_count(ICM_RX_SW_OVERRUN, sw);
    }
    icm_trace_job_end(ICM_T_CAN_RX);
  }
}

/* ------------------------------------------------------------------------
 * Task_Acquisition — 10 ms, priority 5. Validate stage: freshness and range.
 * ------------------------------------------------------------------------ */
static uint32_t timeout_for(const icm_raw_t *raw, icm_msg_t m, uint32_t cyclic)
{
  if (!raw->ever_rx[m] && cyclic < ICM_FIRST_TIMEOUT_TICKS) {
    return ICM_FIRST_TIMEOUT_TICKS;
  }
  return cyclic;
}

TASK(Task_Acquisition)
{
  icm_trace_job_begin(ICM_T_ACQUISITION);

  icm_raw_t raw;
  icm_data_read_raw(&raw);
  uint32_t now = icm_trace_now_tick();

  icm_validated_t v;
  memset(&v, 0, sizeof v);
  v.engine_speed_raw = raw.engine_speed_raw;
  v.vehicle_speed_raw = raw.vehicle_speed_raw;
  v.fuel_raw = raw.fuel_raw;
  v.coolant_raw = raw.coolant_raw;
  v.turn_bits = raw.turn_bits;
  v.warning_bits = raw.warning_bits;
  v.sample_tick = now;

  v.status[ICM_SIG_SPEED] = sp_signal_status(
      raw.ever_rx[ICM_MSG_M2], now, raw.last_rx_tick[ICM_MSG_M2],
      timeout_for(&raw, ICM_MSG_M2, ICM_TIMEOUT_M2_TICKS),
      sp_validate_speed(raw.vehicle_speed_raw));
  v.status[ICM_SIG_RPM] = sp_signal_status(
      raw.ever_rx[ICM_MSG_M1], now, raw.last_rx_tick[ICM_MSG_M1],
      timeout_for(&raw, ICM_MSG_M1, ICM_TIMEOUT_M1_TICKS),
      sp_validate_rpm(raw.engine_speed_raw));
  v.status[ICM_SIG_FUEL] = sp_signal_status(
      raw.ever_rx[ICM_MSG_M3], now, raw.last_rx_tick[ICM_MSG_M3],
      timeout_for(&raw, ICM_MSG_M3, ICM_TIMEOUT_M3_TICKS),
      sp_validate_fuel(raw.fuel_raw));
  v.status[ICM_SIG_TEMP] = sp_signal_status(
      raw.ever_rx[ICM_MSG_M3], now, raw.last_rx_tick[ICM_MSG_M3],
      timeout_for(&raw, ICM_MSG_M3, ICM_TIMEOUT_M3_TICKS),
      sp_validate_temp(raw.coolant_raw));
  v.status[ICM_SIG_BODY] = sp_signal_status(
      raw.ever_rx[ICM_MSG_M4], now, raw.last_rx_tick[ICM_MSG_M4],
      timeout_for(&raw, ICM_MSG_M4, ICM_TIMEOUT_M4_TICKS), SIG_VALID);

  icm_data_write_validated(&v);

  icm_trace_job_end(ICM_T_ACQUISITION);
  TerminateTask();
}

/* ------------------------------------------------------------------------
 * Task_Processing — 20 ms, priority 4. Scale and filter stages.
 * ------------------------------------------------------------------------ */
static float filtered(sp_lpf_t *f, bool valid, float x, float tau)
{
  if (!valid) {
    sp_lpf_reset(f); /* restart from the first fresh sample on recovery */
    return 0.0f;
  }
  return sp_lpf_step(f, x, sp_lpf_alpha(ICM_DT_PROCESSING_S, tau));
}

TASK(Task_Processing)
{
  static sp_lpf_t f_speed, f_rpm, f_fuel, f_temp;
  static bool low_fuel, overheat;

  icm_trace_job_begin(ICM_T_PROCESSING);

  icm_validated_t v;
  icm_data_read_validated(&v);

  icm_physical_t p;
  memset(&p, 0, sizeof p);
  for (int s = 0; s < ICM_SIG_COUNT; s++) {
    p.valid[s] = (v.status[s] == SIG_VALID);
  }

  p.speed_kmh = filtered(&f_speed, p.valid[ICM_SIG_SPEED],
                         sp_scale_speed(v.vehicle_speed_raw), ICM_TAU_SPEED_S);
  p.rpm = filtered(&f_rpm, p.valid[ICM_SIG_RPM],
                   sp_scale_rpm(v.engine_speed_raw), ICM_TAU_RPM_S);
  p.fuel_pct = filtered(&f_fuel, p.valid[ICM_SIG_FUEL],
                        sp_scale_fuel(v.fuel_raw), ICM_TAU_FUEL_S);
  p.coolant_c = filtered(&f_temp, p.valid[ICM_SIG_TEMP],
                         sp_scale_temp(v.coolant_raw), ICM_TAU_TEMP_S);

  if (p.valid[ICM_SIG_BODY]) {
    p.turn_bits = v.turn_bits;
    p.warning_bits = v.warning_bits;
  }

  /* A warning computed from a value we do not have would be a guess. With
   * the signal invalid the lamp goes off and the fault message takes over. */
  low_fuel = p.valid[ICM_SIG_FUEL] &&
             sp_hyst_low(low_fuel, p.fuel_pct, ICM_LOW_FUEL_ON_PCT,
                         ICM_LOW_FUEL_OFF_PCT);
  overheat = p.valid[ICM_SIG_TEMP] &&
             sp_hyst_high(overheat, p.coolant_c, ICM_OVERHEAT_ON_C,
                          ICM_OVERHEAT_OFF_C);
  p.low_fuel = low_fuel;
  p.overheat = overheat;
  p.redline = p.valid[ICM_SIG_RPM] && p.rpm >= ICM_REDLINE_RPM;

  icm_data_write_physical(&p);

  icm_trace_job_end(ICM_T_PROCESSING);
  TerminateTask();
}

/* ------------------------------------------------------------------------
 * Task_Display — 50 ms, priority 3. Display stage.
 * In plain mode (not a terminal: logs, CI) the frame is still rendered every
 * period, so the task does the same work, but only one frame per second is
 * written out, to keep logs readable.
 * ------------------------------------------------------------------------ */
TASK(Task_Display)
{
  static uint32_t frame;
  static char out[4096]; /* only this task touches it */

  icm_trace_job_begin(ICM_T_DISPLAY);

  ICM_Data snap;
  icm_data_snapshot(&snap, ICM_RACE_WINDOW_US);
  uint32_t snaps, torn;
  icm_data_consistency_stats(&snaps, &torn);

  dash_ctx_t ctx = {
      .uptime_s = uptime_s(),
      .frame = frame,
      .ansi = (dash_mode == DASH_LIVE),
      .can_online = can_ctrl_online(),
      .torn_snapshots = torn,
  };
  size_t n = dash_render(out, sizeof out, &snap, &ctx);

  if (dash_mode == DASH_LIVE ||
      (dash_mode == DASH_PLAIN && frame % (1000u / (ICM_PERIOD_DISPLAY * ICM_TICK_MS)) == 0u)) {
    console_write(out, n);
  }
  frame++;

  icm_trace_job_end(ICM_T_DISPLAY);
  TerminateTask();
}

/* ------------------------------------------------------------------------
 * Task_Diagnostics — 100 ms, priority 2. Raw faults -> debounced DTCs, and
 * the deadline monitor.
 * ------------------------------------------------------------------------ */
TASK(Task_Diagnostics)
{
  static dtc_state_t dtc[DTC_COUNT];
  static uint32_t prev_e2e, prev_overrun, prev_os_overruns, prev_misses;
  /* Cycles since the last E2E error / overrun. Those errors come one per
   * frame, and a 1 Hz message with a bad CRC produces one error per second:
   * "new errors in this cycle" would be true for one cycle in ten and the
   * 3-cycle debounce would never confirm. So the monitor fails while there
   * has been an error within the last ICM_DIAG_ERROR_WINDOW cycles. */
  static uint32_t since_e2e = UINT32_MAX, since_overrun = UINT32_MAX;

  icm_trace_job_begin(ICM_T_DIAGNOSTICS);

#if defined(ICM_FAULT_OVERLOAD_DIAG_MS)
  /* Overload build only: between 5 s and 10 s this task burns more CPU than
   * its period, so its own next activation finds it still running. */
  {
    double t = uptime_s();
    if (t >= 5.0 && t < 10.0) {
      spin_ms(ICM_FAULT_OVERLOAD_DIAG_MS);
    }
  }
#elif defined(ICM_FAULT_IRQ_LOCK_MS)
  /* irq_lock build only: once, at t = 3 s, a far too long interrupt-locked
   * section, the classic way a real ECU loses CAN frames. The controller
   * thread keeps filling its 8-deep FIFO while ISR_CAN_RX cannot run. */
  {
    static bool done;
    if (!done && uptime_s() >= 3.0) {
      done = true;
      SuspendAllInterrupts();
      spin_ms(ICM_FAULT_IRQ_LOCK_MS);
      ResumeAllInterrupts();
    }
  }
#else
  (void)spin_ms;
#endif

  ICM_Data snap;
  icm_data_snapshot(&snap, 0u);
  const icm_validated_t *v = &snap.validated;
  const icm_raw_t *r = &snap.raw;

  icm_os_errors_t os;
  icm_trace_get_os_errors(&os);
  uint32_t os_overruns = 0, misses = 0;
  for (int t = 0; t < ICM_T_COUNT; t++) {
    icm_task_stats_t st;
    icm_trace_get_task_stats((icm_task_t)t, &st);
    os_overruns += os.overruns[t];
    misses += st.deadline_misses;
  }

  uint32_t e2e = r->err_crc + r->err_dlc + r->err_counter_repeat;
  uint32_t overrun = r->hw_overruns + r->sw_overruns;

  bool failed[DTC_COUNT] = {false};
  failed[DTC_M1_TIMEOUT] = v->status[ICM_SIG_RPM] == SIG_TIMEOUT;
  failed[DTC_M2_TIMEOUT] = v->status[ICM_SIG_SPEED] == SIG_TIMEOUT;
  failed[DTC_M3_TIMEOUT] = v->status[ICM_SIG_FUEL] == SIG_TIMEOUT;
  failed[DTC_M4_TIMEOUT] = v->status[ICM_SIG_BODY] == SIG_TIMEOUT;
  failed[DTC_SPEED_RANGE] = v->status[ICM_SIG_SPEED] == SIG_OUT_OF_RANGE;
  failed[DTC_RPM_RANGE] = v->status[ICM_SIG_RPM] == SIG_OUT_OF_RANGE;
  failed[DTC_FUEL_RANGE] = v->status[ICM_SIG_FUEL] == SIG_OUT_OF_RANGE;
  failed[DTC_TEMP_RANGE] = v->status[ICM_SIG_TEMP] == SIG_OUT_OF_RANGE;
  since_e2e = (e2e != prev_e2e) ? 0u : (since_e2e < UINT32_MAX ? since_e2e + 1u : since_e2e);
  since_overrun = (overrun != prev_overrun) ? 0u : (since_overrun < UINT32_MAX ? since_overrun + 1u : since_overrun);
  failed[DTC_CAN_INTEGRITY] = since_e2e < ICM_DIAG_ERROR_WINDOW;
  failed[DTC_CAN_OVERRUN] = since_overrun < ICM_DIAG_ERROR_WINDOW;
  failed[DTC_TASK_OVERRUN] = os_overruns != prev_os_overruns || misses != prev_misses;
  prev_e2e = e2e;
  prev_overrun = overrun;
  prev_os_overruns = os_overruns;
  prev_misses = misses;

  icm_faults_t f;
  memset(&f, 0, sizeof f);
  for (int i = 0; i < DTC_COUNT; i++) {
    dtc_event_t ev = dtc_update(&dtc[i], failed[i],
                                dtc_confirm_cycles((dtc_id_t)i),
                                dtc_heal_cycles((dtc_id_t)i));
    if (ev != DTC_NO_CHANGE) {
      console_printf("DIAG t=%.3f mono_ns=%llu DTC 0x%02X %s %s\r\n",
                     uptime_s(), (unsigned long long)icm_now_ns(),
                     dtc_code((dtc_id_t)i), dtc_name((dtc_id_t)i),
                     ev == DTC_BECAME_CONFIRMED ? "CONFIRMED" : "HEALED");
    }
    if (dtc[i].confirmed) {
      f.confirmed_mask |= 1u << i;
    }
    f.occurrences[i] = dtc[i].occurrences;
  }
  f.task_overruns = os_overruns;
  f.deadline_misses = misses;
  f.os_api_errors = os.api_errors;
  icm_data_write_faults(&f);

  icm_trace_job_end(ICM_T_DIAGNOSTICS);
  TerminateTask();
}

/* ------------------------------------------------------------------------
 * Task_Logging — 1000 ms, priority 1. One machine-readable line per second.
 * ------------------------------------------------------------------------ */
TASK(Task_Logging)
{
  icm_trace_job_begin(ICM_T_LOGGING);

  ICM_Data s;
  icm_data_snapshot(&s, 0u);
  uint32_t snaps, torn;
  icm_data_consistency_stats(&snaps, &torn);
  const icm_physical_t *p = &s.phys;
  const icm_validated_t *v = &s.validated;

  console_printf(
      "LOG t=%.2f mono_ns=%llu speed=%.1f/%s rpm=%.0f/%s fuel=%.1f/%s "
      "temp=%.1f/%s body=%s turn=0x%02X warn=0x%02X low_fuel=%d overheat=%d "
      "dtc=0x%08X rx=%u/%u/%u/%u crc=%u cnt=%u jump=%u ovr=%u overruns=%u "
      "misses=%u api_err=%u torn=%u/%u\r\n",
      uptime_s(), (unsigned long long)icm_now_ns(), p->speed_kmh,
      sp_status_name(v->status[ICM_SIG_SPEED]), p->rpm,
      sp_status_name(v->status[ICM_SIG_RPM]), p->fuel_pct,
      sp_status_name(v->status[ICM_SIG_FUEL]), p->coolant_c,
      sp_status_name(v->status[ICM_SIG_TEMP]),
      sp_status_name(v->status[ICM_SIG_BODY]), p->turn_bits, p->warning_bits,
      p->low_fuel, p->overheat, s.faults.confirmed_mask, s.raw.rx_ok[0],
      s.raw.rx_ok[1], s.raw.rx_ok[2], s.raw.rx_ok[3], s.raw.err_crc,
      s.raw.err_counter_repeat, s.raw.counter_jumps,
      s.raw.hw_overruns + s.raw.sw_overruns, s.faults.task_overruns,
      s.faults.deadline_misses, s.faults.os_api_errors, torn, snaps);

  icm_trace_job_end(ICM_T_LOGGING);
  TerminateTask();
}
