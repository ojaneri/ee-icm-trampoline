/*
 * ICM — text-mode instrument cluster renderer implementation.
 * Osvaldo Janeri Filho <janeri@gmail.com>
 */
#include "dashboard.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "icm_config.h"

#define INNER (DASH_WIDTH - 4) /* "| " + content + " |" */

#define ANSI_RED    "\033[1;31m"
#define ANSI_AMBER  "\033[1;33m"
#define ANSI_GREEN  "\033[1;32m"
#define ANSI_RESET  "\033[0m"

/* A string builder that knows the difference between bytes and columns, so
 * colour escapes do not break the right-hand border. */
typedef struct {
  char *buf;
  size_t cap;
  size_t len;
  int col;
  bool ansi;
} sb_t;

static void sb_raw(sb_t *sb, const char *s)
{
  size_t n = strlen(s);
  if (sb->len + n >= sb->cap) {
    n = (sb->cap > sb->len + 1) ? sb->cap - sb->len - 1 : 0;
  }
  memcpy(sb->buf + sb->len, s, n);
  sb->len += n;
  sb->buf[sb->len] = '\0';
}

static void sb_text(sb_t *sb, const char *fmt, ...)
{
  char tmp[256];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
  va_end(ap);
  if (n < 0) {
    return;
  }
  sb_raw(sb, tmp);
  sb->col += (int)strlen(tmp);
}

static void sb_color(sb_t *sb, const char *color)
{
  if (sb->ansi) {
    sb_raw(sb, color);
  }
}

static void line_begin(sb_t *sb)
{
  sb->col = 0;
  sb_text(sb, "| ");
}

static void line_end(sb_t *sb)
{
  while (sb->col < DASH_WIDTH - 2) {
    sb_text(sb, " ");
  }
  sb_text(sb, " |");
  sb_raw(sb, "\r\n"); /* the POSIX target puts the tty in raw mode */
}

static void rule(sb_t *sb)
{
  sb->col = 0;
  sb_text(sb, "+");
  for (int i = 0; i < DASH_WIDTH - 2; i++) {
    sb_text(sb, "-");
  }
  sb_text(sb, "+");
  sb_raw(sb, "\r\n");
}

static void bar(sb_t *sb, float value, float lo, float hi, int cells, bool valid)
{
  sb_text(sb, "[");
  if (!valid) {
    for (int i = 0; i < cells; i++) {
      sb_text(sb, "?");
    }
  } else {
    float frac = (value - lo) / (hi - lo);
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    int filled = (int)(frac * (float)cells + 0.5f);
    for (int i = 0; i < cells; i++) {
      sb_text(sb, i < filled ? "#" : ".");
    }
  }
  sb_text(sb, "]");
}

bool dash_blink_on(uint32_t frame)
{
  return ((frame / ICM_BLINK_HALF_FRAMES) % 2u) == 0u;
}

static bool dtc_on(const ICM_Data *d, dtc_id_t id)
{
  return (d->faults.confirmed_mask & (1u << id)) != 0u;
}

typedef struct {
  const char *label;
  bool on;
  const char *color;
} lamp_t;

size_t dash_render(char *buf, size_t cap, const ICM_Data *d,
                   const dash_ctx_t *ctx)
{
  sb_t sb = {.buf = buf, .cap = cap, .len = 0, .col = 0, .ansi = ctx->ansi};
  const icm_physical_t *p = &d->phys;
  buf[0] = '\0';

  if (ctx->ansi) {
    sb_raw(&sb, ctx->frame == 0u ? "\033[2J\033[H" : "\033[H");
  }

  rule(&sb);
  line_begin(&sb);
  sb_text(&sb, "ICM Instrument Cluster - Trampoline OSEK/VDX");
  while (sb.col < DASH_WIDTH - 17) sb_text(&sb, " ");
  sb_text(&sb, "t=%8.2f s", ctx->uptime_s);
  line_end(&sb);
  rule(&sb);

  /* Turn indicators. Hazard lights both. Both go dark when the body
   * message times out: showing a stale "left" would be a lie. */
  bool body_ok = p->valid[ICM_SIG_BODY];
  bool hazard = body_ok && (p->turn_bits & ICM_TURN_HAZARD);
  bool left = body_ok && ((p->turn_bits & ICM_TURN_LEFT) || hazard);
  bool right = body_ok && ((p->turn_bits & ICM_TURN_RIGHT) || hazard);
  bool lit = dash_blink_on(ctx->frame);
  line_begin(&sb);
  if (left && lit) {
    sb_color(&sb, ANSI_GREEN);
    sb_text(&sb, "<<< LEFT");
    sb_color(&sb, ANSI_RESET);
  } else {
    sb_text(&sb, "        ");
  }
  while (sb.col < 30) sb_text(&sb, " ");
  sb_text(&sb, hazard ? "HAZARD" : "      ");
  while (sb.col < DASH_WIDTH - 12) sb_text(&sb, " ");
  if (right && lit) {
    sb_color(&sb, ANSI_GREEN);
    sb_text(&sb, "RIGHT >>>");
    sb_color(&sb, ANSI_RESET);
  }
  line_end(&sb);

  /* Gauges */
  line_begin(&sb);
  sb_text(&sb, "SPEED ");
  bar(&sb, p->speed_kmh, 0.0f, ICM_SPEED_MAX_KMH, 40, p->valid[ICM_SIG_SPEED]);
  if (p->valid[ICM_SIG_SPEED]) {
    sb_text(&sb, " %6.1f km/h", p->speed_kmh);
  } else {
    sb_text(&sb, "    --- km/h");
  }
  line_end(&sb);

  line_begin(&sb);
  sb_text(&sb, "RPM   ");
  bar(&sb, p->rpm, 0.0f, ICM_RPM_MAX, 40, p->valid[ICM_SIG_RPM]);
  if (p->valid[ICM_SIG_RPM]) {
    sb_text(&sb, " %6.0f rpm ", p->rpm);
    if (p->redline) {
      sb_color(&sb, ANSI_RED);
      sb_text(&sb, "RED");
      sb_color(&sb, ANSI_RESET);
    }
  } else {
    sb_text(&sb, "    --- rpm");
  }
  line_end(&sb);

  line_begin(&sb);
  sb_text(&sb, "FUEL  ");
  bar(&sb, p->fuel_pct, 0.0f, ICM_FUEL_MAX_PCT, 20, p->valid[ICM_SIG_FUEL]);
  if (p->valid[ICM_SIG_FUEL]) {
    sb_text(&sb, " %5.1f %%", p->fuel_pct);
  } else {
    sb_text(&sb, "   --- %%");
  }
  while (sb.col < 40) sb_text(&sb, " ");
  sb_text(&sb, "TEMP ");
  bar(&sb, p->coolant_c, 40.0f, 130.0f, 10, p->valid[ICM_SIG_TEMP]);
  if (p->valid[ICM_SIG_TEMP]) {
    sb_text(&sb, " %4.0f C", p->coolant_c);
  } else {
    sb_text(&sb, "  --- C");
  }
  line_end(&sb);

  /* Telltales. Bus-requested lamps go dark with the body message, the
   * cluster's own (fuel, temp, fault) do not depend on it. */
  uint8_t w = body_ok ? p->warning_bits : 0u;
  bool any_dtc = d->faults.confirmed_mask != 0u;
  const lamp_t lamps[] = {
      {"OIL", (w & ICM_WARN_OIL) != 0, ANSI_RED},
      {"BAT", (w & ICM_WARN_BATTERY) != 0, ANSI_RED},
      {"BRK", (w & ICM_WARN_BRAKE) != 0, ANSI_RED},
      {"MIL", (w & ICM_WARN_MIL) != 0, ANSI_AMBER},
      {"ABS", (w & ICM_WARN_ABS) != 0, ANSI_AMBER},
      {"SRS", (w & ICM_WARN_AIRBAG) != 0, ANSI_RED},
      {"BELT", (w & ICM_WARN_SEATBELT) != 0, ANSI_RED},
      {"DOOR", (w & ICM_WARN_DOOR) != 0, ANSI_RED},
      {"FUEL", p->low_fuel, ANSI_AMBER},
      {"TEMP", p->overheat, ANSI_RED},
      {"FAULT", any_dtc, ANSI_AMBER},
  };
  /* Two rows: what other ECUs asked for, then what the cluster decided. */
  line_begin(&sb);
  sb_text(&sb, "LAMPS");
  for (size_t i = 0; i < sizeof lamps / sizeof lamps[0]; i++) {
    if (i == 8) {
      line_end(&sb);
      line_begin(&sb);
      sb_text(&sb, "     ");
    }
    sb_text(&sb, " ");
    if (lamps[i].on) {
      sb_color(&sb, lamps[i].color);
      sb_text(&sb, "[%s]", lamps[i].label);
      sb_color(&sb, ANSI_RESET);
    } else {
      sb_text(&sb, " ");
      for (size_t k = 0; k < strlen(lamps[i].label); k++) sb_text(&sb, ".");
      sb_text(&sb, " ");
    }
  }
  line_end(&sb);

  /* Messages, most important first, at most three lines. */
  const char *msgs[8];
  int nmsg = 0;
  if (!ctx->can_online) msgs[nmsg++] = "CAN CONTROLLER OFFLINE";
  if (dtc_on(d, DTC_TASK_OVERRUN)) msgs[nmsg++] = "TIMING FAULT: TASK DEADLINE OVERRUN";
  if (dtc_on(d, DTC_M2_TIMEOUT)) msgs[nmsg++] = "COMM LOST: VEHICLE SPEED (M2)";
  if (dtc_on(d, DTC_M1_TIMEOUT)) msgs[nmsg++] = "COMM LOST: ENGINE SPEED (M1)";
  if (dtc_on(d, DTC_M3_TIMEOUT)) msgs[nmsg++] = "COMM LOST: ENGINE STATUS (M3)";
  if (dtc_on(d, DTC_M4_TIMEOUT)) msgs[nmsg++] = "COMM LOST: BODY STATUS (M4)";
  if (nmsg < 8 && (dtc_on(d, DTC_SPEED_RANGE) || dtc_on(d, DTC_RPM_RANGE) ||
                   dtc_on(d, DTC_FUEL_RANGE) || dtc_on(d, DTC_TEMP_RANGE)))
    msgs[nmsg++] = "SENSOR SIGNAL OUT OF RANGE";
  if (nmsg < 8 && dtc_on(d, DTC_CAN_INTEGRITY)) msgs[nmsg++] = "CAN DATA CORRUPTED (E2E)";
  if (nmsg < 8 && dtc_on(d, DTC_CAN_OVERRUN)) msgs[nmsg++] = "CAN RECEIVE OVERRUN";
  if (nmsg < 8 && p->overheat) msgs[nmsg++] = "ENGINE OVERHEAT - STOP SAFELY";
  if (nmsg < 8 && p->low_fuel) msgs[nmsg++] = "LOW FUEL";

  for (int i = 0; i < 3; i++) {
    line_begin(&sb);
    if (i < nmsg) {
      sb_text(&sb, "MSG   %s", msgs[i]);
    } else if (i == 0) {
      sb_text(&sb, "MSG   -");
    }
    line_end(&sb);
  }

  rule(&sb);
  line_begin(&sb);
  sb_text(&sb, "CAN rx %u/%u/%u/%u  crc %u  cnt %u  ovr %u", d->raw.rx_ok[0],
          d->raw.rx_ok[1], d->raw.rx_ok[2], d->raw.rx_ok[3], d->raw.err_crc,
          d->raw.err_counter_repeat, d->raw.hw_overruns + d->raw.sw_overruns);
  line_end(&sb);
  line_begin(&sb);
  sb_text(&sb, "RTOS overruns %u  deadline misses %u  api errors %u  torn %u",
          d->faults.task_overruns, d->faults.deadline_misses,
          d->faults.os_api_errors, ctx->torn_snapshots);
  line_end(&sb);
  rule(&sb);
  return sb.len;
}
