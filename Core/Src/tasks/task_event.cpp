/*
 * task_event.cpp
 *
 *  Created on: Jun 4, 2026
 *      Author: embershine
 */

#include "tasks/task_event.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "UartLogger.hpp"
#include <stdio.h>

// Binary semaphore signaled from the PC13 EXTI ISR (xSemaphoreGiveFromISR).
// Consumer (Task_Event) blocks on it. Separate from xLatencyQueue (Phase 2A).
SemaphoreHandle_t g_buttonEventSem = NULL;

// Observability-only ISR entry counter (see header note). volatile because it
// is written in ISR context and read in task context. NOT used for control.
volatile uint32_t g_isrRawCount = 0;

// Phase 3 event counter: number of events actually processed by the consumer.
// Distinct from the Phase 2A latency path; this path owns its own counter.
static uint32_t button_event_count = 0;

void EventDemo_Init(void)
{
    // Binary semaphore for ISR-to-task event signaling.
    // Starts empty: the consumer blocks until the first button press gives it.
    g_buttonEventSem = xSemaphoreCreateBinary();
}

// Consumer task: blocks until the ISR signals a button event, then logs.
// Priority chosen below the latency task so the two paths stay independent;
// adjust at creation site in main.cpp.
void Task_Event(void *argument)
{
    UartLogger::getInstance().log("[EVENT] consumer task started\r\n");

    for (;;) {
        // Block until the ISR gives the semaphore on a button press.
        if (xSemaphoreTake(g_buttonEventSem, portMAX_DELAY) == pdTRUE) {
            button_event_count++;

            char buf[96];
            // isr_raw_count may exceed button_event_count: multiple ISR
            // triggers (bounce, or consumer not yet ready) are coalesced
            // by the binary semaphore into a single pending signal.
            snprintf(buf, sizeof(buf),
                     "[EVENT] button_event_count=%lu isr_raw_count=%lu\r\n",
                     (unsigned long)button_event_count,
                     (unsigned long)g_isrRawCount);
            UartLogger::getInstance().log(buf);
        }
    }
}
