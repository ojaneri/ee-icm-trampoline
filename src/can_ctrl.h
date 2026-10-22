/*
 * ICM — virtual CAN controller and its driver queue.
 * Osvaldo Janeri Filho <janeri@gmail.com>
 *
 * There is no CAN peripheral on a Linux process, so this module plays the
 * part of one. It has three layers, mirroring a real MCU:
 *
 *   bus        SocketCAN interface (vcan0 in simulation; a CANable 2.0 on
 *              can0 would work unchanged, it is the same socket API)
 *     |
 *   controller a POSIX thread reading the socket, with an acceptance filter
 *              for our four identifiers and an 8-deep receive FIFO. When the
 *              FIFO is full the frame is dropped and an overrun counted,
 *              which is what bxCAN or FlexCAN do. After each frame it raises
 *              the "interrupt line": kill(getpid(), irq_signal).
 *     |
 *   driver     ISR_CAN_RX (category 2) drains the controller FIFO into a
 *              32-deep software queue and sets EV_CAN_RX on Task_CAN_RX.
 *              Task_CAN_RX pops from that queue.
 *
 * The controller thread is not an OSEK object and never calls the OS. It
 * blocks every signal, so the interrupt always lands on the main thread where
 * Trampoline runs, and the Trampoline interrupt mask (sigprocmask) works as
 * it should. The controller FIFO is a single-producer / single-consumer ring
 * with C11 acquire/release atomics, which is the software equivalent of the
 * FIFO registers of a real controller.
 *
 * POSIX signals do not queue: three frames arriving while the interrupt is
 * masked raise one interrupt. The ISR therefore drains the FIFO until it is
 * empty rather than reading one frame per interrupt, which is also how a
 * level-triggered FIFO interrupt is handled on real hardware.
 */
#ifndef CAN_CTRL_H
#define CAN_CTRL_H

#include <stdbool.h>
#include <stdint.h>

#include "can_db.h"

typedef struct {
  icm_can_frame_t frame;
  uint64_t hw_rx_ns; /* when the controller took it off the bus   */
  uint64_t isr_ns;   /* when ISR_CAN_RX moved it to the queue     */
} can_ctrl_rx_t;

/* StartupHook. Returns 0 on success; on failure the cluster runs without a
 * bus and every message times out, which is the correct fail-safe picture. */
int can_ctrl_start(const char *ifname, int irq_signal);
void can_ctrl_stop(void);
bool can_ctrl_online(void);

/* ISR_CAN_RX: move everything from the controller FIFO to the driver queue.
 * Returns the number of frames moved. */
unsigned can_ctrl_isr_drain(void);

/* Task_CAN_RX: pop one frame from the driver queue. */
bool can_ctrl_pop(can_ctrl_rx_t *out);

/* Lifetime totals of each layer (ShutdownHook only). */
void can_ctrl_totals(uint32_t *pushed, uint32_t *hw_dropped, uint32_t *drained,
                     uint32_t *sw_dropped, uint32_t *popped);

/* Has either layer dropped a frame since the counters were last taken? */
bool can_ctrl_overrun_pending(void);

/* Overruns since the previous call (controller FIFO, driver queue). */
uint32_t can_ctrl_take_hw_overruns(void);
uint32_t can_ctrl_take_sw_overruns(void);

#endif /* CAN_CTRL_H */
