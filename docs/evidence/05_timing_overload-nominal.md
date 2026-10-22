run: overload-nominal
{
  "variant": "overload",
  "scenario": "nominal",
  "seconds": 18.0,
  "iface": "vcan0",
  "sched": "SCHED_FIFO 50",
  "cmd": [
    "chrt",
    "-f",
    "50",
    "/root/icm-work/ee-icm-trampoline/build/overload/icm_exe"
  ]
}
| SystemCounter tick | value |
|---|---|
| ticks recorded | 1800 |
| mean period | 10.0441 ms (+0.44 % vs 10 ms) |
| stdev / min / p99 / max | 0.024 / 9.848 / 10.122 / 10.565 ms |

| Task | T (ms) | jobs | release gaps (ticks) | started in release tick | wall period mean / min / max (ms) | start delay p99 (us) | response mean / p99 / max (us) | exec mean / max (us) | misses |
|---|---|---|---|---|---|---|---|---|---|
| Task_CAN_RX | sporadic (D=10) | 238 | - | - | - | irq p99 65.4 | 43.3 / 76.9 / 83.0 | 4.3 / 18.4 | 0 |
| Task_Acquisition | 10 | 1790 | 1x1789 | 100.0 % | 10.044 / 9.847 / 10.564 | 12.6 | 9.3 / 23.5 / 72.4 | 4.6 / 40.6 | 0 |
| Task_Processing | 20 | 895 | 2x894 | 100.0 % | 20.088 / 19.890 / 20.606 | 27.9 | 16.3 / 42.1 / 79.4 | 3.9 / 38.6 | 0 |
| Task_Display | 50 | 358 | 5x357 | 100.0 % | 50.220 / 50.095 / 50.832 | 39.4 | 66.9 / 118.3 / 182.6 | 51.5 / 164.8 | 0 |
| Task_Diagnostics | 100 | 154 | 10x128, 20x25 | 100.0 % | 116.852 / 100.288 / 201.591 | 121.1 | 24435.4 / 150114.6 / 150121.1 | 24310.3 / 149788.4 | 25 |
| Task_Logging | 1000 | 18 | 100x17 | 72.2 % | 1004.382 / 954.495 / 1054.905 | 49736.7 | 13893.2 / 49773.9 / 49773.9 | 18.6 / 41.0 | 0 |

Utilisation U = 151.334 % (Liu & Layland bound for n = 6: 73.5 %)

| Task | C (ms) | B (ms) | R (ms) | D (ms) | R <= D |
|---|---|---|---|---|---|
| Task_CAN_RX | 0.018 | 149.788 | 149.807 | 10 | NO |
| Task_Acquisition | 0.041 | 149.788 | 150.767 | 10 | NO |
| Task_Processing | 0.039 | 149.788 | 151.414 | 20 | NO |
| Task_Display | 0.165 | 149.788 | 151.849 | 50 | NO |
| Task_Diagnostics | 149.788 | 0.041 | 152.384 | 100 | NO |
| Task_Logging | 0.041 | 0.000 | inf | 1000 | NO |
