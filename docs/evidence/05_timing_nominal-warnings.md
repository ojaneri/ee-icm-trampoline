run: nominal-warnings
{
  "variant": "nominal",
  "scenario": "warnings",
  "seconds": 16.0,
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
| ticks recorded | 1600 |
| mean period | 10.0465 ms (+0.47 % vs 10 ms) |
| stdev / min / p99 / max | 0.027 / 9.896 / 10.129 / 10.531 ms |

| Task | T (ms) | jobs | release gaps (ticks) | started in release tick | wall period mean / min / max (ms) | start delay p99 (us) | response mean / p99 / max (us) | exec mean / max (us) | misses |
|---|---|---|---|---|---|---|---|---|---|
| Task_CAN_RX | sporadic (D=10) | 210 | - | - | - | irq p99 98.5 | 52.8 / 109.1 / 153.0 | 4.5 / 24.6 | 0 |
| Task_Acquisition | 10 | 1590 | 1x1589 | 100.0 % | 10.046 / 9.898 / 10.531 | 14.8 | 9.5 / 24.1 / 63.7 | 4.7 / 48.8 | 0 |
| Task_Processing | 20 | 795 | 2x794 | 100.0 % | 20.093 / 19.943 / 20.578 | 34.7 | 16.6 / 48.6 / 128.2 | 4.1 / 117.7 | 0 |
| Task_Display | 50 | 318 | 5x317 | 100.0 % | 50.232 / 50.046 / 50.749 | 34.2 | 70.2 / 126.1 / 174.3 | 54.5 / 153.3 | 0 |
| Task_Diagnostics | 100 | 159 | 10x158 | 100.0 % | 100.464 / 100.221 / 100.983 | 135.0 | 87.5 / 152.0 / 187.9 | 9.2 / 41.7 | 0 |
| Task_Logging | 1000 | 16 | 100x15 | 100.0 % | 1004.634 / 1004.049 / 1006.103 | 144.8 | 127.5 / 159.9 / 159.9 | 11.4 / 15.1 | 0 |

Utilisation U = 2.247 % (Liu & Layland bound for n = 6: 73.5 %)

| Task | C (ms) | B (ms) | R (ms) | D (ms) | R <= D |
|---|---|---|---|---|---|
| Task_CAN_RX | 0.025 | 0.153 | 0.178 | 10 | yes |
| Task_Acquisition | 0.049 | 0.153 | 0.227 | 10 | yes |
| Task_Processing | 0.118 | 0.153 | 0.344 | 20 | yes |
| Task_Display | 0.153 | 0.042 | 0.386 | 50 | yes |
| Task_Diagnostics | 0.042 | 0.015 | 0.401 | 100 | yes |
| Task_Logging | 0.015 | 0.000 | 0.401 | 1000 | yes |
