/*
 * ICM — access to ICM_Data. The only door into the shared structure.
 * Osvaldo Janeri Filho <janeri@gmail.com>
 *
 * Every function here does GetResource(ICM_Resource), copies, and
 * ReleaseResource(ICM_Resource). Nothing else. No function hands out a pointer
 * into the structure, so no caller can read or write it after the resource is
 * released; the copy is the caller's own. tools/check_shared_access.py fails
 * the build if any other file names the structure instance or calls
 * GetResource(ICM_Resource) itself.
 *
 * Why copies and not pointers: a critical section that is one memcpy has a
 * length we can measure and bound (it is the blocking term B in the response
 * time analysis of requirements/architecture.md). A critical section that lasts
 * "until the caller is done with the pointer" has no bound at all.
 */
#ifndef ICM_DATA_H
#define ICM_DATA_H

#include "icm_types.h"

typedef enum {
  ICM_RX_ERR_CRC = 0,
  ICM_RX_ERR_DLC,
  ICM_RX_ERR_COUNTER_REPEAT,
  ICM_RX_COUNTER_JUMP,
  ICM_RX_HW_OVERRUN,
  ICM_RX_SW_OVERRUN
} icm_rx_err_t;

/* Task_CAN_RX */
void icm_data_rx_store(const icm_can_values_t *values, uint32_t tick);
void icm_data_rx_count(icm_rx_err_t kind, uint32_t n);

/* Task_Acquisition */
void icm_data_read_raw(icm_raw_t *out);
void icm_data_write_validated(const icm_validated_t *in);

/* Task_Processing */
void icm_data_read_validated(icm_validated_t *out);
void icm_data_write_physical(const icm_physical_t *in);

/* Task_Diagnostics */
void icm_data_write_faults(const icm_faults_t *in);

/*
 * Task_Display, Task_Diagnostics, Task_Logging: a consistent copy of the
 * whole structure. Returns false if the copy was torn (seq_begin != seq_end),
 * which must never happen in a correct build.
 *
 * race_window_us widens the copy on purpose, for the race test only: the
 * first half is copied, the caller spins for that many microseconds, then the
 * second half is copied. Production callers pass 0.
 */
bool icm_data_snapshot(ICM_Data *out, unsigned race_window_us);

/* Counters of the consistency check above, for the logs and the tests. */
void icm_data_consistency_stats(uint32_t *snapshots, uint32_t *torn);

#endif /* ICM_DATA_H */
