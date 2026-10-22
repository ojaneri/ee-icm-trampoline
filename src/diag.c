/*
 * ICM — DTC debouncing implementation.
 * Osvaldo Janeri Filho <janeri@gmail.com>
 */
#include "diag.h"

#include "icm_config.h"

typedef struct {
  uint8_t code;
  const char *name;
  uint8_t confirm_cycles;
  uint8_t heal_cycles;
} dtc_info_t;

/*
 * Two entries deviate from the default 3/10 cycles:
 *
 * - Task overrun confirms on the first cycle. A missed deadline already is a
 *   fact, not a noisy measurement, and there is nothing to filter out. It
 *   heals only after 5 s so that a task overrunning every other period does
 *   not make the DTC flicker on and off.
 *
 * - Timeouts confirm on the first cycle too, because the 3-period timeout in
 *   signal_proc.c already is the debounce. Stacking another 300 ms on top
 *   would only make the driver wait longer for the same verdict.
 */
static const dtc_info_t dtc_table[DTC_COUNT] = {
    [DTC_M1_TIMEOUT]    = {0x01, "M1_ENGINE_SPEED_TIMEOUT", 1, ICM_DTC_HEAL_CYCLES},
    [DTC_M2_TIMEOUT]    = {0x02, "M2_VEHICLE_SPEED_TIMEOUT", 1, ICM_DTC_HEAL_CYCLES},
    [DTC_M3_TIMEOUT]    = {0x03, "M3_ENGINE_STATUS_TIMEOUT", 1, ICM_DTC_HEAL_CYCLES},
    [DTC_M4_TIMEOUT]    = {0x04, "M4_BODY_STATUS_TIMEOUT", 1, ICM_DTC_HEAL_CYCLES},
    [DTC_SPEED_RANGE]   = {0x11, "SPEED_OUT_OF_RANGE", ICM_DTC_CONFIRM_CYCLES, ICM_DTC_HEAL_CYCLES},
    [DTC_RPM_RANGE]     = {0x12, "RPM_OUT_OF_RANGE", ICM_DTC_CONFIRM_CYCLES, ICM_DTC_HEAL_CYCLES},
    [DTC_FUEL_RANGE]    = {0x13, "FUEL_OUT_OF_RANGE", ICM_DTC_CONFIRM_CYCLES, ICM_DTC_HEAL_CYCLES},
    [DTC_TEMP_RANGE]    = {0x14, "TEMP_OUT_OF_RANGE", ICM_DTC_CONFIRM_CYCLES, ICM_DTC_HEAL_CYCLES},
    [DTC_CAN_INTEGRITY] = {0x21, "CAN_E2E_ERROR", ICM_DTC_CONFIRM_CYCLES, ICM_DTC_HEAL_CYCLES},
    [DTC_CAN_OVERRUN]   = {0x22, "CAN_RX_OVERRUN", 1, ICM_DTC_HEAL_CYCLES},
    [DTC_TASK_OVERRUN]  = {0x31, "TASK_DEADLINE_OVERRUN", 1, 50},
};

void dtc_reset(dtc_state_t *s)
{
  s->fail_streak = 0;
  s->pass_streak = 0;
  s->confirmed = false;
  s->occurrences = 0;
}

dtc_event_t dtc_update(dtc_state_t *s, bool failed, uint8_t confirm_cycles,
                       uint8_t heal_cycles)
{
  if (failed) {
    s->pass_streak = 0;
    if (s->fail_streak < UINT8_MAX) {
      s->fail_streak++;
    }
    if (!s->confirmed && s->fail_streak >= confirm_cycles) {
      s->confirmed = true;
      s->occurrences++;
      return DTC_BECAME_CONFIRMED;
    }
  } else {
    s->fail_streak = 0;
    if (s->pass_streak < UINT8_MAX) {
      s->pass_streak++;
    }
    if (s->confirmed && s->pass_streak >= heal_cycles) {
      s->confirmed = false;
      return DTC_BECAME_HEALED;
    }
  }
  return DTC_NO_CHANGE;
}

uint8_t dtc_code(dtc_id_t id)
{
  return (id < DTC_COUNT) ? dtc_table[id].code : 0u;
}

const char *dtc_name(dtc_id_t id)
{
  return (id < DTC_COUNT) ? dtc_table[id].name : "?";
}

uint8_t dtc_confirm_cycles(dtc_id_t id)
{
  return (id < DTC_COUNT) ? dtc_table[id].confirm_cycles : 1u;
}

uint8_t dtc_heal_cycles(dtc_id_t id)
{
  return (id < DTC_COUNT) ? dtc_table[id].heal_cycles : 1u;
}
