/*
 * ICM — signal processing implementation.
 * Osvaldo Janeri Filho <janeri@gmail.com>
 */
#include "signal_proc.h"

#include "icm_config.h"

const char *sp_status_name(sig_status_t status)
{
  switch (status) {
  case SIG_NEVER_RECEIVED: return "NEVER_RX";
  case SIG_VALID:          return "VALID";
  case SIG_OUT_OF_RANGE:   return "OUT_OF_RANGE";
  case SIG_TIMEOUT:        return "TIMEOUT";
  default:                 return "?";
  }
}

/*
 * SNA (0xFFFF / 0xFF) is above every max raw value, so the single comparison
 * also rejects it. That is deliberate rather than lucky: the raw maxima in
 * icm_config.h were chosen with the SNA codes outside the valid band.
 */
sig_status_t sp_validate_speed(uint16_t raw)
{
  return (raw <= ICM_SPEED_MAX_RAW) ? SIG_VALID : SIG_OUT_OF_RANGE;
}

sig_status_t sp_validate_rpm(uint16_t raw)
{
  return (raw <= ICM_RPM_MAX_RAW) ? SIG_VALID : SIG_OUT_OF_RANGE;
}

sig_status_t sp_validate_fuel(uint8_t raw)
{
  return (raw <= ICM_FUEL_MAX_RAW) ? SIG_VALID : SIG_OUT_OF_RANGE;
}

/* The lower bound (-40 degC) is raw 0, so only the upper one can fail. */
sig_status_t sp_validate_temp(uint8_t raw)
{
  return (raw <= ICM_TEMP_MAX_RAW) ? SIG_VALID : SIG_OUT_OF_RANGE;
}

float sp_scale_speed(uint16_t raw) { return (float)raw * ICM_SPEED_FACTOR; }
float sp_scale_rpm(uint16_t raw)   { return (float)raw * ICM_RPM_FACTOR; }
float sp_scale_fuel(uint8_t raw)   { return (float)raw * ICM_FUEL_FACTOR; }
float sp_scale_temp(uint8_t raw)   { return (float)raw + ICM_TEMP_OFFSET_C; }

/*
 * Strictly greater: with a 3-period timeout, a message that is exactly three
 * periods old has missed two frames, and that is still tolerated (ICM-S-01).
 */
bool sp_timed_out(uint32_t now_tick, uint32_t last_rx_tick,
                  uint32_t timeout_ticks)
{
  return (uint32_t)(now_tick - last_rx_tick) > timeout_ticks;
}

sig_status_t sp_signal_status(bool ever_received, uint32_t now_tick,
                              uint32_t last_rx_tick, uint32_t timeout_ticks,
                              sig_status_t range_status)
{
  if (sp_timed_out(now_tick, last_rx_tick, timeout_ticks)) {
    return SIG_TIMEOUT;
  }
  if (!ever_received) {
    return SIG_NEVER_RECEIVED;
  }
  return range_status;
}

float sp_lpf_alpha(float dt_s, float tau_s)
{
  return dt_s / (tau_s + dt_s);
}

void sp_lpf_reset(sp_lpf_t *f)
{
  f->y = 0.0f;
  f->primed = false;
}

/*
 * The first sample after a reset is taken as-is. Without that, the fuel gauge
 * (tau = 5 s) would take about 25 s to climb from 0 to the real level after
 * every ignition-on or every recovered fault, and the low-fuel lamp would
 * light up on a full tank in the meantime.
 */
float sp_lpf_step(sp_lpf_t *f, float x, float alpha)
{
  if (!f->primed) {
    f->y = x;
    f->primed = true;
  } else {
    f->y += alpha * (x - f->y);
  }
  return f->y;
}

bool sp_hyst_low(bool active, float x, float on_at_or_below, float off_above)
{
  return active ? (x <= off_above) : (x <= on_at_or_below);
}

bool sp_hyst_high(bool active, float x, float on_at_or_above, float off_below)
{
  return active ? (x >= off_below) : (x >= on_at_or_above);
}
