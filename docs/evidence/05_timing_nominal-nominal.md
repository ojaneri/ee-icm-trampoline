run: nominal-nominal
{
  "variant": "nominal",
  "scenario": "nominal",
  "seconds": 31.0,
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
| ticks recorded | 3100 |
| mean period | 10.0480 ms (+0.48 % vs 10 ms) |
| stdev / min / p99 / max | 0.043 / 9.268 / 10.162 / 10.895 ms |

| Task | T (ms) | jobs | release gaps (ticks) | started in release tick | wall period mean / min / max (ms) | start delay p99 (us) | response mean / p99 / max (us) | exec mean / max (us) | misses |
|---|---|---|---|---|---|---|---|---|---|
| Task_CAN_RX | sporadic (D=10) | 420 | - | - | - | irq p99 139.1 | 52.4 / 148.3 / 157.8 | 4.5 / 31.8 | 0 |
| Task_Acquisition | 10 | 3090 | 1x3089 | 100.0 % | 10.048 / 9.269 / 10.895 | 17.3 | 9.9 / 30.0 / 133.5 | 4.9 / 127.7 | 0 |
| Task_Processing | 20 | 1545 | 2x1544 | 100.0 % | 20.096 / 19.894 / 20.954 | 32.5 | 16.9 / 37.6 / 141.5 | 4.0 / 26.1 | 0 |
| Task_Display | 50 | 618 | 5x617 | 100.0 % | 50.240 / 49.922 / 51.082 | 38.9 | 72.7 / 139.9 / 319.9 | 55.9 / 174.9 | 0 |
| Task_Diagnostics | 100 | 309 | 10x308 | 100.0 % | 100.480 / 100.260 / 101.350 | 167.4 | 88.6 / 177.3 / 202.0 | 9.4 / 49.5 | 0 |
| Task_Logging | 1000 | 31 | 100x30 | 100.0 % | 1004.787 / 1003.701 / 1005.995 | 196.4 | 135.6 / 209.9 / 209.9 | 13.7 / 36.2 | 0 |

Utilisation U = 2.869 % (Liu & Layland bound for n = 6: 73.5 %)

| Task | C (ms) | B (ms) | R (ms) | D (ms) | R <= D |
|---|---|---|---|---|---|
| Task_CAN_RX | 0.032 | 0.175 | 0.207 | 10 | yes |
| Task_Acquisition | 0.128 | 0.175 | 0.334 | 10 | yes |
| Task_Processing | 0.026 | 0.175 | 0.360 | 20 | yes |
| Task_Display | 0.175 | 0.050 | 0.410 | 50 | yes |
| Task_Diagnostics | 0.050 | 0.036 | 0.446 | 100 | yes |
| Task_Logging | 0.036 | 0.000 | 0.446 | 1000 | yes |
