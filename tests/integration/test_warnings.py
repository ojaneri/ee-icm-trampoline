"""Driver warnings: low fuel, engine overheat, redline.

Osvaldo Janeri Filho <janeri@gmail.com>

"warnings" scenario (15 s): fuel at 8 % throughout, coolant ramps from 90 to
125 degC between 3 s and 8 s, holds until 10 s, then drops to 100 degC;
engine at 7000 rpm between 5 s and 9 s. These are values in range: the
cluster must warn, not fault.
"""
import unittest

import icm_runner


class Warnings(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.res = icm_runner.cached("nominal", "warnings")
        start = cls.res.sim("start")[0]["mono_ns"]
        cls.logs = [dict(l, sim_t=(l["mono"] - start) / 1e9) for l in cls.res.logs]

    def test_ICM_F_08_low_fuel_lamp(self):
        """ICM-F-08: 8 % is below the 10 % threshold: the lamp is on from the
        first second (the filter primes on the first sample, it does not
        sweep up from zero), and the dashboard shows lamp and message."""
        for l in self.logs:
            if l["sim_t"] >= 1.0:
                self.assertEqual(l["low_fuel"], "1", l)
        frames = "\n".join(f for t, f in self.res.frames if t and t > 1.5)
        self.assertIn("[FUEL]", frames)
        self.assertIn("LOW FUEL", frames)

    def test_ICM_F_08_overheat_on_and_off_with_hysteresis(self):
        """ICM-F-08: the lamp comes on once the (filtered) coolant temperature
        reaches 115 degC and goes off once it is below 110 degC."""
        on = [l for l in self.logs if l["overheat"] == "1"]
        self.assertTrue(on, "overheat never signalled")
        for l in on:
            self.assertGreaterEqual(float(l["temp"].split("/")[0]), 110.0)
        self.assertGreater(on[0]["sim_t"], 6.0)
        self.assertEqual(self.logs[-1]["overheat"], "0")
        frames = "\n".join(f for _, f in self.res.frames)
        self.assertIn("[TEMP]", frames)
        self.assertIn("ENGINE OVERHEAT", frames)

    def test_ICM_F_08_redline(self):
        """ICM-F-08: 7000 rpm is above the 6500 rpm redline and is shown."""
        frames = self.res.frames_between(6.0, 9.0)
        self.assertTrue(any("RED" in f for f in frames))

    def test_ICM_S_04_warnings_are_not_faults(self):
        """ICM-S-04: in-range values never raise a DTC."""
        end = self.res.sim_end_mono()
        self.assertEqual([d for d in self.res.diag if d["mono"] < end], [])


if __name__ == "__main__":
    unittest.main()
