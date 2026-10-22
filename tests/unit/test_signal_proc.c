/*
 * ICM — unit tests for validate / scale / filter (src/signal_proc.c).
 * Osvaldo Janeri Filho <janeri@gmail.com>
 *
 * Boundary values in the spirit of Project #1: the edges of every valid
 * range, one step inside and one step outside, plus the SNA codes.
 */
#include "check.h"
#include "icm_config.h"
#include "signal_proc.h"

/* ICM-S-02: vehicle speed 0..250 km/h = raw 0..25000. */
static void test_ICM_S_02_speed_range_boundaries(void)
{
  CHECK(sp_validate_speed(0) == SIG_VALID);
  CHECK(sp_validate_speed(25000) == SIG_VALID);
  CHECK(sp_validate_speed(25001) == SIG_OUT_OF_RANGE);
  CHECK(sp_validate_speed(30000) == SIG_OUT_OF_RANGE); /* 300 km/h */
  CHECK(sp_validate_speed(0xFFFF) == SIG_OUT_OF_RANGE); /* SNA */
}

/* ICM-S-02: engine speed 0..8000 rpm = raw 0..32000. */
static void test_ICM_S_02_rpm_range_boundaries(void)
{
  CHECK(sp_validate_rpm(0) == SIG_VALID);
  CHECK(sp_validate_rpm(32000) == SIG_VALID);
  CHECK(sp_validate_rpm(32001) == SIG_OUT_OF_RANGE);
  CHECK(sp_validate_rpm(36000) == SIG_OUT_OF_RANGE); /* 9000 rpm */
  CHECK(sp_validate_rpm(0xFFFF) == SIG_OUT_OF_RANGE);
}

/* ICM-S-02: fuel 0..100 % = raw 0..200; coolant -40..150 degC = raw 0..190. */
static void test_ICM_S_02_fuel_and_temp_boundaries(void)
{
  CHECK(sp_validate_fuel(0) == SIG_VALID);
  CHECK(sp_validate_fuel(200) == SIG_VALID);
  CHECK(sp_validate_fuel(201) == SIG_OUT_OF_RANGE);
  CHECK(sp_validate_fuel(0xFF) == SIG_OUT_OF_RANGE);
  CHECK(sp_validate_temp(0) == SIG_VALID);
  CHECK(sp_validate_temp(190) == SIG_VALID);
  CHECK(sp_validate_temp(191) == SIG_OUT_OF_RANGE);
  CHECK(sp_validate_temp(0xFF) == SIG_OUT_OF_RANGE);
}

/* ICM-F-05: raw -> physical. */
static void test_ICM_F_05_scaling(void)
{
  CHECK_NEAR(sp_scale_speed(12000), 120.0, 1e-4);
  CHECK_NEAR(sp_scale_speed(25000), 250.0, 1e-4);
  CHECK_NEAR(sp_scale_rpm(32000), 8000.0, 1e-4);
  CHECK_NEAR(sp_scale_rpm(3), 0.75, 1e-6);
  CHECK_NEAR(sp_scale_fuel(131), 65.5, 1e-6);
  CHECK_NEAR(sp_scale_temp(0), -40.0, 1e-6);
  CHECK_NEAR(sp_scale_temp(190), 150.0, 1e-6);
}

/* ICM-S-01: timeout is strictly more than 3 periods, and survives the
 * 32-bit tick counter wrapping around. */
static void test_ICM_S_01_timeout_boundary_and_wrap(void)
{
  CHECK(ICM_TIMEOUT_M1_TICKS == 150u);
  CHECK(ICM_TIMEOUT_M2_TICKS == 300u);
  CHECK(ICM_TIMEOUT_M4_TICKS == 30u);
  CHECK(!sp_timed_out(1150, 1000, 150));
  CHECK(sp_timed_out(1151, 1000, 150));
  CHECK(!sp_timed_out(100, 0xFFFFFFF0u, 150)); /* 116 ticks across the wrap */
  CHECK(sp_timed_out(200, 0xFFFFFFF0u, 150));  /* 216 ticks across the wrap */
}

/* ICM-S-01: timeout outranks range; never-received is its own state. */
static void test_ICM_S_01_status_combination(void)
{
  CHECK(sp_signal_status(false, 10, 0, 200, SIG_VALID) == SIG_NEVER_RECEIVED);
  CHECK(sp_signal_status(false, 201, 0, 200, SIG_VALID) == SIG_TIMEOUT);
  CHECK(sp_signal_status(true, 50, 40, 30, SIG_OUT_OF_RANGE) == SIG_OUT_OF_RANGE);
  CHECK(sp_signal_status(true, 80, 40, 30, SIG_OUT_OF_RANGE) == SIG_TIMEOUT);
  CHECK(sp_signal_status(true, 50, 40, 30, SIG_VALID) == SIG_VALID);
}

/* ICM-F-06: the low-pass filter primes on the first sample, converges to a
 * step input with the expected time constant, and restarts after reset. */
static void test_ICM_F_06_lowpass_filter(void)
{
  sp_lpf_t f;
  sp_lpf_reset(&f);
  float a = sp_lpf_alpha(0.02f, 0.2f);
  CHECK_NEAR(sp_lpf_step(&f, 50.0f, a), 50.0, 1e-6); /* primed, no ramp */

  /* step 50 -> 100: after one time constant (10 steps of 20 ms for tau =
   * 0.2 s) the output covers about 63 % of the step. */
  float y = 0.0f;
  for (int i = 0; i < 10; i++) {
    y = sp_lpf_step(&f, 100.0f, a);
  }
  CHECK(y > 79.0f && y < 83.0f); /* 50 + 50 * (1 - (1-a)^10) = 80.7 */
  for (int i = 0; i < 200; i++) {
    y = sp_lpf_step(&f, 100.0f, a);
  }
  CHECK_NEAR(y, 100.0, 0.01);

  sp_lpf_reset(&f);
  CHECK_NEAR(sp_lpf_step(&f, 7.0f, a), 7.0, 1e-6);
}

/* ICM-F-08: hysteresis for low fuel (on <= 10 %, off > 12 %) and overheat
 * (on >= 115 degC, off < 110 degC). */
static void test_ICM_F_08_warning_hysteresis(void)
{
  bool lf = false;
  lf = sp_hyst_low(lf, 10.5f, ICM_LOW_FUEL_ON_PCT, ICM_LOW_FUEL_OFF_PCT);
  CHECK(!lf);
  lf = sp_hyst_low(lf, 10.0f, ICM_LOW_FUEL_ON_PCT, ICM_LOW_FUEL_OFF_PCT);
  CHECK(lf);
  lf = sp_hyst_low(lf, 11.9f, ICM_LOW_FUEL_ON_PCT, ICM_LOW_FUEL_OFF_PCT);
  CHECK(lf); /* inside the band: stays on, no flicker */
  lf = sp_hyst_low(lf, 12.1f, ICM_LOW_FUEL_ON_PCT, ICM_LOW_FUEL_OFF_PCT);
  CHECK(!lf);

  bool oh = false;
  oh = sp_hyst_high(oh, 114.9f, ICM_OVERHEAT_ON_C, ICM_OVERHEAT_OFF_C);
  CHECK(!oh);
  oh = sp_hyst_high(oh, 115.0f, ICM_OVERHEAT_ON_C, ICM_OVERHEAT_OFF_C);
  CHECK(oh);
  oh = sp_hyst_high(oh, 110.0f, ICM_OVERHEAT_ON_C, ICM_OVERHEAT_OFF_C);
  CHECK(oh);
  oh = sp_hyst_high(oh, 109.9f, ICM_OVERHEAT_ON_C, ICM_OVERHEAT_OFF_C);
  CHECK(!oh);
}

int main(void)
{
  RUN(test_ICM_S_02_speed_range_boundaries);
  RUN(test_ICM_S_02_rpm_range_boundaries);
  RUN(test_ICM_S_02_fuel_and_temp_boundaries);
  RUN(test_ICM_F_05_scaling);
  RUN(test_ICM_S_01_timeout_boundary_and_wrap);
  RUN(test_ICM_S_01_status_combination);
  RUN(test_ICM_F_06_lowpass_filter);
  RUN(test_ICM_F_08_warning_hysteresis);
  return CHECK_REPORT();
}
