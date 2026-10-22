/*
 * ICM — timing instrumentation implementation.
 * Osvaldo Janeri Filho <janeri@gmail.com>
 */
#include "icm_trace.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "icm_config.h"
#include "tpl_os.h"

#define JOBS_PER_TASK 8192u   /* 81 s of Task_Acquisition */
#define MAX_TICKS     16384u  /* 163 s */

typedef struct {
  uint32_t release_tick;
  uint32_t start_tick;
  uint64_t release_ns;
  uint64_t start_ns;
  uint64_t end_ns;
  uint64_t exec_ns;
  uint64_t aux_ns; /* Task_CAN_RX: time ISR_CAN_RX ran for this batch */
  uint32_t preemptions;
} job_t;

static const char *const task_names[ICM_T_COUNT] = {
    "Task_CAN_RX",      "Task_Acquisition", "Task_Processing",
    "Task_Display",     "Task_Diagnostics", "Task_Logging",
};

static int task_ids[ICM_T_COUNT];
static uint64_t deadline_ns[ICM_T_COUNT];

/* written by the ALARM_TickProbe callback only */
static volatile uint32_t tick_count;
static uint64_t tick_ns[MAX_TICKS];

/* per task, written by that task (and its Pre/PostTaskHook) only */
static job_t jobs[ICM_T_COUNT][JOBS_PER_TASK];
static uint32_t job_count[ICM_T_COUNT];
static uint32_t grid_first[ICM_T_COUNT];
static uint32_t grid_period[ICM_T_COUNT];
static bool job_active[ICM_T_COUNT];
static uint64_t switched_in_ns[ICM_T_COUNT];
static uint64_t exec_acc_ns[ICM_T_COUNT];
static uint32_t preempt_count[ICM_T_COUNT];
static job_t current[ICM_T_COUNT];
static icm_task_stats_t stats[ICM_T_COUNT];

/* written by ErrorHook */
static icm_os_errors_t os_errors;

uint64_t icm_now_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

void icm_trace_init(void)
{
  task_ids[ICM_T_CAN_RX] = Task_CAN_RX;
  task_ids[ICM_T_ACQUISITION] = Task_Acquisition;
  task_ids[ICM_T_PROCESSING] = Task_Processing;
  task_ids[ICM_T_DISPLAY] = Task_Display;
  task_ids[ICM_T_DIAGNOSTICS] = Task_Diagnostics;
  task_ids[ICM_T_LOGGING] = Task_Logging;

  const uint64_t tick = (uint64_t)ICM_TICK_MS * 1000000ull;
  deadline_ns[ICM_T_CAN_RX] = (uint64_t)ICM_CAN_RX_DEADLINE_MS * 1000000ull;
  deadline_ns[ICM_T_ACQUISITION] = ICM_PERIOD_ACQUISITION * tick;
  deadline_ns[ICM_T_PROCESSING] = ICM_PERIOD_PROCESSING * tick;
  deadline_ns[ICM_T_DISPLAY] = ICM_PERIOD_DISPLAY * tick;
  deadline_ns[ICM_T_DIAGNOSTICS] = ICM_PERIOD_DIAGNOSTICS * tick;
  deadline_ns[ICM_T_LOGGING] = ICM_PERIOD_LOGGING * tick;

  tick_ns[0] = icm_now_ns();
}

void icm_trace_tick(void)
{
  uint32_t t = tick_count + 1u;
  if (t < MAX_TICKS) {
    tick_ns[t] = icm_now_ns();
  }
  tick_count = t;
}

uint32_t icm_trace_now_tick(void)
{
  return tick_count; /* aligned 32-bit load, one writer */
}

uint64_t icm_trace_tick_ns(uint32_t tick)
{
  return (tick < MAX_TICKS) ? tick_ns[tick] : 0u;
}

icm_task_t icm_trace_index_of(int task_id)
{
  for (int i = 0; i < ICM_T_COUNT; i++) {
    if (task_ids[i] == task_id) {
      return (icm_task_t)i;
    }
  }
  return ICM_T_COUNT;
}

const char *icm_trace_task_name(icm_task_t t)
{
  return (t < ICM_T_COUNT) ? task_names[t] : "?";
}

uint64_t icm_trace_deadline_ns(icm_task_t t)
{
  return (t < ICM_T_COUNT) ? deadline_ns[t] : 0u;
}

void icm_trace_set_release_grid(icm_task_t t, uint32_t first_tick,
                                uint32_t period_ticks)
{
  grid_first[t] = first_tick;
  grid_period[t] = period_ticks;
}

static void begin(icm_task_t t, uint32_t release_tick, uint64_t release_ns,
                  uint64_t aux_ns, uint32_t start_tick, uint64_t start_ns)
{
  current[t].release_tick = release_tick;
  current[t].start_tick = start_tick;
  current[t].release_ns = release_ns;
  current[t].start_ns = start_ns;
  current[t].aux_ns = aux_ns;
  exec_acc_ns[t] = 0;
  preempt_count[t] = 0;
  switched_in_ns[t] = start_ns;
  job_active[t] = true;
}

void icm_trace_job_begin(icm_task_t t)
{
  SuspendOSInterrupts();
  uint64_t now = icm_now_ns();
  uint32_t start_tick = tick_count;
  uint32_t release_tick = start_tick;
  if (grid_period[t] > 0u && start_tick >= grid_first[t]) {
    release_tick = grid_first[t] +
                   ((start_tick - grid_first[t]) / grid_period[t]) *
                       grid_period[t];
  }
  uint64_t release_ns = icm_trace_tick_ns(release_tick);
  begin(t, release_tick, release_ns ? release_ns : now, 0u, start_tick, now);
  ResumeOSInterrupts();
}

void icm_trace_job_begin_at(icm_task_t t, uint64_t release_ns, uint64_t aux_ns)
{
  SuspendOSInterrupts();
  uint64_t now = icm_now_ns();
  begin(t, tick_count, release_ns, aux_ns, tick_count, now);
  ResumeOSInterrupts();
}

void icm_trace_job_end(icm_task_t t)
{
  SuspendOSInterrupts();
  uint64_t now = icm_now_ns();
  job_t *j = &current[t];
  j->end_ns = now;
  j->exec_ns = exec_acc_ns[t] + (now - switched_in_ns[t]);
  j->preemptions = preempt_count[t];
  job_active[t] = false;

  uint64_t response = j->end_ns - j->release_ns;
  stats[t].jobs++;
  if (response > deadline_ns[t]) {
    stats[t].deadline_misses++;
  }
  if (response > stats[t].max_response_ns) {
    stats[t].max_response_ns = response;
  }
  if (j->exec_ns > stats[t].max_exec_ns) {
    stats[t].max_exec_ns = j->exec_ns;
  }
  if (job_count[t] < JOBS_PER_TASK) {
    jobs[t][job_count[t]++] = *j;
  }
  ResumeOSInterrupts();
}

/* Hooks run in kernel context with interrupts already masked. */
void icm_trace_pre_task_hook(void)
{
  TaskType id;
  GetTaskID(&id);
  icm_task_t t = icm_trace_index_of((int)id);
  if (t < ICM_T_COUNT && job_active[t]) {
    switched_in_ns[t] = icm_now_ns();
  }
}

void icm_trace_post_task_hook(void)
{
  TaskType id;
  GetTaskID(&id);
  icm_task_t t = icm_trace_index_of((int)id);
  if (t < ICM_T_COUNT && job_active[t]) {
    exec_acc_ns[t] += icm_now_ns() - switched_in_ns[t];
    preempt_count[t]++;
  }
}

void icm_trace_error_hook(uint8_t error, uint8_t service, int task_id_or_neg)
{
  if (error == E_OS_LIMIT && task_id_or_neg >= 0) {
    icm_task_t t = icm_trace_index_of(task_id_or_neg);
    if (t < ICM_T_COUNT) {
      os_errors.overruns[t]++;
      return;
    }
  }
  os_errors.api_errors++;
  os_errors.last_error = error;
  os_errors.last_service = service;
}

void icm_trace_get_os_errors(icm_os_errors_t *out)
{
  SuspendOSInterrupts();
  *out = os_errors;
  ResumeOSInterrupts();
}

void icm_trace_get_task_stats(icm_task_t t, icm_task_stats_t *out)
{
  SuspendOSInterrupts();
  *out = stats[t];
  ResumeOSInterrupts();
}

void icm_trace_dump(const char *dir)
{
  char path[512];
  FILE *f;

  snprintf(path, sizeof path, "%s/jobs.csv", dir);
  f = fopen(path, "w");
  if (f != NULL) {
    fprintf(f, "task,job,release_tick,start_tick,release_ns,start_ns,end_ns,"
               "exec_ns,preemptions,aux_ns\n");
    for (int t = 0; t < ICM_T_COUNT; t++) {
      for (uint32_t i = 0; i < job_count[t]; i++) {
        const job_t *j = &jobs[t][i];
        fprintf(f, "%s,%u,%u,%u,%llu,%llu,%llu,%llu,%u,%llu\n", task_names[t],
                i, j->release_tick, j->start_tick,
                (unsigned long long)j->release_ns,
                (unsigned long long)j->start_ns,
                (unsigned long long)j->end_ns,
                (unsigned long long)j->exec_ns, j->preemptions,
                (unsigned long long)j->aux_ns);
      }
    }
    fclose(f);
  }

  snprintf(path, sizeof path, "%s/ticks.csv", dir);
  f = fopen(path, "w");
  if (f != NULL) {
    uint32_t n = tick_count < MAX_TICKS ? tick_count : MAX_TICKS - 1u;
    fprintf(f, "tick,ns\n");
    for (uint32_t i = 0; i <= n; i++) {
      fprintf(f, "%u,%llu\n", i, (unsigned long long)tick_ns[i]);
    }
    fclose(f);
  }

  char line[256];
  for (int t = 0; t < ICM_T_COUNT; t++) {
    int len = snprintf(line, sizeof line,
                       "SUMMARY task=%s jobs=%u deadline_misses=%u "
                       "os_overruns=%u max_response_us=%.1f max_exec_us=%.1f\r\n",
                       task_names[t], stats[t].jobs, stats[t].deadline_misses,
                       os_errors.overruns[t],
                       (double)stats[t].max_response_ns / 1000.0,
                       (double)stats[t].max_exec_ns / 1000.0);
    (void)!write(STDOUT_FILENO, line, (size_t)len);
  }
  int len = snprintf(line, sizeof line,
                     "SUMMARY os_api_errors=%u last_error=%u last_service=%u "
                     "ticks=%u\r\n",
                     os_errors.api_errors, os_errors.last_error,
                     os_errors.last_service, tick_count);
  (void)!write(STDOUT_FILENO, line, (size_t)len);
}
