/*
 * ICM — diagnostic trouble codes with counter-based debouncing.
 * Osvaldo Janeri Filho <janeri@gmail.com>
 *
 * A raw fault (one timed-out message, one out-of-range sample) is not yet a
 * diagnosis. Task_Diagnostics feeds the raw pass/fail of each monitor into
 * dtc_update() every 100 ms; a DTC is confirmed after N consecutive failing
 * cycles and healed after M consecutive passing ones. This is the counter
 * debouncing of AUTOSAR DEM, reduced to what a cluster needs.
 *
 * The codes are project-internal. We did not borrow SAE J2012 U-codes because
 * two of our messages would come from the same ECU and map to the same code,
 * and a cluster that cannot tell its faults apart is not much of a test bed.
 */
#ifndef DIAG_H
#define DIAG_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  DTC_M1_TIMEOUT = 0, /* 0x01 engine speed message lost           */
  DTC_M2_TIMEOUT,     /* 0x02 vehicle speed message lost          */
  DTC_M3_TIMEOUT,     /* 0x03 engine status message lost          */
  DTC_M4_TIMEOUT,     /* 0x04 body status message lost            */
  DTC_SPEED_RANGE,    /* 0x11 vehicle speed out of range / SNA    */
  DTC_RPM_RANGE,      /* 0x12 engine speed out of range / SNA     */
  DTC_FUEL_RANGE,     /* 0x13 fuel level out of range / SNA       */
  DTC_TEMP_RANGE,     /* 0x14 coolant temperature out of range    */
  DTC_CAN_INTEGRITY,  /* 0x21 CRC or alive-counter errors         */
  DTC_CAN_OVERRUN,    /* 0x22 receive FIFO overrun, frames lost   */
  DTC_TASK_OVERRUN,   /* 0x31 a task missed its deadline          */
  DTC_COUNT
} dtc_id_t;

typedef struct {
  uint8_t fail_streak;
  uint8_t pass_streak;
  bool confirmed;
  uint16_t occurrences; /* times it went from healed to confirmed */
} dtc_state_t;

typedef enum {
  DTC_NO_CHANGE = 0,
  DTC_BECAME_CONFIRMED,
  DTC_BECAME_HEALED
} dtc_event_t;

void dtc_reset(dtc_state_t *s);
dtc_event_t dtc_update(dtc_state_t *s, bool failed, uint8_t confirm_cycles,
                       uint8_t heal_cycles);

uint8_t dtc_code(dtc_id_t id);
const char *dtc_name(dtc_id_t id);
uint8_t dtc_confirm_cycles(dtc_id_t id);
uint8_t dtc_heal_cycles(dtc_id_t id);

#endif /* DIAG_H */
