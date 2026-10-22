/*
 * ICM — signal processing: validate -> scale -> filter.
 * Osvaldo Janeri Filho <janeri@gmail.com>
 *
 * The professor's pipeline is receive -> validate -> scale -> filter ->
 * display. Receive is Task_CAN_RX (can_db.c), display is Task_Display
 * (dashboard.c). The three steps in the middle are here, as pure functions,
 * so they can be unit tested without an operating system underneath.
 */
#ifndef SIGNAL_PROC_H
#define SIGNAL_PROC_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  SIG_NEVER_RECEIVED = 0, /* power-up, still inside the first timeout window */
  SIG_VALID,
  SIG_OUT_OF_RANGE,       /* includes the SNA "not available" code           */
  SIG_TIMEOUT             /* message silent for longer than its timeout      */
} sig_status_t;

const char *sp_status_name(sig_status_t status);

/* Range checks on the raw value, before scaling (ICM-S-02). */
sig_status_t sp_validate_speed(uint16_t raw);
sig_status_t sp_validate_rpm(uint16_t raw);
sig_status_t sp_validate_fuel(uint8_t raw);
sig_status_t sp_validate_temp(uint8_t raw);

/* Raw to physical (ICM-F-05). Only call on values that validated. */
float sp_scale_speed(uint16_t raw); /* km/h  */
float sp_scale_rpm(uint16_t raw);   /* rpm   */
float sp_scale_fuel(uint8_t raw);   /* %     */
float sp_scale_temp(uint8_t raw);   /* degC  */

/*
 * Freshness of a message (ICM-S-01). `last_rx_tick` is the tick of the last
 * accepted frame, or the start-up tick if none has arrived yet. Unsigned
 * subtraction makes the check correct across the 32-bit tick wrap.
 */
bool sp_timed_out(uint32_t now_tick, uint32_t last_rx_tick,
                  uint32_t timeout_ticks);

/*
 * Combines freshness and range into one status. Timeout wins over range:
 * a value that stopped arriving is not "in range", it is unknown.
 */
sig_status_t sp_signal_status(bool ever_received, uint32_t now_tick,
                              uint32_t last_rx_tick, uint32_t timeout_ticks,
                              sig_status_t range_status);

/* First-order low-pass filter (ICM-F-06). */
typedef struct {
  float y;
  bool primed;
} sp_lpf_t;

float sp_lpf_alpha(float dt_s, float tau_s);
void sp_lpf_reset(sp_lpf_t *f);
float sp_lpf_step(sp_lpf_t *f, float x, float alpha);

/* Hysteresis comparators for warning lamps (ICM-F-08). */
bool sp_hyst_low(bool active, float x, float on_at_or_below, float off_above);
bool sp_hyst_high(bool active, float x, float on_at_or_above, float off_below);

#endif /* SIGNAL_PROC_H */
