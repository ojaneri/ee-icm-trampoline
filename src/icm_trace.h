/*
 * ICM — timing instrumentation and OS error bookkeeping.
 * Osvaldo Janeri Filho <janeri@gmail.com>
 *
 * What is measured, per job (one activation of one task):
 *
 *   release   the tick at which the alarm activated the task, and the
 *             CLOCK_MONOTONIC time of that tick (taken by an alarm callback
 *             that runs on every tick, ALARM_TickProbe). For Task_CAN_RX the
 *             release is the time the controller took the frame off the bus.
 *   start     first instruction of the task body.
 *   end       last instruction before TerminateTask / WaitEvent.
 *   exec      CPU time of the job alone: PreTaskHook and PostTaskHook stamp
 *             every switch in and out, so time spent in higher priority tasks
 *             that preempted this job is not counted.
 *
 * response = end - release; deadline miss = response > period (D = T).
 *
 * Every task writes only its own row of the job tables, so the tables need no
 * lock. The few counters that more than one context writes (the OS error
 * counters, written by ErrorHook) are read with OS interrupts suspended.
 * Everything is dumped to CSV by ShutdownHook; tools/analyze_timing.py turns
 * the CSV into the tables in requirements/test_evidence.md.
 */
#ifndef ICM_TRACE_H
#define ICM_TRACE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  ICM_T_CAN_RX = 0,
  ICM_T_ACQUISITION,
  ICM_T_PROCESSING,
  ICM_T_DISPLAY,
  ICM_T_DIAGNOSTICS,
  ICM_T_LOGGING,
  ICM_T_COUNT
} icm_task_t;

typedef struct {
  uint32_t overruns[ICM_T_COUNT]; /* E_OS_LIMIT on alarm activation */
  uint32_t api_errors;            /* any other ErrorHook call        */
  uint8_t last_error;
  uint8_t last_service;
} icm_os_errors_t;

typedef struct {
  uint32_t jobs;
  uint32_t deadline_misses; /* response > deadline */
  uint64_t max_response_ns;
  uint64_t max_exec_ns;
} icm_task_stats_t;

uint64_t icm_now_ns(void);

void icm_trace_init(void);   /* StartupHook */
void icm_trace_tick(void);   /* ALARMCALLBACK, every tick */
uint32_t icm_trace_now_tick(void);
uint64_t icm_trace_tick_ns(uint32_t tick);

/* Task_Init, after SetRelAlarm: where the release grid of a task starts. */
void icm_trace_set_release_grid(icm_task_t t, uint32_t first_tick,
                                uint32_t period_ticks);

void icm_trace_job_begin(icm_task_t t);
/* Task_CAN_RX: release time comes from the frame, not from a tick. */
void icm_trace_job_begin_at(icm_task_t t, uint64_t release_ns,
                            uint64_t aux_ns);
void icm_trace_job_end(icm_task_t t);

void icm_trace_pre_task_hook(void);
void icm_trace_post_task_hook(void);
void icm_trace_error_hook(uint8_t error, uint8_t service, int task_id_or_neg);

void icm_trace_get_os_errors(icm_os_errors_t *out);
void icm_trace_get_task_stats(icm_task_t t, icm_task_stats_t *out);
const char *icm_trace_task_name(icm_task_t t);
uint64_t icm_trace_deadline_ns(icm_task_t t);

/* Maps an OSEK TaskType to our index, ICM_T_COUNT if it is not tracked. */
icm_task_t icm_trace_index_of(int task_id);

/* ShutdownHook: write jobs.csv and ticks.csv into dir, summary to stdout. */
void icm_trace_dump(const char *dir);

#endif /* ICM_TRACE_H */
