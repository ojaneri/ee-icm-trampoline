run: nominal-sched_other
{
  "variant": "nominal",
  "scenario": "nominal",
  "seconds": 31.0,
  "iface": "vcan0",
  "sched": "SCHED_OTHER",
  "cmd": [
    "/root/icm-work/ee-icm-trampoline/build/nominal/icm_exe"
  ]
}
| SystemCounter tick | value |
|---|---|
| ticks recorded | 3100 |
| mean period | 10.1819 ms (+1.82 % vs 10 ms) |
| stdev / min / p99 / max | 0.675 / 6.280 / 12.651 / 23.212 ms |

| Task | T (ms) | jobs | release gaps (ticks) | started in release tick | wall period mean / min / max (ms) | start delay p99 (us) | response mean / p99 / max (us) | exec mean / max (us) | misses |
|---|---|---|---|---|---|---|---|---|---|
| Task_CAN_RX | sporadic (D=10) | 420 | - | - | - | irq p99 1336.7 | 107.9 / 1346.1 / 2915.9 | 5.3 / 68.3 | 0 |
| Task_Acquisition | 10 | 3090 | 1x3089 | 100.0 % | 10.182 / 6.280 / 23.213 | 18.3 | 10.3 / 31.2 / 120.1 | 5.1 / 115.8 | 0 |
| Task_Processing | 20 | 1545 | 2x1544 | 100.0 % | 20.364 / 17.076 / 34.457 | 36.1 | 17.3 / 41.3 / 127.1 | 3.9 / 57.9 | 0 |
| Task_Display | 50 | 618 | 5x617 | 100.0 % | 50.911 / 48.098 / 64.155 | 42.1 | 72.0 / 143.4 / 242.0 | 55.1 / 223.3 | 0 |
| Task_Diagnostics | 100 | 309 | 10x308 | 100.0 % | 101.823 / 98.926 / 115.402 | 162.1 | 90.4 / 172.6 / 255.5 | 10.4 / 85.7 | 0 |
| Task_Logging | 1000 | 31 | 100x30 | 100.0 % | 1018.350 / 1011.965 / 1030.343 | 161.1 | 132.6 / 179.9 / 179.9 | 13.2 / 31.1 | 0 |

Utilisation U = 4.258 % (Liu & Layland bound for n = 6: 73.5 %)

| Task | C (ms) | B (ms) | R (ms) | D (ms) | R <= D |
|---|---|---|---|---|---|
| Task_CAN_RX | 0.068 | 0.223 | 0.292 | 10 | yes |
| Task_Acquisition | 0.116 | 0.223 | 0.407 | 10 | yes |
| Task_Processing | 0.058 | 0.223 | 0.465 | 20 | yes |
| Task_Display | 0.223 | 0.086 | 0.551 | 50 | yes |
| Task_Diagnostics | 0.086 | 0.031 | 0.582 | 100 | yes |
| Task_Logging | 0.031 | 0.000 | 0.582 | 1000 | yes |
