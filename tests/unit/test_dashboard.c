/*
 * ICM — unit tests for the text dashboard (src/dashboard.c).
 * Osvaldo Janeri Filho <janeri@gmail.com>
 */
#include <string.h>

#include "check.h"
#include "dashboard.h"
#include "icm_config.h"

static char buf[8192];

static ICM_Data all_valid(void)
{
  ICM_Data d;
  memset(&d, 0, sizeof d);
  for (int s = 0; s < ICM_SIG_COUNT; s++) {
    d.phys.valid[s] = true;
  }
  d.phys.speed_kmh = 120.0f;
  d.phys.rpm = 3000.0f;
  d.phys.fuel_pct = 65.0f;
  d.phys.coolant_c = 90.0f;
  return d;
}

static void render(const ICM_Data *d, uint32_t frame)
{
  dash_ctx_t ctx = {.uptime_s = 1.0, .frame = frame, .ansi = false,
                    .can_online = true, .torn_snapshots = 0};
  dash_render(buf, sizeof buf, d, &ctx);
}

/* ICM-F-07: every line of the frame is exactly DASH_WIDTH columns, whatever
 * is on it (the border must not move as values change). */
static void test_ICM_F_07_fixed_width_frame(void)
{
  ICM_Data d = all_valid();
  d.phys.turn_bits = ICM_TURN_HAZARD;
  d.phys.warning_bits = 0xFF;
  d.phys.low_fuel = d.phys.overheat = d.phys.redline = true;
  d.faults.confirmed_mask = 0xFFFFFFFFu;
  render(&d, 0);
  int lines = 0;
  for (char *line = strtok(buf, "\r\n"); line; line = strtok(NULL, "\r\n")) {
    CHECK((int)strlen(line) == DASH_WIDTH);
    lines++;
  }
  CHECK(lines >= 15);
}

/* ICM-F-07: values shown with their units. */
static void test_ICM_F_07_shows_values(void)
{
  ICM_Data d = all_valid();
  render(&d, 0);
  CHECK(strstr(buf, " 120.0 km/h") != NULL);
  CHECK(strstr(buf, "3000 rpm") != NULL);
  CHECK(strstr(buf, "65.0 %") != NULL);
  CHECK(strstr(buf, "90 C") != NULL);
  CHECK(strstr(buf, "MSG   -") != NULL); /* no message */
}

/* ICM-S-05: an invalid signal is shown as "---", never as a number. */
static void test_ICM_S_05_invalid_signal_shows_dashes(void)
{
  ICM_Data d = all_valid();
  d.phys.valid[ICM_SIG_SPEED] = false;
  d.phys.speed_kmh = 0.0f;
  d.faults.confirmed_mask = 1u << DTC_M2_TIMEOUT;
  render(&d, 0);
  CHECK(strstr(buf, "--- km/h") != NULL);
  CHECK(strstr(buf, "0.0 km/h") == NULL);
  CHECK(strstr(buf, "COMM LOST: VEHICLE SPEED (M2)") != NULL);
  CHECK(strstr(buf, "[FAULT]") != NULL);
}

/* ICM-F-07: turn indicator blinks at 350 ms on / 350 ms off, 86 flashes per
 * minute, inside the 60..120 of UNECE R48. */
static void test_ICM_F_07_blink_rate(void)
{
  int toggles = 0;
  bool prev = dash_blink_on(0);
  for (uint32_t f = 1; f < 1200; f++) { /* 60 s of 50 ms frames */
    bool now = dash_blink_on(f);
    if (now != prev) toggles++;
    prev = now;
  }
  int flashes_per_min = toggles / 2;
  CHECK(flashes_per_min >= 60 && flashes_per_min <= 120);
  CHECK(dash_blink_on(0) && dash_blink_on(6) && !dash_blink_on(7));

  ICM_Data d = all_valid();
  d.phys.turn_bits = ICM_TURN_LEFT;
  render(&d, 0);
  CHECK(strstr(buf, "<<< LEFT") != NULL);
  render(&d, 7);
  CHECK(strstr(buf, "<<< LEFT") == NULL);
}

/* ICM-S-05: with the body message lost, no turn indicator and no
 * bus-requested telltale is shown, even if the last value said "on". */
static void test_ICM_S_05_body_loss_darkens_indicators(void)
{
  ICM_Data d = all_valid();
  d.phys.valid[ICM_SIG_BODY] = false;
  d.phys.turn_bits = ICM_TURN_RIGHT;
  d.phys.warning_bits = ICM_WARN_OIL;
  render(&d, 0);
  CHECK(strstr(buf, "RIGHT >>>") == NULL);
  CHECK(strstr(buf, "[OIL]") == NULL);
}

/* ICM-F-08: cluster warnings light their lamps and messages. */
static void test_ICM_F_08_warning_lamps(void)
{
  ICM_Data d = all_valid();
  d.phys.low_fuel = true;
  d.phys.overheat = true;
  d.phys.redline = true;
  render(&d, 0);
  CHECK(strstr(buf, "[FUEL]") != NULL);
  CHECK(strstr(buf, "[TEMP]") != NULL);
  CHECK(strstr(buf, "RED") != NULL);
  CHECK(strstr(buf, "LOW FUEL") != NULL);
  CHECK(strstr(buf, "ENGINE OVERHEAT") != NULL);
}

int main(void)
{
  RUN(test_ICM_F_07_fixed_width_frame);
  RUN(test_ICM_F_07_shows_values);
  RUN(test_ICM_S_05_invalid_signal_shows_dashes);
  RUN(test_ICM_F_07_blink_rate);
  RUN(test_ICM_S_05_body_loss_darkens_indicators);
  RUN(test_ICM_F_08_warning_lamps);
  return CHECK_REPORT();
}
