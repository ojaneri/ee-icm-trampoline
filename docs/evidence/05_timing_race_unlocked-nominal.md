run: race_unlocked-nominal
{
  "variant": "race_unlocked",
  "scenario": "nominal",
  "seconds": 20.0,
  "iface": "vcan0",
  "sched": "SCHED_FIFO 50",
  "cmd": [
    "chrt",
    "-f",
    "50",
    "/root/icm-work/ee-icm-trampoline/build/race_unlocked/icm_exe"
  ]
}
| SystemCounter tick | value |
|---|---|
| ticks recorded | 2000 |
| mean period | 10.0461 ms (+0.46 % vs 10 ms) |
| stdev / min / p99 / max | 0.031 / 9.722 / 10.157 / 10.463 ms |

| Task | T (ms) | jobs | release gaps (ticks) | started in release tick | wall period mean / min / max (ms) | start delay p99 (us) | response mean / p99 / max (us) | exec mean / max (us) | misses |
|---|---|---|---|---|---|---|---|---|---|
| Task_CAN_RX | sporadic (D=10) | 266 | - | - | - | irq p99 132.6 | 49.8 / 140.8 / 457.5 | 2.8 / 26.2 | 0 |
| Task_Acquisition | 10 | 1990 | 1x1989 | 100.0 % | 10.046 / 9.720 / 10.464 | 9.4 | 6.4 / 13.6 / 73.5 | 1.6 / 16.3 | 0 |
| Task_Processing | 20 | 995 | 2x994 | 100.0 % | 20.092 / 19.892 / 20.519 | 24.0 | 10.6 / 25.7 / 78.5 | 1.3 / 69.9 | 0 |
| Task_Display | 50 | 398 | 5x397 | 100.0 % | 50.230 / 50.023 / 50.653 | 29.1 | 12064.0 / 12120.5 / 12136.8 | 12043.0 / 12111.6 | 0 |
| Task_Diagnostics | 100 | 199 | 10x198 | 0.0 % | 100.461 / 100.227 / 100.894 | 12140.6 | 12080.2 / 12148.0 / 12149.2 | 7.5 / 56.0 | 0 |
| Task_Logging | 1000 | 20 | 100x19 | 0.0 % | 1004.606 / 1003.780 / 1006.693 | 12172.0 | 12129.1 / 12188.6 / 12188.6 | 11.9 / 30.4 | 0 |

Utilisation U = 25.666 % (Liu & Layland bound for n = 6: 73.5 %)

| Task | C (ms) | B (ms) | R (ms) | D (ms) | R <= D |
|---|---|---|---|---|---|
| Task_CAN_RX | 0.026 | 12.112 | 12.138 | 10 | NO |
| Task_Acquisition | 0.016 | 12.112 | 12.259 | 10 | NO |
| Task_Processing | 0.070 | 12.112 | 12.345 | 20 | yes |
| Task_Display | 12.112 | 0.056 | 12.401 | 50 | yes |
| Task_Diagnostics | 0.056 | 0.030 | 12.431 | 100 | yes |
| Task_Logging | 0.030 | 0.000 | 12.431 | 1000 | yes |
