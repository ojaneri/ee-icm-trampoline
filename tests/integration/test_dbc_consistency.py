"""The signal dictionary exists three times; they must agree.

Osvaldo Janeri Filho <janeri@gmail.com>

1. dbc/icm.dbc            the dictionary in the industry's own format
2. src/icm_config.h        what the cluster is compiled with
3. sim/can_sim.py          what the simulated ECUs send

(The C decoder is tied to the simulator by the golden frames in
tests/unit/test_can_db.c.) This test needs no bus and no RTOS. The DBC part
uses cantools, installed in CI; without it that part is skipped, not passed.
"""
import re
import unittest
from pathlib import Path

import icm_runner  # noqa: F401  (puts sim/ on sys.path)
import can_sim

ROOT = Path(__file__).resolve().parents[2]
CONFIG = (ROOT / "src" / "icm_config.h").read_text()


def config_value(name):
    m = re.search(rf"#define\s+{name}\s+\(?(0x[0-9A-Fa-f]+|-?[0-9]+(?:\.[0-9]+)?)[uf]?\)?",
                  CONFIG)
    if not m:
        raise AssertionError(f"{name} not in icm_config.h")
    v = m.group(1)
    return float(int(v, 16)) if v.startswith("0x") else float(v)


class ConfigVsSimulator(unittest.TestCase):
    def test_ICM_F_01_to_04_ids_and_periods(self):
        """ICM-F-01 ICM-F-02 ICM-F-03 ICM-F-04: identifiers and periods."""
        for msg, name in (("M1", "M1_ENGINE_SPEED"), ("M2", "M2_VEHICLE_SPEED"),
                          ("M3", "M3_ENGINE_STATUS"), ("M4", "M4_BODY_STATUS")):
            with self.subTest(msg=msg):
                self.assertEqual(config_value(f"ICM_CAN_ID_{name}"), can_sim.IDS[msg])
                self.assertEqual(config_value(f"ICM_CAN_PERIOD_{msg}_MS"),
                                 can_sim.PERIOD_S[msg] * 1000)
        self.assertEqual(can_sim.PERIOD_S["M1"], 0.5)   # professor: 500 ms
        self.assertEqual(can_sim.PERIOD_S["M2"], 1.0)   # professor: 1000 ms

    def test_ICM_F_05_scaling_factors(self):
        """ICM-F-05: factor and offset of every scaled signal."""
        self.assertEqual(config_value("ICM_SPEED_FACTOR"), can_sim.SPEED_FACTOR)
        self.assertEqual(config_value("ICM_RPM_FACTOR"), can_sim.RPM_FACTOR)
        self.assertEqual(config_value("ICM_FUEL_FACTOR"), can_sim.FUEL_FACTOR)
        self.assertEqual(config_value("ICM_TEMP_OFFSET_C"), can_sim.TEMP_OFFSET)


class DbcVsSimulator(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            import cantools
        except ImportError:
            raise unittest.SkipTest("cantools not installed (pip install cantools)")
        cls.db = cantools.database.load_file(str(ROOT / "dbc" / "icm.dbc"))

    def test_ICM_F_01_to_04_dbc_ids_and_cycle_times(self):
        """ICM-F-01 ICM-F-02 ICM-F-03 ICM-F-04: DBC frame ids and
        GenMsgCycleTime match the simulator."""
        for msg, can_id in can_sim.IDS.items():
            with self.subTest(msg=msg):
                m = self.db.get_message_by_frame_id(can_id)
                self.assertEqual(m.length, 8)
                self.assertEqual(m.cycle_time, int(can_sim.PERIOD_S[msg] * 1000))

    def test_ICM_F_05_dbc_encoding_equals_simulator_bytes(self):
        """ICM-F-05: for a spread of physical values, encoding with the DBC
        gives byte for byte the frame the simulator puts on the bus."""
        samples = [
            {"speed_kmh": 0.0, "rpm": 800.0, "fuel_pct": 0.0, "coolant_c": -40.0,
             "turn_bits": 0, "warning_bits": 0},
            {"speed_kmh": 120.0, "rpm": 3120.0, "fuel_pct": 65.5, "coolant_c": 90.0,
             "turn_bits": 0x05, "warning_bits": 0x48},
            {"speed_kmh": 250.0, "rpm": 8000.0, "fuel_pct": 100.0, "coolant_c": 150.0,
             "turn_bits": 0x0F, "warning_bits": 0xFF},
        ]
        for phys in samples:
            raw = can_sim.raw_from_physical(phys)
            for msg, can_id in can_sim.IDS.items():
                with self.subTest(msg=msg, phys=phys):
                    sim_bytes = can_sim.pack(msg, raw, counter=7)
                    m = self.db.get_message_by_frame_id(can_id)
                    values = {
                        "M1": {"EngineSpeed": phys["rpm"]},
                        "M2": {"VehicleSpeed": phys["speed_kmh"]},
                        "M3": {"FuelLevel": phys["fuel_pct"],
                               "CoolantTemp": phys["coolant_c"]},
                        "M4": {name: (raw[field] >> bit) & 1 for name, field, bit in (
                            ("TurnLeft", "turn_bits", 0), ("TurnRight", "turn_bits", 1),
                            ("Hazard", "turn_bits", 2), ("HighBeam", "turn_bits", 3),
                            ("WarnOilPressure", "warning_bits", 0),
                            ("WarnBattery", "warning_bits", 1),
                            ("WarnBrake", "warning_bits", 2),
                            ("WarnCheckEngine", "warning_bits", 3),
                            ("WarnABS", "warning_bits", 4), ("WarnAirbag", "warning_bits", 5),
                            ("WarnSeatbelt", "warning_bits", 6),
                            ("WarnDoorOpen", "warning_bits", 7))},
                    }[msg]
                    values[f"{msg}_AliveCounter"] = 7
                    values[f"{msg}_CRC"] = sim_bytes[7]
                    self.assertEqual(m.encode(values), sim_bytes)


if __name__ == "__main__":
    unittest.main()
