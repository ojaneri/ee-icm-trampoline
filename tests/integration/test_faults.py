"""Fault handling: CAN timeouts, out-of-range signals, corrupted frames.

Osvaldo Janeri Filho <janeri@gmail.com>

One 32 s run of the "faults" scenario (sim/can_sim.py):

     5.0 -  10.0 s  M2 (vehicle speed) not sent            -> DTC 0x02
    11.0 -  14.0 s  M1 says 9000 rpm                        -> DTC 0x12
    15.0 -  17.0 s  M2 says 300 km/h                        -> DTC 0x11
    18.0 -  19.5 s  M3 sent with a bad CRC                  -> DTC 0x21
    20.0 -  20.8 s  M4 alive counter frozen (stale sender)  -> 0x21, 0x04
    22.5 -  24.5 s  M4 (body) not sent                      -> DTC 0x04
    26.0 s          200 M1 frames back to back              -> DTC 0x22

Latencies are measured end to end in one clock: the simulator stamps every
frame it sends with CLOCK_MONOTONIC and the cluster stamps every DTC change
with the same clock, so "last M2 frame on the bus -> DTC confirmed" is a
plain subtraction.
"""
import unittest

import icm_runner

TICK_S = 0.010


class Faults(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.res = icm_runner.cached("nominal", "faults")
        cls.start = cls.res.sim("start")[0]["mono_ns"]

    # -- helpers ---------------------------------------------------------
    def sim_t(self, mono):
        return (mono - self.start) / 1e9

    def last_tx_before(self, msg, t):
        tx = [e for e in self.res.sim("tx", msg=msg) if e["t"] < t]
        return tx[-1]

    def first_tx_from(self, msg, t):
        return next(e for e in self.res.sim("tx", msg=msg) if e["t"] >= t)

    def first_dtc_after(self, name, state, mono):
        ev = [d for d in self.res.dtc_events(name, state) if d["mono"] > mono]
        self.assertTrue(ev, f"{name} never {state} after t={self.sim_t(mono):.2f}")
        return ev[0]

    def frames_while_faulty(self, name, recovery_tx):
        """Dashboard frames from the DTC confirmation to the first good frame
        after the fault. Not up to the DTC healing: the gauge must come back
        as soon as the signal does, while the DTC rightly stays stored for
        its 1 s healing time (that is fault memory, not signal state)."""
        conf = self.res.dtc_events(name, "CONFIRMED")[0]
        t_end = self.res.uptime_of(recovery_tx["mono_ns"])
        frames = self.res.frames_between(conf["t"] + 0.05, t_end)
        self.assertTrue(frames, f"no dashboard frame while {name} was active")
        return frames

    # -- M2 timeout --------------------------------------------------------
    def test_ICM_S_01_m2_timeout_detected_after_3_periods(self):
        """ICM-S-01 ICM-S-04: losing M2 (1000 ms) is confirmed 3 s after its
        last frame (timeout = 3 periods), not earlier, and within 3.3 s
        (one tick + one diagnostics cycle + host tick drift)."""
        last = self.last_tx_before("M2", 5.0)
        dtc = self.first_dtc_after("M2_VEHICLE_SPEED_TIMEOUT", "CONFIRMED", last["mono_ns"])
        latency = (dtc["mono"] - last["mono_ns"]) / 1e9
        self.assertGreaterEqual(latency, 3.0)
        self.assertLessEqual(latency, 3.3)

    def test_ICM_S_04_m2_timeout_heals_after_recovery(self):
        """ICM-S-04: once M2 is back, the DTC heals after 10 good cycles
        (1 s), within 1.3 s of the first frame."""
        back = self.first_tx_from("M2", 10.0)
        dtc = self.first_dtc_after("M2_VEHICLE_SPEED_TIMEOUT", "HEALED", back["mono_ns"])
        self.assertLessEqual((dtc["mono"] - back["mono_ns"]) / 1e9, 1.3)

    def test_ICM_S_05_m2_timeout_shows_dashes_and_message(self):
        """ICM-S-05: while the speed is unknown the speedometer shows ---,
        never a number, and the driver gets a message."""
        for f in self.frames_while_faulty("M2_VEHICLE_SPEED_TIMEOUT",
                                          self.first_tx_from("M2", 10.0)):
            self.assertIn("--- km/h", f)
            self.assertIn("COMM LOST: VEHICLE SPEED (M2)", f)
            self.assertIn("[FAULT]", f)

    # -- out of range ----------------------------------------------------------
    def test_ICM_S_02_rpm_out_of_range_detected(self):
        """ICM-S-02 ICM-S-04: 9000 rpm (above 8000) is rejected, confirmed
        within 3 diagnostics cycles of the first bad frame (<= 0.5 s)."""
        bad = self.first_tx_from("M1", 11.0)
        dtc = self.first_dtc_after("RPM_OUT_OF_RANGE", "CONFIRMED", bad["mono_ns"])
        self.assertLessEqual((dtc["mono"] - bad["mono_ns"]) / 1e9, 0.5)
        healed = self.first_dtc_after("RPM_OUT_OF_RANGE", "HEALED", dtc["mono"])
        self.assertGreater(self.sim_t(healed["mono"]), 14.0)

    def test_ICM_S_05_rpm_out_of_range_never_displayed(self):
        """ICM-S-05: the 9000 rpm value never reaches the tachometer."""
        for f in self.frames_while_faulty("RPM_OUT_OF_RANGE",
                                          self.first_tx_from("M1", 14.0)):
            self.assertIn("--- rpm", f)
            self.assertNotIn("9000 rpm", f)
        for l in self.res.logs:
            self.assertLessEqual(float(l["rpm"].split("/")[0]), 8000.0)

    def test_ICM_S_02_speed_out_of_range_detected(self):
        """ICM-S-02 ICM-S-04: 300 km/h (above 250) is rejected and confirmed
        within 0.5 s of the first bad frame."""
        bad = self.first_tx_from("M2", 15.0)
        dtc = self.first_dtc_after("SPEED_OUT_OF_RANGE", "CONFIRMED", bad["mono_ns"])
        self.assertLessEqual((dtc["mono"] - bad["mono_ns"]) / 1e9, 0.5)
        for l in self.res.logs:
            self.assertLessEqual(float(l["speed"].split("/")[0]), 250.0)

    # -- end-to-end protection ----------------------------------------------------
    def test_ICM_S_03_crc_errors_counted_and_reported(self):
        """ICM-S-03: every M3 frame sent with a bad CRC is counted and
        discarded, CAN_E2E_ERROR is confirmed, and 1.5 s of corruption on a
        1 s message does not escalate to a timeout (3 s)."""
        corrupt = [e for e in self.res.sim("tx", msg="M3") if e["corrupt"]]
        self.assertGreaterEqual(len(corrupt), 1)
        self.assertEqual(int(self.res.logs[-1]["crc"]), len(corrupt))
        self.first_dtc_after("CAN_E2E_ERROR", "CONFIRMED", corrupt[0]["mono_ns"])
        self.assertEqual(self.res.dtc_events("M3_ENGINE_STATUS_TIMEOUT"), [])

    def test_ICM_S_03_frozen_alive_counter_rejected(self):
        """ICM-S-03 ICM-S-01: frames that repeat the alive counter are stale
        and discarded; with M4 (100 ms) stale for 1 s, body status goes to
        TIMEOUT even though frames kept arriving."""
        frozen = [e for e in self.res.sim("tx", msg="M4") if e["frozen"]]
        self.assertGreater(len(frozen), 5)
        self.assertEqual(int(self.res.logs[-1]["cnt"]), len(frozen))
        dtc = self.first_dtc_after("M4_BODY_STATUS_TIMEOUT", "CONFIRMED", frozen[0]["mono_ns"])
        self.assertLess(self.sim_t(dtc["mono"]), 20.8)

    # -- M4 timeout ----------------------------------------------------------
    def test_ICM_S_01_m4_timeout_detected(self):
        """ICM-S-01: losing M4 (100 ms) is confirmed 0.3..0.55 s after its
        last frame."""
        last = self.last_tx_before("M4", 22.5)
        dtc = self.first_dtc_after("M4_BODY_STATUS_TIMEOUT", "CONFIRMED", last["mono_ns"])
        latency = (dtc["mono"] - last["mono_ns"]) / 1e9
        self.assertGreaterEqual(latency, 0.3)
        self.assertLessEqual(latency, 0.55)

    def test_ICM_S_05_body_loss_turns_indicators_off(self):
        """ICM-S-05: the drive cycle has the right indicator on from 22 to 25 s,
        but with M4 lost the cluster must not show it (it does not know)."""
        last = self.last_tx_before("M4", 22.5)
        dtc = self.first_dtc_after("M4_BODY_STATUS_TIMEOUT", "CONFIRMED", last["mono_ns"])
        frames = self.res.frames_between(dtc["t"] + 0.05, dtc["t"] + 1.5)
        self.assertTrue(frames)
        for f in frames:
            self.assertNotIn("RIGHT >>>", f)
            self.assertIn("COMM LOST: BODY STATUS (M4)", f)

    # -- overrun ---------------------------------------------------------------
    def test_ICM_S_06_burst_absorbed_without_loss(self):
        """ICM-S-06 ICM-T-06: 200 M1 frames sent as fast as the sender can go
        are all received: the ISR drains the 8-deep controller FIFO faster
        than any sender can fill it. (Loss needs a blocked ISR; that case is
        test_overrun.py.)"""
        self.assertEqual(self.res.sim("burst")[0]["frames"], 200)
        last = self.res.logs[-1]
        sent_m1 = [e for e in self.res.sim("tx", msg="M1") if e["mono_ns"] < last["mono"]]
        rx_m1 = int(last["rx"].split("/")[0])
        self.assertEqual(rx_m1, len(sent_m1) + 200)
        self.assertEqual(int(last["ovr"]), 0)

    # -- timing under faults ---------------------------------------------------
    def test_ICM_T_07_deadlines_hold_during_faults(self):
        """ICM-T-07: fault handling costs no deadline: zero misses and zero
        overruns over the whole faulty run, burst included."""
        for task, s in self.res.timing["tasks"].items():
            with self.subTest(task=task):
                self.assertEqual(s["misses"], 0)
                self.assertEqual(self.res.summary[task]["overruns"], 0)
        self.assertEqual(self.res.summary_value("os_api_errors"), 0)
        self.assertEqual(self.res.summary_value("torn"), 0)


if __name__ == "__main__":
    unittest.main()
