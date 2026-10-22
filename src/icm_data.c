/*
 * ICM — the one instance of ICM_Data and its accessors.
 * Osvaldo Janeri Filho <janeri@gmail.com>
 *
 * Mutual exclusion comes from the OSEK priority ceiling protocol. In the OIL
 * file every task that calls into this module lists ICM_Resource, and GOIL
 * sets the resource's ceiling to the highest of their priorities, which is
 * Task_CAN_RX's. While a task holds the resource it runs at that ceiling, so
 * no other task that touches ICM_Data can be scheduled until it releases.
 *
 * What the protocol buys, beyond plain mutual exclusion:
 *   - no deadlock: a task holding the ceiling cannot be preempted by any task
 *     that could want the resource, so the "A waits for B waits for A" cycle
 *     cannot form;
 *   - bounded priority inversion: a task can be delayed by at most one lower
 *     priority critical section, and only once per job, before it starts;
 *   - no blocking inside the critical section: GetResource never waits. If a
 *     task got the CPU, nobody else holds a resource it might need.
 *
 * ISR_CAN_RX does not touch ICM_Data at all; it only moves frames from the
 * controller FIFO to the driver queue (can_ctrl.c). That keeps ICM_Resource a
 * task-level resource and the interrupt latency independent of how long any
 * task holds it.
 */
#include "icm_data.h"

#include <stddef.h>
#include <string.h>
#include <time.h>

#include "tpl_os.h"

DeclareResource(ICM_Resource);

/*
 * ICM_UNLOCKED_SHARED_DATA exists for exactly one build, race_unlocked, which
 * is the negative control of tests/integration/test_race.py: the same race
 * test must fail when the resource is taken out, or the test proves nothing.
 * tools/build.sh is the only place that defines it.
 */
#if defined(ICM_UNLOCKED_SHARED_DATA)
#warning "ICM_Data accessors built WITHOUT ICM_Resource (race_unlocked test build)"
#define ICM_LOCK()   do { } while (0)
#define ICM_UNLOCK() do { } while (0)
#else
#define ICM_LOCK()   (void)GetResource(ICM_Resource)
#define ICM_UNLOCK() (void)ReleaseResource(ICM_Resource)
#endif
/* The (void) is not hiding errors: OS is built with STATUS = EXTENDED, so a
 * failing GetResource/ReleaseResource reaches ErrorHook, is counted as an OS
 * API error, and the timing tests require that count to be zero. */

static ICM_Data icm_data;

static uint32_t snapshots_taken;
static uint32_t snapshots_torn;

static void spin_us(unsigned us)
{
  struct timespec t0, t;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  do {
    clock_gettime(CLOCK_MONOTONIC, &t);
  } while ((unsigned)((t.tv_sec - t0.tv_sec) * 1000000L +
                      (t.tv_nsec - t0.tv_nsec) / 1000L) < us);
}

void icm_data_rx_store(const icm_can_values_t *v, uint32_t tick)
{
  ICM_LOCK();
  switch (v->msg) {
  case ICM_MSG_M1:
    icm_data.raw.engine_speed_raw = v->engine_speed_raw;
    break;
  case ICM_MSG_M2:
    icm_data.raw.vehicle_speed_raw = v->vehicle_speed_raw;
    break;
  case ICM_MSG_M3:
    icm_data.raw.fuel_raw = v->fuel_raw;
    icm_data.raw.coolant_raw = v->coolant_raw;
    break;
  case ICM_MSG_M4:
    icm_data.raw.turn_bits = v->turn_bits;
    icm_data.raw.warning_bits = v->warning_bits;
    break;
  default:
    break;
  }
  if (v->msg < ICM_MSG_COUNT) {
    icm_data.raw.last_rx_tick[v->msg] = tick;
    icm_data.raw.ever_rx[v->msg] = true;
    icm_data.raw.rx_ok[v->msg]++;
  }
  ICM_UNLOCK();
}

void icm_data_rx_count(icm_rx_err_t kind, uint32_t n)
{
  ICM_LOCK();
  switch (kind) {
  case ICM_RX_ERR_CRC:            icm_data.raw.err_crc += n; break;
  case ICM_RX_ERR_DLC:            icm_data.raw.err_dlc += n; break;
  case ICM_RX_ERR_COUNTER_REPEAT: icm_data.raw.err_counter_repeat += n; break;
  case ICM_RX_COUNTER_JUMP:       icm_data.raw.counter_jumps += n; break;
  case ICM_RX_HW_OVERRUN:         icm_data.raw.hw_overruns += n; break;
  case ICM_RX_SW_OVERRUN:         icm_data.raw.sw_overruns += n; break;
  default: break;
  }
  ICM_UNLOCK();
}

void icm_data_read_raw(icm_raw_t *out)
{
  ICM_LOCK();
  *out = icm_data.raw;
  ICM_UNLOCK();
}

void icm_data_write_validated(const icm_validated_t *in)
{
  ICM_LOCK();
  icm_data.validated = *in;
  ICM_UNLOCK();
}

void icm_data_read_validated(icm_validated_t *out)
{
  ICM_LOCK();
  *out = icm_data.validated;
  ICM_UNLOCK();
}

void icm_data_write_physical(const icm_physical_t *in)
{
  ICM_LOCK();
  uint32_t seq = icm_data.phys.seq_end + 1u;
  icm_data.phys = *in;
  icm_data.phys.seq_begin = seq;
  icm_data.phys.seq_end = seq;
  ICM_UNLOCK();
}

void icm_data_write_faults(const icm_faults_t *in)
{
  ICM_LOCK();
  icm_data.faults = *in;
  ICM_UNLOCK();
}

bool icm_data_snapshot(ICM_Data *out, unsigned race_window_us)
{
  /*
   * The copy is done in two halves split in the middle of the physical
   * section, between seq_begin and seq_end. With race_window_us = 0 this is
   * one memcpy in two calls and costs nothing; with a window it is a reader
   * that is slow exactly where it hurts most.
   */
  const size_t split = offsetof(ICM_Data, phys) +
                       offsetof(icm_physical_t, fuel_pct);

  ICM_LOCK();
  memcpy(out, &icm_data, split);
  if (race_window_us > 0u) {
    spin_us(race_window_us);
  }
  __asm__ volatile("" ::: "memory"); /* re-read after the window */
  memcpy((char *)out + split, (const char *)&icm_data + split,
         sizeof(ICM_Data) - split);
  ICM_UNLOCK();

  bool consistent = (out->phys.seq_begin == out->phys.seq_end);

  /* Three tasks of different priority call this function, so the counters
   * are shared. They are not part of ICM_Data (the unlocked build must still
   * count correctly, or the negative control would be unreliable), hence the
   * interrupt lock instead of the resource. */
  SuspendOSInterrupts();
  snapshots_taken++;
  if (!consistent) {
    snapshots_torn++;
  }
  ResumeOSInterrupts();
  return consistent;
}

void icm_data_consistency_stats(uint32_t *snapshots, uint32_t *torn)
{
  SuspendOSInterrupts();
  *snapshots = snapshots_taken;
  *torn = snapshots_torn;
  ResumeOSInterrupts();
}
