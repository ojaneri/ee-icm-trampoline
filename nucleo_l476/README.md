# ICM STM32 Nucleo L476RG Port

This folder contains the embedded port structure for the **STM32 Nucleo L476RG** (Issue #3), running alongside the ESP32-C6 dashboard display.

## Files
- `platformio.ini`: PlatformIO config for the STM32L476RG board.
- `src/main.c`: Real-time task scheduler skeleton mimicking the 6 OSEK tasks and shared `ICM_Data` protection under resource ceiling.
