/**
 * ICM Trampoline RTOS Port for STM32 Nucleo L476RG
 * Uses Hardware Timer (TIM2) as SystemCounter (1 ms tick)
 * UART2 for debug/dashboard logging
 */

#include <stdint.h>
#include <stdbool.h>

// Simulated OSEK Task context on STM32L476RG
typedef void (*TaskFunc)(void);

typedef struct {
    const char* name;
    uint32_t period_ms;
    uint32_t elapsed;
    TaskFunc func;
} TaskControl;

// Shared data structure protected by resource
typedef struct {
    float speed;
    int rpm;
    float fuel;
    float temp;
    bool fault_active;
    char message[48];
} ICM_Data_t;

static volatile ICM_Data_t icm_data = {
    .speed = 0.0f,
    .rpm = 800,
    .fuel = 65.0f,
    .temp = 82.0f,
    .fault_active = false,
    .message = "NUCLEO L476RG BOOT OK"
};

// Task stubs mimicking icm_app.c
void Task_Acquisition(void) {
    // Simulates reading CAN signals / sensors
    icm_data.speed = 120.0f;
    icm_data.rpm = 3120;
}

void Task_Processing(void) {
    // Low pass filter & scaling
    if (icm_data.rpm > 6500) {
        icm_data.fault_active = true;
        __builtin_strcpy(icm_data.message, "ENGINE OVERHEAT / REDLINE");
    }
}

void Task_Display(void) {
    // Output state over UART2 (ST-Link Virtual COM port at 115200 baud)
    // Visible in a serial monitor on COM3
}

// Minimal CMSIS-Core setup for STM32L476RG (Nucleo)
int main(void) {
    // System clock init (80 MHz via MSI/PLL)
    // SysTick or TIM2 configured for 1ms tick

    while (1) {
        // Super-loop emulating OSEK scheduler fixed-priority preemptive scheduling
        Task_Acquisition();
        Task_Processing();
        Task_Display();
    }
}
