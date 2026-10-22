"""Overload: one task overruns its period; detection and isolation.

Osvaldo Janeri Filho <janeri@gmail.com>

The overload build (tools/build.sh overload) makes Task_Diagnostics busy-wait
150 ms in every job between t = 5 s and t = 10 s. Its period is 100 ms, so
each next activation finds the previous job still running:

* ACTIVATION = 1, so the alarm's ActivateTask fails with E_OS_LIMIT and
  ErrorHook records an overrun for Task_Diagnostics (detection);
* Diagnostics confirms DTC 0x31 TASK_DEADLINE_OVERRUN (reporting);
* every task with a higher priority keeps meeting its deadlines, because
  fixed-priority preemption does not let a lower-priority task take their
  processor time (isolation). Only Task_Logging, below Diagnostics, could be
  delayed; at 75 % load it still has room.
"""
import unittest

import icm_runner

RUN_S = 18.0


class Overload(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.res = icm_runner.cached("overload", "nominal", seconds=RUN_S)

    def test_ICM_T_08_overrun_detected_by_errorhook(self):
        """ICM-T-08: E_OS_LIMIT on the activation of Task_Diagnostics is
        caught by ErrorHook, and the measured response time exceeds the
        100 ms deadline."""
        self.assertGreater(self.res.summary["Task_Diagnostics"]["overruns"], 10)
        self.assertGreater(self.res.summary["Task_Diagnostics"]["misses"], 0)

    def test_ICM_T_08_overrun_reported_as_dtc_and_healed(self):
        """ICM-T-08 ICM-S-04: DTC 0x31 is confirmed during the overload and
        heals 5 s after it ends."""
        conf = self.res.dtc_events("TASK_DEADLINE_OVERRUN", "CONFIRMED")
        healed = self.res.dtc_events("TASK_DEADLINE_OVERRUN", "HEALED")
        self.assertTrue(conf)
        self.assertGreater(conf[0]["t"], 5.0)
        self.assertLess(conf[0]["t"], 5.6)
        self.assertTrue(healed)
        self.assertGreater(healed[0]["t"], 14.5)
        frames = "\n".join(f for t, f in self.res.frames if t and 6 < t < 10)
        self.assertIn("TIMING FAULT: TASK DEADLINE OVERRUN", frames)

    def test_ICM_T_09_higher_priority_tasks_unaffected(self):
        """ICM-T-09: CAN_RX, Acquisition, Processing and Display miss nothing
        while Diagnostics is overloaded."""
        for task in ("Task_CAN_RX", "Task_Acquisition", "Task_Processing", "Task_Display"):
            with self.subTest(task=task):
                self.assertEqual(self.res.timing["tasks"][task]["misses"], 0)
                self.assertEqual(self.res.summary[task]["overruns"], 0)
                self.assertEqual(self.res.timing["tasks"][task]["start_in_release_tick_pct"]
                                 if task != "Task_CAN_RX" else 100.0, 100.0)


if __name__ == "__main__":
    unittest.main()
