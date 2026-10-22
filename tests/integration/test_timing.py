"""Timing and deadline tests, nominal traffic (30 s drive cycle).

Osvaldo Janeri Filho <janeri@gmail.com>

What is asserted strictly and what is asserted with tolerance follows the
two-clock split in tools/analyze_timing.py:

* In ticks, the schedule is the OS's business and must be exact: every
  release of a 20 ms task is exactly 2 ticks after the previous one, every
  job starts in the tick it was released in, no job is ever lost.
* In wall-clock time the host gets a say (ViPER's usleep-based tick drifts,
  and without SCHED_FIFO a loaded Linux box delays the whole process). The
  bounds there are the deadlines themselves, plus a +-5 % band on the mean
  period that is wide enough for the drift we measured (+0.5 % under
  SCHED_FIFO, +2.4 % without) and narrow enough to catch a wrong alarm
  cycle, which would be off by 50 % or more.
"""
import unittest

import icm_runner

PERIODIC = {
    "Task_Acquisition": 10.0,
    "Task_Processing": 20.0,
    "Task_Display": 50.0,
    "Task_Diagnostics": 100.0,
    "Task_Logging": 1000.0,
}


class NominalTiming(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.res = icm_runner.cached("nominal", "nominal")
        cls.t = cls.res.timing["tasks"]

    def test_ICM_T_01_to_05_release_grid_is_exact_in_ticks(self):
        """ICM-T-01 ICM-T-02 ICM-T-03 ICM-T-04 ICM-T-05: each periodic task is
        released by its alarm exactly every T/10 ms ticks, with no gap."""
        for task, period in PERIODIC.items():
            with self.subTest(task=task):
                s = self.t[task]
                self.assertTrue(s["release_gaps_exact"],
                                f"{task} release gaps {s['release_gap_counts']}")
                # 31 s run (3100 ticks), first release 10 ticks in
                expected = int((31_000 - 100) / period)
                self.assertGreaterEqual(s["jobs"], expected - 2)
                self.assertLessEqual(s["jobs"], expected + 1)

    def test_ICM_T_07_every_job_starts_in_its_release_tick(self):
        """ICM-T-07: under fixed-priority preemptive scheduling with U ~ 2 %,
        no job may still be waiting when the next tick arrives."""
        for task in PERIODIC:
            with self.subTest(task=task):
                self.assertEqual(self.t[task]["start_in_release_tick_pct"], 100.0)

    def test_ICM_T_01_to_05_wall_clock_period(self):
        """ICM-T-01 ICM-T-02 ICM-T-03 ICM-T-04 ICM-T-05: the mean period
        measured with CLOCK_MONOTONIC is within 5 % of nominal."""
        for task, period in PERIODIC.items():
            with self.subTest(task=task):
                mean = self.t[task]["period_wall_mean_ms"]
                self.assertAlmostEqual(mean, period, delta=0.05 * period)

    def test_ICM_T_07_no_deadline_miss(self):
        """ICM-T-07: response time (release -> end) <= period for every job
        of every task, measured, and no E_OS_LIMIT from any alarm."""
        for task, s in self.t.items():
            with self.subTest(task=task):
                self.assertEqual(s["misses"], 0)
                self.assertLess(s["resp_max_us"], s["deadline_ms"] * 1000.0)
                self.assertEqual(self.res.summary[task]["overruns"], 0)
                self.assertEqual(self.res.summary[task]["misses"], 0)

    def test_ICM_T_06_can_frame_to_data_within_10ms(self):
        """ICM-T-06: every frame is decoded into ICM_Data within 10 ms of the
        controller taking it off the bus."""
        s = self.t["Task_CAN_RX"]
        self.assertGreater(s["jobs"], 0)
        self.assertLess(s["resp_max_us"], 10_000.0)

    def test_ICM_T_06_no_frame_lost_in_nominal_traffic(self):
        """ICM-T-06: every frame the simulator sent was accepted (rx counters
        in the last log line == frames sent before it)."""
        last = self.res.logs[-1]
        rx = [int(x) for x in last["rx"].split("/")]
        for i, msg in enumerate(["M1", "M2", "M3", "M4"]):
            with self.subTest(msg=msg):
                sent = [e for e in self.res.sim("tx", msg=msg)
                        if e["mono_ns"] < last["mono"]]
                # a frame can be in flight at the instant the line is printed
                self.assertIn(rx[i], (len(sent), len(sent) - 1))
        self.assertEqual(last["crc"], "0")
        self.assertEqual(last["ovr"], "0")

    def test_ICM_T_07_schedulable_by_analysis(self):
        """ICM-T-07: with the measured worst-case execution times, utilisation
        is under the Liu & Layland bound and response-time analysis (with the
        ceiling-protocol blocking term) gives R <= D for every task."""
        sc = self.res.timing["sched"]
        self.assertLess(sc["U"], sc["rm_bound"])
        for task, r in sc["rta"].items():
            with self.subTest(task=task):
                self.assertTrue(r["ok"], r)

    def test_ICM_D_03_no_os_api_error(self):
        """ICM-D-03: with STATUS = EXTENDED every misuse of an OS service
        (wrong resource, release without get, ...) reaches ErrorHook. None
        may happen."""
        self.assertEqual(self.res.summary_value("os_api_errors"), 0)

    def test_ICM_D_04_no_torn_snapshot(self):
        """ICM-D-04: every copy of ICM_Data taken in the run was consistent."""
        self.assertEqual(self.res.summary_value("torn"), 0)
        self.assertGreater(self.res.summary_value("snapshots"), 900)


if __name__ == "__main__":
    unittest.main()
