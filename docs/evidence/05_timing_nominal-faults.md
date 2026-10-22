run: nominal-faults
{
  "variant": "nominal",
  "scenario": "faults",
  "seconds": 33.0,
  "iface": "vcan0",
  "sched": "SCHED_FIFO 50",
  "cmd": [
    "chrt",
    "-f",
    "50",
    "/root/icm-work/ee-icm-trampoline/build/nominal/icm_exe"
  ]
}
| SystemCounter tick | value |
|---|---|
| ticks recorded | 3300 |
| mean period | 10.0484 ms (+0.48 % vs 10 ms) |
| stdev / min / p99 / max | 0.057 / 8.938 / 10.185 / 11.318 ms |

| Task | T (ms) | jobs | release gaps (ticks) | started in release tick | wall period mean / min / max (ms) | start delay p99 (us) | response mean / p99 / max (us) | exec mean / max (us) | misses |
|---|---|---|---|---|---|---|---|---|---|
| Task_CAN_RX | sporadic (D=10) | 622 | - | - | - | irq p99 104.5 | 43.5 / 118.4 / 199.4 | 4.1 / 24.2 | 0 |
| Task_Acquisition | 10 | 3290 | 1x3289 | 100.0 % | 10.048 / 8.941 / 11.318 | 18.4 | 10.2 / 31.3 / 218.6 | 4.9 / 75.0 | 0 |
| Task_Processing | 20 | 1645 | 2x1644 | 100.0 % | 20.097 / 19.619 / 21.261 | 35.8 | 17.1 / 41.6 / 84.1 | 4.1 / 72.2 | 0 |
| Task_Display | 50 | 658 | 5x657 | 100.0 % | 50.242 / 50.077 / 51.547 | 35.1 | 70.2 / 131.7 / 154.6 | 53.9 / 140.7 | 0 |
| Task_Diagnostics | 100 | 329 | 10x328 | 100.0 % | 100.485 / 100.254 / 101.757 | 134.2 | 86.4 / 144.0 / 159.6 | 10.5 / 62.7 | 0 |
| Task_Logging | 1000 | 33 | 100x32 | 100.0 % | 1004.839 / 1003.576 / 1009.200 | 147.4 | 125.8 / 187.2 / 187.2 | 11.9 / 44.8 | 0 |

Utilisation U = 2.268 % (Liu & Layland bound for n = 6: 73.5 %)

| Task | C (ms) | B (ms) | R (ms) | D (ms) | R <= D |
|---|---|---|---|---|---|
| Task_CAN_RX | 0.024 | 0.141 | 0.165 | 10 | yes |
| Task_Acquisition | 0.075 | 0.141 | 0.240 | 10 | yes |
| Task_Processing | 0.072 | 0.141 | 0.312 | 20 | yes |
| Task_Display | 0.141 | 0.063 | 0.375 | 50 | yes |
| Task_Diagnostics | 0.063 | 0.045 | 0.420 | 100 | yes |
| Task_Logging | 0.045 | 0.000 | 0.420 | 1000 | yes |
