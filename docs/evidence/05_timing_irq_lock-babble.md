run: irq_lock-babble
{
  "variant": "irq_lock",
  "scenario": "babble",
  "seconds": 9.0,
  "iface": "vcan0",
  "sched": "SCHED_FIFO 50",
  "cmd": [
    "chrt",
    "-f",
    "50",
    "/root/icm-work/ee-icm-trampoline/build/irq_lock/icm_exe"
  ]
}
| SystemCounter tick | value |
|---|---|
| ticks recorded | 900 |
| mean period | 10.2436 ms (+2.44 % vs 10 ms) |
| stdev / min / p99 / max | 6.344 / 0.595 / 10.143 / 200.112 ms |

| Task | T (ms) | jobs | release gaps (ticks) | started in release tick | wall period mean / min / max (ms) | start delay p99 (us) | response mean / p99 / max (us) | exec mean / max (us) | misses |
|---|---|---|---|---|---|---|---|---|---|
| Task_CAN_RX | sporadic (D=10) | 3879 | - | - | - | irq p99 110.6 | 102.8 / 119.4 / 189646.6 | 4.5 / 105.8 | 1 |
| Task_Acquisition | 10 | 890 | 1x889 | 100.0 % | 10.246 / 0.560 / 200.146 | 20.7 | 8.9 / 31.5 / 69.1 | 4.5 / 36.1 | 0 |
| Task_Processing | 20 | 445 | 2x444 | 100.0 % | 20.492 / 20.019 / 200.706 | 30.2 | 15.6 / 34.1 / 75.6 | 3.8 / 10.5 | 0 |
| Task_Display | 50 | 178 | 5x177 | 100.0 % | 51.234 / 50.120 / 230.799 | 37.1 | 71.3 / 117.4 / 203.7 | 56.4 / 183.5 | 0 |
| Task_Diagnostics | 100 | 89 | 10x88 | 100.0 % | 102.479 / 100.241 / 281.478 | 207.0 | 2337.1 / 200204.7 / 200204.7 | 2257.9 / 200070.7 | 1 |
| Task_Logging | 1000 | 9 | 100x8 | 100.0 % | 1026.876 / 1003.446 / 1185.150 | 230.3 | 140.9 / 242.5 / 242.5 | 12.1 / 14.0 | 0 |

Utilisation U = 204.380 % (Liu & Layland bound for n = 6: 73.5 %)

| Task | C (ms) | B (ms) | R (ms) | D (ms) | R <= D |
|---|---|---|---|---|---|
| Task_CAN_RX | 0.106 | 200.071 | 200.177 | 10 | NO |
| Task_Acquisition | 0.036 | 200.071 | 207.515 | 10 | NO |
| Task_Processing | 0.010 | 200.071 | 208.248 | 20 | NO |
| Task_Display | 0.184 | 200.071 | 208.536 | 50 | NO |
| Task_Diagnostics | 200.071 | 0.014 | 209.284 | 100 | NO |
| Task_Logging | 0.014 | 0.000 | inf | 1000 | NO |
