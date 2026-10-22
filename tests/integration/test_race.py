"""Mutual exclusion on ICM_Data: the race test and its negative control.

Osvaldo Janeri Filho <janeri@gmail.com>

A test that only ever passes proves little: it might pass because the code is
right, or because it is not looking. So the same test runs on two builds
(tools/build.sh):

* race_locked: Task_Display copies ICM_Data in two halves with a 12 ms busy
  wait in between, while holding ICM_Resource. 12 ms is longer than a tick,
  so a tick (and with it Task_Processing, priority 4, which writes the
  physical section) always arrives in the middle of the copy. Under the
  ceiling protocol Display runs at the ceiling while it holds the resource,
  Processing cannot start, and every copy is consistent.

* race_unlocked: the same window with the GetResource / ReleaseResource
  calls compiled out. Processing now preempts Display in the middle of the
  copy and rewrites the section; the copy has seq_begin from before and
  seq_end from after. Torn copies must appear here, or the check in
  icm_data_snapshot() would be blind and race_locked would prove nothing.

Same idea as the buggy build of Project #1: the suite has to go red on the
broken variant.
"""
import unittest

import icm_runner

RUN_S = 20.0


class RaceLocked(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.res = icm_runner.cached("race_locked", "nominal", seconds=RUN_S)

    def test_ICM_D_01_ICM_D_04_no_torn_copy_with_resource(self):
        """ICM-D-01 ICM-D-02 ICM-D-04: with ICM_Resource held, a reader that
        is slow enough to be interrupted mid-copy still never sees a torn
        ICM_Data."""
        self.assertEqual(self.res.summary_value("race_window_us"), 12000)
        self.assertGreater(self.res.summary_value("snapshots"), 500)
        self.assertEqual(self.res.summary_value("torn"), 0)

    def test_ICM_D_02_blocking_is_bounded_by_one_critical_section(self):
        """ICM-D-02: under the ceiling protocol a higher-priority task is
        blocked at most once, for at most one lower-priority critical section
        (12 ms here, inflated on purpose). Task_Acquisition (10 ms period)
        is delayed past its release tick, yet no activation is lost."""
        acq = self.res.timing["tasks"]["Task_Acquisition"]
        self.assertGreater(acq["start_delay_max_us"], 1000.0)  # it was blocked
        self.assertLess(acq["start_delay_max_us"], 12_000.0 + 3_000.0)
        self.assertEqual(self.res.summary["Task_Acquisition"]["overruns"], 0)
        self.assertEqual(self.res.summary_value("os_api_errors"), 0)

    def test_ICM_T_08_blocking_longer_than_a_deadline_is_caught(self):
        """ICM-T-08 ICM-D-02: the price of the ceiling. A 12 ms critical
        section blocks Task_CAN_RX (the ceiling is its priority) for up to
        12 ms, longer than its 10 ms deadline. Response-time analysis with
        B = 12 ms predicts the miss, and the deadline monitor sees it. Same
        theory, same measurement: this is why critical sections in the
        product are one memcpy long."""
        rta = self.res.timing["sched"]["rta"]["Task_CAN_RX"]
        self.assertGreater(rta["B_ms"], 10.0)
        self.assertFalse(rta["ok"])
        self.assertGreater(self.res.summary["Task_CAN_RX"]["misses"], 0)
        self.assertTrue(self.res.dtc_events("TASK_DEADLINE_OVERRUN", "CONFIRMED"))


class RaceUnlockedNegativeControl(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.res = icm_runner.cached("race_unlocked", "nominal", seconds=RUN_S)

    def test_ICM_D_04_torn_copies_without_resource(self):
        """ICM-D-04 (negative control): without the resource the very same
        reader gets torn copies, so the consistency check does see races."""
        self.assertEqual(self.res.summary_value("race_window_us"), 12000)
        self.assertGreater(self.res.summary_value("torn"), 0)


if __name__ == "__main__":
    unittest.main()
