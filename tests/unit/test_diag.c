/*
 * ICM — unit tests for DTC debouncing (src/diag.c).
 * Osvaldo Janeri Filho <janeri@gmail.com>
 */
#include "check.h"
#include "diag.h"
#include "icm_config.h"

/* ICM-S-04: confirmed on the N-th consecutive failing cycle, not before. */
static void test_ICM_S_04_confirm_after_n_failures(void)
{
  dtc_state_t s;
  dtc_reset(&s);
  CHECK(dtc_update(&s, true, 3, 10) == DTC_NO_CHANGE);
  CHECK(dtc_update(&s, true, 3, 10) == DTC_NO_CHANGE);
  CHECK(!s.confirmed);
  CHECK(dtc_update(&s, true, 3, 10) == DTC_BECAME_CONFIRMED);
  CHECK(s.confirmed && s.occurrences == 1);
  CHECK(dtc_update(&s, true, 3, 10) == DTC_NO_CHANGE); /* reported once */
}

/* ICM-S-04: an intermittent fault that never lasts N cycles never confirms. */
static void test_ICM_S_04_intermittent_fault_does_not_confirm(void)
{
  dtc_state_t s;
  dtc_reset(&s);
  for (int i = 0; i < 100; i++) {
    CHECK(dtc_update(&s, (i % 3) != 2, 3, 10) == DTC_NO_CHANGE);
  }
  CHECK(!s.confirmed);
}

/* ICM-S-04: healed after M consecutive passing cycles; one bad cycle in the
 * middle restarts the count. */
static void test_ICM_S_04_heal_after_m_passes(void)
{
  dtc_state_t s;
  dtc_reset(&s);
  dtc_update(&s, true, 1, 10);
  CHECK(s.confirmed);
  for (int i = 0; i < 9; i++) {
    CHECK(dtc_update(&s, false, 1, 10) == DTC_NO_CHANGE);
  }
  CHECK(dtc_update(&s, true, 1, 10) == DTC_NO_CHANGE); /* still confirmed */
  for (int i = 0; i < 9; i++) {
    CHECK(dtc_update(&s, false, 1, 10) == DTC_NO_CHANGE);
  }
  CHECK(dtc_update(&s, false, 1, 10) == DTC_BECAME_HEALED);
  CHECK(!s.confirmed);
  dtc_update(&s, true, 1, 10);
  CHECK(s.occurrences == 2);
}

/* ICM-S-04 ICM-T-08: the table. Codes are unique; timeouts and task overrun
 * confirm on the first cycle; everything else uses the default debounce. */
static void test_ICM_S_04_dtc_table(void)
{
  for (int i = 0; i < DTC_COUNT; i++) {
    for (int j = i + 1; j < DTC_COUNT; j++) {
      CHECK(dtc_code((dtc_id_t)i) != dtc_code((dtc_id_t)j));
    }
  }
  CHECK(dtc_confirm_cycles(DTC_M2_TIMEOUT) == 1);
  CHECK(dtc_confirm_cycles(DTC_TASK_OVERRUN) == 1);
  CHECK(dtc_heal_cycles(DTC_TASK_OVERRUN) == 50);
  CHECK(dtc_confirm_cycles(DTC_RPM_RANGE) == ICM_DTC_CONFIRM_CYCLES);
  CHECK(dtc_code(DTC_TASK_OVERRUN) == 0x31);
}

int main(void)
{
  RUN(test_ICM_S_04_confirm_after_n_failures);
  RUN(test_ICM_S_04_intermittent_fault_does_not_confirm);
  RUN(test_ICM_S_04_heal_after_m_passes);
  RUN(test_ICM_S_04_dtc_table);
  return CHECK_REPORT();
}
