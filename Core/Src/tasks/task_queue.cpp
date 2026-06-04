/*
 * task_queue.cpp
 *
 *  Created on: Jun 4, 2026
 *      Author: embershine
 */

#include "tasks/task_queue.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "UartLogger.hpp"
#include <stdio.h>

// Dedicated queue for the DROP-NEW demo. Separate from xLatencyQueue (Phase 2A).
static QueueHandle_t g_demoQueue = NULL;
static const UBaseType_t QUEUE_DEPTH = 5;

// Two counters, kept separate by design (see README):
//   drop_count     = items actually lost (data loss)
//   overflow_count = queue-full events (saturation)
// They increment together in this demo but measure different concepts and
// diverge once a send timeout or partial-retention policy is introduced.
// Single writer (producer) -> low race risk; volatile for cross-task visibility.
static volatile uint32_t queue_drop_count = 0;
static volatile uint32_t queue_overflow_count = 0;

// Task handles needed for stack high-water mark reporting.
static TaskHandle_t s_hProducer = NULL;
static TaskHandle_t s_hConsumer = NULL;

void QueueDemo_Init(void)
{
    g_demoQueue = xQueueCreate(QUEUE_DEPTH, sizeof(uint32_t));
    if (g_demoQueue == NULL) {
        UartLogger::getInstance().log("[QUEUE] ERROR: xQueueCreate failed\r\n");
        configASSERT(g_demoQueue != NULL);
    }
}

// Producer: faster period (10ms). On queue-full, applies DROP-NEW:
// the new item is NOT enqueued and existing items are preserved.
// No per-drop UART logging (avoids UART becoming the bottleneck).
void Task_QueueProducer(void *argument)
{
    s_hProducer = xTaskGetCurrentTaskHandle();

    uint32_t item = 0;
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        item++;
        // timeout 0 = non-blocking send => DROP-NEW on full.
        if (xQueueSend(g_demoQueue, &item, 0) != pdPASS) {
            queue_overflow_count++;  // saturation event
            queue_drop_count++;      // new item lost
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(10));
    }
}

// Consumer: slower period (100ms). Drains one item per cycle and prints the
// counters every 10th cycle (~1 line/sec) plus stack high-water marks.
void Task_QueueConsumer(void *argument)
{
    s_hConsumer = xTaskGetCurrentTaskHandle();

    UartLogger::getInstance().log("[QUEUE] consumer task started\r\n");

    uint32_t cycle = 0;
    uint32_t item = 0;
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        // Drain one item if available (non-blocking).
        (void)xQueueReceive(g_demoQueue, &item, 0);

        if (++cycle % 10 == 0) {
            char buf[64];
            snprintf(buf, sizeof(buf),
                     "[QUEUE] drop_count=%lu overflow_count=%lu\r\n",
                     (unsigned long)queue_drop_count,
                     (unsigned long)queue_overflow_count);
            UartLogger::getInstance().log(buf);

            // Stack high-water marks (words). Lower = closer to overflow.
            UBaseType_t wmP = (s_hProducer != NULL)
                            ? uxTaskGetStackHighWaterMark(s_hProducer) : 0;
            UBaseType_t wmC = (s_hConsumer != NULL)
                            ? uxTaskGetStackHighWaterMark(s_hConsumer) : 0;
            snprintf(buf, sizeof(buf),
                     "[QUEUE] stack_hwm prod=%lu cons=%lu (words)\r\n",
                     (unsigned long)wmP, (unsigned long)wmC);
            UartLogger::getInstance().log(buf);
        }

        vTaskDelayUntil(&last, pdMS_TO_TICKS(100));
    }
}
