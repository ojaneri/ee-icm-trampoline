"""Functional tests on nominal traffic: does the cluster show the car?

Osvaldo Janeri Filho <janeri@gmail.com>

Same 30 s run as test_timing.py (shared through icm_runner.cached). The
simulator's drive cycle is known, so at each instant we know what the gauges
should say, up to the filter lag.
"""
import unittest

import icm_runner


class NominalSignals(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.res = icm_runner.cached("nominal", "nominal")
        start = cls.res.sim("start")[0]["mono_ns"]
        # LOG lines with the simulator's own time axis attached
        cls.logs = [dict(l, sim_t=(l["mono"] - start) / 1e9) for l in cls.res.logs]

    def log_at(self, t_from, t_to):
        rows = [l for l in self.logs if t_from <= l["sim_t"] < t_to]
        self.assertTrue(rows, f"no LOG line between {t_from} and {t_to} s")
        return rows

    def test_ICM_F_02_ICM_F_05_cruise_speed(self):
        """ICM-F-02 ICM-F-05: at 120 km/h cruise (drive cycle 14..20 s) the
        speedometer reads 120.0 +- 0.5 km/h and the signal is VALID."""
        for l in self.log_at(15.0, 20.0):
            speed, status = l["speed"].split("/")
            self.assertEqual(status, "VALID")
            self.assertAlmostEqual(float(speed), 120.0, delta=0.5)

    def test_ICM_F_01_ICM_F_05_cruise_rpm(self):
        """ICM-F-01 ICM-F-05: in top gear at 120 km/h the engine turns
        120 x 26 = 3120 rpm."""
        for l in self.log_at(15.0, 20.0):
            rpm, status = l["rpm"].split("/")
            self.assertEqual(status, "VALID")
            self.assertAlmostEqual(float(rpm), 3120.0, delta=10.0)

    def test_ICM_F_03_ICM_F_06_fuel_and_temperature(self):
        """ICM-F-03 ICM-F-06: fuel follows 65 - 0.05 t % (filtered, tau 5 s,
        so it lags a little), coolant warms up towards 90 degC."""
        for l in self.log_at(20.0, 30.0):
            fuel = float(l["fuel"].split("/")[0])
            expected = 65.0 - 0.05 * l["sim_t"]
            self.assertAlmostEqual(fuel, expected, delta=1.0)
        last_temp = float(self.log_at(28.0, 31.0)[-1]["temp"].split("/")[0])
        self.assertGreater(last_temp, 85.0)
        self.assertLess(last_temp, 91.0)

    def test_ICM_F_04_ICM_F_07_turn_indicators(self):
        """ICM-F-04 ICM-F-07: left indicator 3..6 s, right 22..25 s, hazard
        from 28 s, and the dashboard blinks them."""
        self.assertTrue(any(l["turn"] == "0x01" for l in self.log_at(3.5, 6.0)))
        self.assertTrue(any(l["turn"] == "0x02" for l in self.log_at(22.5, 25.0)))
        self.assertTrue(any(l["turn"] == "0x04" for l in self.log_at(28.5, 30.0)))
        frames = "\n".join(f for _, f in self.res.frames)
        self.assertIn("<<< LEFT", frames)
        self.assertIn("RIGHT >>>", frames)
        self.assertIn("HAZARD", frames)

    def test_ICM_F_07_dashboard_frames_are_rendered(self):
        """ICM-F-07: one frame per second reaches the log in plain mode, and
        it carries speed, rpm, fuel, temperature, lamps and messages."""
        self.assertGreaterEqual(len(self.res.frames), 29)
        _, frame = self.res.frames[-1]
        for text in ("SPEED", "RPM", "FUEL", "TEMP", "LAMPS", "MSG"):
            self.assertIn(text, frame)

    def test_ICM_S_04_no_dtc_in_nominal_traffic(self):
        """ICM-S-04: with every ECU healthy no DTC is ever confirmed, start-up
        included (ICM-S-01b first timeout)."""
        end = self.res.sim_end_mono()
        confirmed = [d for d in self.res.diag
                     if d["state"] == "CONFIRMED" and d["mono"] < end]
        self.assertEqual(confirmed, [])


if __name__ == "__main__":
    unittest.main()
