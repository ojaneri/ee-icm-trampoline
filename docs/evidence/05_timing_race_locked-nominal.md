run: race_locked-nominal
{
  "variant": "race_locked",
  "scenario": "nominal",
  "seconds": 20.0,
  "iface": "vcan0",
  "sched": "SCHED_FIFO 50",
  "cmd": [
    "chrt",
    "-f",
    "50",
    "/root/icm-work/ee-icm-trampoline/build/race_locked/icm_exe"
  ]
}
| SystemCounter tick | value |
|---|---|
| ticks recorded | 2000 |
| mean period | 10.0505 ms (+0.51 % vs 10 ms) |
| stdev / min / p99 / max | 0.086 / 8.744 / 10.222 / 11.666 ms |

| Task | T (ms) | jobs | release gaps (ticks) | started in release tick | wall period mean / min / max (ms) | start delay p99 (us) | response mean / p99 / max (us) | exec mean / max (us) | misses |
|---|---|---|---|---|---|---|---|---|---|
| Task_CAN_RX | sporadic (D=10) | 254 | - | - | - | irq p99 102.5 | 1357.1 / 11809.7 / 11910.7 | 4.6 / 20.1 | 11 |
| Task_Acquisition | 10 | 1990 | 1x1989 | 100.0 % | 10.051 / 7.674 / 12.192 | 2034.5 | 409.4 / 2039.2 / 2446.6 | 4.7 / 38.2 | 0 |
| Task_Processing | 20 | 995 | 2x994 | 100.0 % | 20.101 / 17.931 / 22.273 | 2040.2 | 414.7 / 2044.3 / 2216.2 | 3.8 / 25.5 | 0 |
| Task_Display | 50 | 398 | 5x397 | 100.0 % | 50.252 / 49.826 / 52.583 | 41.5 | 12089.0 / 12153.1 / 12283.6 | 12058.3 / 12228.3 | 0 |
| Task_Diagnostics | 100 | 199 | 10x198 | 0.0 % | 100.505 / 100.273 / 102.398 | 12176.7 | 12103.1 / 12186.6 / 12276.8 | 10.0 / 51.7 | 0 |
| Task_Logging | 1000 | 20 | 100x19 | 0.0 % | 1005.089 / 1003.875 / 1007.973 | 12279.7 | 12151.4 / 12294.9 / 12294.9 | 13.5 / 32.8 | 0 |

Utilisation U = 25.693 % (Liu & Layland bound for n = 6: 73.5 %)

| Task | C (ms) | B (ms) | R (ms) | D (ms) | R <= D |
|---|---|---|---|---|---|
| Task_CAN_RX | 0.020 | 12.228 | 12.248 | 10 | NO |
| Task_Acquisition | 0.038 | 12.228 | 12.367 | 10 | NO |
| Task_Processing | 0.026 | 12.228 | 12.431 | 20 | yes |
| Task_Display | 12.228 | 0.052 | 12.483 | 50 | yes |
| Task_Diagnostics | 0.052 | 0.033 | 12.515 | 100 | yes |
| Task_Logging | 0.033 | 0.000 | 12.515 | 1000 | yes |
