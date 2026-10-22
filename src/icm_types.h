/*
 * ICM — the shared data structure, ICM_Data.
 * Osvaldo Janeri Filho <janeri@gmail.com>
 *
 * Only types live here, no functions and no RTOS headers, so that pure modules
 * (dashboard.c) and the host unit tests can use them. The one instance of
 * ICM_Data is private to icm_data.c and reachable only through the accessors
 * declared in icm_data.h, all of which hold ICM_Resource.
 *
 * The structure is split by writer. Each section has exactly one task that
 * writes it, which is documented next to the section and enforced by the
 * accessor API (there is no function that writes someone else's section).
 * Single writer is not enough on its own: a reader preempted half-way through
 * a copy by the writer gets half old, half new data. That is what the
 * resource is for, and what tests/integration/test_race.py demonstrates.
 */
#ifndef ICM_TYPES_H
#define ICM_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#include "can_db.h"
#include "diag.h"
#include "signal_proc.h"

typedef enum {
  ICM_SIG_SPEED = 0, /* from M2 */
  ICM_SIG_RPM,       /* from M1 */
  ICM_SIG_FUEL,      /* from M3 */
  ICM_SIG_TEMP,      /* from M3 */
  ICM_SIG_BODY,      /* from M4: turn signals and telltales */
  ICM_SIG_COUNT
} icm_sig_t;

/* Written by Task_CAN_RX: last accepted value of every signal, as received. */
typedef struct {
  uint16_t engine_speed_raw;
  uint16_t vehicle_speed_raw;
  uint8_t fuel_raw;
  uint8_t coolant_raw;
  uint8_t turn_bits;
  uint8_t warning_bits;
  uint32_t last_rx_tick[ICM_MSG_COUNT];
  bool ever_rx[ICM_MSG_COUNT];
  uint32_t rx_ok[ICM_MSG_COUNT];
  /* cumulative reception error counters */
  uint32_t err_crc;
  uint32_t err_dlc;
  uint32_t err_counter_repeat;
  uint32_t counter_jumps;
  uint32_t hw_overruns; /* frames the emulated controller had to drop  */
  uint32_t sw_overruns; /* frames the driver queue had to drop        */
} icm_raw_t;

/* Written by Task_Acquisition: the same values, with a verdict on each. */
typedef struct {
  sig_status_t status[ICM_SIG_COUNT];
  uint16_t engine_speed_raw;
  uint16_t vehicle_speed_raw;
  uint8_t fuel_raw;
  uint8_t coolant_raw;
  uint8_t turn_bits;
  uint8_t warning_bits;
  uint32_t sample_tick;
} icm_validated_t;

/*
 * Written by Task_Processing: physical, filtered values ready to show.
 * seq_begin and seq_end bracket the payload and are always written equal.
 * A copy in which they differ was torn by a concurrent write; the readers
 * check this on every snapshot and count it (ICM-D-04).
 */
typedef struct {
  uint32_t seq_begin;
  float speed_kmh;
  float rpm;
  float fuel_pct;
  float coolant_c;
  bool valid[ICM_SIG_COUNT];
  uint8_t turn_bits;
  uint8_t warning_bits;
  bool low_fuel;
  bool overheat;
  bool redline;
  uint32_t seq_end;
} icm_physical_t;

/* Written by Task_Diagnostics. */
typedef struct {
  uint32_t confirmed_mask; /* bit n set = DTC n confirmed */
  uint16_t occurrences[DTC_COUNT];
  uint32_t task_overruns; /* E_OS_LIMIT on alarm activation, all tasks */
  uint32_t deadline_misses; /* measured response time > deadline        */
  uint32_t os_api_errors;
} icm_faults_t;

typedef struct {
  icm_raw_t raw;             /* writer: Task_CAN_RX       */
  icm_validated_t validated; /* writer: Task_Acquisition  */
  icm_physical_t phys;       /* writer: Task_Processing   */
  icm_faults_t faults;       /* writer: Task_Diagnostics  */
} ICM_Data;

#endif /* ICM_TYPES_H */
