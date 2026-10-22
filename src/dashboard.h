/*
 * ICM — text-mode instrument cluster renderer.
 * Osvaldo Janeri Filho <janeri@gmail.com>
 *
 * Why text and not graphics. The prototype runs on Trampoline's POSIX target,
 * inside a Linux process with no framebuffer of its own, and the course is
 * about deterministic scheduling rather than about drawing. A graphical
 * front end would add a second process, an IPC channel and a renderer whose
 * timing we do not control, and every one of those would blur the
 * measurements that Task_Display is supposed to produce. A fixed-size text
 * frame keeps the display task's job identical every period (same bytes,
 * same work), so its execution time is a property of the code and not of a
 * GPU driver. On a real cluster this module would be replaced by the gauge
 * driver; the interface (one const snapshot in, one frame out) would not
 * change.
 *
 * Pure function: no RTOS calls, no I/O. Task_Display hands the result to the
 * console; tests/unit/test_dashboard.c checks the frame contents directly.
 */
#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "icm_types.h"

#define DASH_WIDTH 72

/* Extra context that is not part of ICM_Data. */
typedef struct {
  double uptime_s;
  uint32_t frame;          /* display frame counter, drives blinking */
  bool ansi;               /* colours and cursor-home for a live terminal */
  bool can_online;
  uint32_t torn_snapshots; /* consistency check counter, should stay 0 */
} dash_ctx_t;

/* Renders one frame into buf. Returns the number of bytes written (always
 * NUL-terminated, truncated if cap is too small). */
size_t dash_render(char *buf, size_t cap, const ICM_Data *d,
                   const dash_ctx_t *ctx);

/* Is the turn indicator lamp lit in this frame? Exposed for the tests. */
bool dash_blink_on(uint32_t frame);

#endif /* DASHBOARD_H */
