"""Receive overrun: frames lost while the CAN interrupt cannot be serviced.

Osvaldo Janeri Filho <janeri@gmail.com>

The "faults" scenario shows that a flood of frames alone does not lose data:
ISR_CAN_RX drains the 8-deep controller FIFO in tens of microseconds. What
does lose frames on a real ECU is an interrupt-locked section that lasts too
long. The irq_lock build has exactly one: at t = 3 s, Task_Diagnostics calls
SuspendAllInterrupts() and spins for 200 ms. The "babble" scenario has the
engine ECU send M1 every millisecond from 2 s to 6 s, so about 200 frames
arrive while the interrupt is locked, and only 8 fit in the FIFO.

What must hold:
* nothing is lost silently: every frame sent is either received or counted
  as an overrun;
* the loss is reported (DTC 0x22);
* the 200 ms of late frames are caught by the deadline monitor too (the
  Task_CAN_RX deadline is 10 ms), which raises DTC 0x31.

Side effect worth knowing, asserted here so it stays documented: with all
interrupts locked the OS tick is blocked as well. POSIX signals do not queue,
so 20 timer ticks collapse into one; on a microcontroller the timer's pending
flag is a single bit and behaves the same. The 10 ms task sees a ~200 ms gap.
"""
import unittest

import icm_runner


class Overrun(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.res = icm_runner.cached("irq_lock", "babble")
        cls.last = cls.res.logs[-1]

    def test_ICM_S_06_every_frame_received_or_counted(self):
        """ICM-S-06: frames sent (all messages: whatever arrives during the
        lock is lost, not only M1) == frames received + frames counted as
        overrun, exactly. Also layer by layer: what the controller accepted
        is what the ISR drained is what the task popped."""
        sent = self.res.sim("tx")
        rx = sum(int(x) for x in self.last["rx"].split("/"))
        ovr = int(self.last["ovr"])
        self.assertGreater(ovr, 100)
        self.assertEqual(len(sent), rx + ovr)
        pushed = self.res.summary_value("can_pushed")
        self.assertEqual(pushed + self.res.summary_value("can_hw_dropped"), len(sent))
        self.assertEqual(self.res.summary_value("can_drained"), pushed)
        self.assertEqual(self.res.summary_value("can_popped"), pushed)

    def test_ICM_S_06_overrun_reported(self):
        """ICM-S-06: DTC 0x22 CAN_RX_OVERRUN is confirmed.

        Not asserted: an alive-counter jump. ~190 lost frames is a multiple
        of 16 plus a little, and a 4-bit counter cannot tell 192 lost frames
        from none (192 mod 16 = 0). That is a property of every short E2E
        counter, and the reason the controller's overrun flag exists."""
        self.assertTrue(self.res.dtc_events("CAN_RX_OVERRUN", "CONFIRMED"))

    def test_ICM_T_08_late_frames_caught_by_deadline_monitor(self):
        """ICM-T-08: frames that waited out the 200 ms lock exceed the 10 ms
        Task_CAN_RX deadline; the monitor counts them and DTC 0x31 is
        confirmed."""
        self.assertGreater(self.res.summary["Task_CAN_RX"]["misses"], 0)
        self.assertTrue(self.res.dtc_events("TASK_DEADLINE_OVERRUN", "CONFIRMED"))

    def test_ICM_T_01_lost_ticks_during_interrupt_lock(self):
        """ICM-T-01 (documented limitation): during the lock no tick is
        counted, so the 10 ms task's wall-clock period shows one ~200 ms
        gap, while its release grid in ticks is still exact."""
        acq = self.res.timing["tasks"]["Task_Acquisition"]
        self.assertGreater(acq["period_wall_max_ms"], 150.0)
        self.assertTrue(acq["release_gaps_exact"])


if __name__ == "__main__":
    unittest.main()
