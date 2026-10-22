/*
 * ICM — virtual CAN controller implementation (SocketCAN backend).
 * Osvaldo Janeri Filho <janeri@gmail.com>
 */
#define _GNU_SOURCE
#include "can_ctrl.h"

#include <errno.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "icm_config.h"
#include "tpl_os.h"

#define HW_MASK (ICM_CAN_HW_FIFO_DEPTH - 1u)
#define SW_MASK (ICM_CAN_SW_QUEUE_DEPTH - 1u)

_Static_assert((ICM_CAN_HW_FIFO_DEPTH & HW_MASK) == 0, "HW FIFO depth must be a power of two");
_Static_assert((ICM_CAN_SW_QUEUE_DEPTH & SW_MASK) == 0, "SW queue depth must be a power of two");

/* ---- controller FIFO: thread produces, ISR consumes ------------------- */
static can_ctrl_rx_t hw_fifo[ICM_CAN_HW_FIFO_DEPTH];
static _Atomic uint32_t hw_head; /* written by the controller thread only */
static _Atomic uint32_t hw_tail; /* written by ISR_CAN_RX only            */
static _Atomic uint32_t hw_overruns;

/* ---- driver queue: ISR produces, Task_CAN_RX consumes ------------------
 * Plain variables. The ISR cannot be interrupted by the task, and the task
 * reads them with OS interrupts suspended, so on one core there is no
 * interleaving to protect against. */
static can_ctrl_rx_t sw_queue[ICM_CAN_SW_QUEUE_DEPTH];
static uint32_t sw_head;
static uint32_t sw_tail;
static uint32_t sw_overruns;

/* Lifetime totals per layer, for the accounting line in ShutdownHook:
 * pushed = drained + hw dropped, drained = popped + sw dropped. */
static _Atomic uint32_t total_pushed;
static _Atomic uint32_t total_hw_dropped;
static uint32_t total_drained;
static uint32_t total_sw_dropped;
static uint32_t total_popped;

static int can_sock = -1;
static int irq_sig;
static pthread_t ctrl_thread;
static _Atomic bool stop_requested;
static bool online;

static uint64_t now_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void *controller_main(void *arg)
{
  (void)arg;

  /* Never take an interrupt on this thread: it is the peripheral, not the
   * CPU. Without this the kernel may deliver SIGUSR2 (the OS tick) or our
   * own IRQ signal here, and Trampoline would run its scheduler on a thread
   * whose stack and signal mask it knows nothing about. */
  sigset_t all;
  sigfillset(&all);
  pthread_sigmask(SIG_BLOCK, &all, NULL);

  while (!atomic_load(&stop_requested)) {
    struct can_frame cf;
    ssize_t n = read(can_sock, &cf, sizeof cf);
    if (n != (ssize_t)sizeof cf) {
      continue; /* receive timeout (to poll stop_requested) or short read */
    }
    uint64_t t = now_ns();

    uint32_t head = atomic_load_explicit(&hw_head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&hw_tail, memory_order_acquire);
    if (head - tail >= ICM_CAN_HW_FIFO_DEPTH) {
      atomic_fetch_add(&hw_overruns, 1u);
      atomic_fetch_add(&total_hw_dropped, 1u);
    } else {
      atomic_fetch_add(&total_pushed, 1u);
      can_ctrl_rx_t *slot = &hw_fifo[head & HW_MASK];
      slot->frame.id = cf.can_id & CAN_SFF_MASK;
      slot->frame.dlc = cf.can_dlc;
      memcpy(slot->frame.data, cf.data, sizeof slot->frame.data);
      slot->hw_rx_ns = t;
      slot->isr_ns = 0;
      atomic_store_explicit(&hw_head, head + 1u, memory_order_release);
    }
    /* Raise the interrupt line, also on overrun: the overrun flag is an
     * interrupt source on real controllers too. */
    kill(getpid(), irq_sig);
  }
  return NULL;
}

int can_ctrl_start(const char *ifname, int irq_signal)
{
  irq_sig = irq_signal;

  can_sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (can_sock < 0) {
    return -1;
  }

  struct ifreq ifr;
  memset(&ifr, 0, sizeof ifr);
  snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", ifname);
  if (ioctl(can_sock, SIOCGIFINDEX, &ifr) < 0) {
    close(can_sock);
    can_sock = -1;
    return -1;
  }

  /* Acceptance filter: the cluster listens to four identifiers. Done in the
   * kernel so that unrelated bus traffic never reaches the FIFO, which is the
   * job of the filter banks on a real controller. */
  struct can_filter filters[ICM_MSG_COUNT];
  for (int m = 0; m < ICM_MSG_COUNT; m++) {
    filters[m].can_id = can_db_id_from_msg((icm_msg_t)m);
    filters[m].can_mask = CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG;
  }
  setsockopt(can_sock, SOL_CAN_RAW, CAN_RAW_FILTER, filters, sizeof filters);

  /* 100 ms receive timeout so the thread notices can_ctrl_stop(). */
  struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};
  setsockopt(can_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

  struct sockaddr_can addr;
  memset(&addr, 0, sizeof addr);
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;
  if (bind(can_sock, (struct sockaddr *)&addr, sizeof addr) < 0) {
    close(can_sock);
    can_sock = -1;
    return -1;
  }

  atomic_store(&stop_requested, false);
  if (pthread_create(&ctrl_thread, NULL, controller_main, NULL) != 0) {
    close(can_sock);
    can_sock = -1;
    return -1;
  }
  online = true;
  return 0;
}

void can_ctrl_stop(void)
{
  atomic_store(&stop_requested, true);
}

bool can_ctrl_online(void)
{
  return online;
}

unsigned can_ctrl_isr_drain(void)
{
  unsigned moved = 0;
  uint64_t t = now_ns();
  uint32_t tail = atomic_load_explicit(&hw_tail, memory_order_relaxed);
  uint32_t head = atomic_load_explicit(&hw_head, memory_order_acquire);

  while (tail != head) {
    can_ctrl_rx_t entry = hw_fifo[tail & HW_MASK];
    tail++;
    entry.isr_ns = t;
    if (sw_head - sw_tail >= ICM_CAN_SW_QUEUE_DEPTH) {
      sw_overruns++;
      total_sw_dropped++;
    } else {
      sw_queue[sw_head & SW_MASK] = entry;
      sw_head++;
    }
    moved++;
  }
  total_drained += moved;
  atomic_store_explicit(&hw_tail, tail, memory_order_release);
  return moved;
}

bool can_ctrl_pop(can_ctrl_rx_t *out)
{
  bool got = false;
  SuspendOSInterrupts();
  if (sw_tail != sw_head) {
    *out = sw_queue[sw_tail & SW_MASK];
    sw_tail++;
    total_popped++;
    got = true;
  }
  ResumeOSInterrupts();
  return got;
}

void can_ctrl_totals(uint32_t *pushed, uint32_t *hw_dropped, uint32_t *drained,
                     uint32_t *sw_dropped, uint32_t *popped)
{
  *pushed = atomic_load(&total_pushed);
  *hw_dropped = atomic_load(&total_hw_dropped);
  *drained = total_drained;
  *sw_dropped = total_sw_dropped;
  *popped = total_popped;
}

/* Called from ISR_CAN_RX, so sw_overruns needs no further protection. */
bool can_ctrl_overrun_pending(void)
{
  return atomic_load(&hw_overruns) != 0u || sw_overruns != 0u;
}

uint32_t can_ctrl_take_hw_overruns(void)
{
  return atomic_exchange(&hw_overruns, 0u);
}

uint32_t can_ctrl_take_sw_overruns(void)
{
  SuspendOSInterrupts();
  uint32_t n = sw_overruns;
  sw_overruns = 0;
  ResumeOSInterrupts();
  return n;
}
