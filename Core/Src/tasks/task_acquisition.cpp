/*
 * task_acquisition.cpp
 *
 *  Created on: Jun 2, 2026
 *      Author: embershine
 */

#include "tasks/task_acquisition.h"
#include "FreeRTOS.h"
#include "task.h"
#include "UartLogger.hpp"
#include <stdio.h>

// origin code start
//static void trigger_overflow(void) {
//    volatile uint8_t big[2048];
//    big[0] = 1;
//    (void)big;
//}
// origin code end

/* Intentional infinite recursion to trigger stack overflow hook.
 * Used in Phase 1 stack overflow detection test only.
 * -Winfinite-recursion suppressed intentionally. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winfinite-recursion"
static void trigger_overflow(int depth) {
    volatile uint8_t chunk[64];
    chunk[0] = (uint8_t)depth;
    (void)chunk;
    trigger_overflow(depth + 1);
}
#pragma GCC diagnostic pop

void Task_Acquisition(void *argument)
{

	UartLogger::getInstance().log("[ACQ] task started\r\n");

    //vTaskDelay(pdMS_TO_TICKS(5000));  // wait 5000ms // Disabled: debugger attach wait, used in Phase 1
    //UartLogger::getInstance().log("[ACQ] triggering overflow...\r\n"); // Disabled after Phase 1
    //trigger_overflow(0); // Disabled after Phase 1 stack overflow test

    //UartLogger::log("[ACQ] task started\r\n");


    /* --- vTaskDelay 방식 (비교용, 처음 10회) --- */
    for (int i = 0; i < 10; i++) {
        TickType_t t0 = xTaskGetTickCount();
        /* 작업 시뮬레이션 */
        volatile uint32_t dummy = 0;
        for (volatile int j = 0; j < 50000; j++) dummy++;

        TickType_t elapsed = xTaskGetTickCount() - t0;
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "[ACQ] vTaskDelay  tick_before=%lu elapsed=%lu\r\n",
                 (unsigned long)t0, (unsigned long)elapsed);
        //UartLogger::log(buf);
        UartLogger::getInstance().log(buf);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    /* --- vTaskDelayUntil 방식 (이후 계속) --- */
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod  = pdMS_TO_TICKS(500);

    for (;;) {
        TickType_t t0 = xTaskGetTickCount();
        volatile uint32_t dummy = 0;
        for (volatile int j = 0; j < 50000; j++) dummy++;

        TickType_t elapsed = xTaskGetTickCount() - t0;
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "[ACQ] vTaskDelayUntil tick_before=%lu elapsed=%lu\r\n",
                 (unsigned long)t0, (unsigned long)elapsed);
        //UartLogger::log(buf);
        UartLogger::getInstance().log(buf);
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}
