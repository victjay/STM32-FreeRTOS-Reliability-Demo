/*
 * task_latency.cpp
 *
 *  Created on: Jun 3, 2026
 *      Author: embershine
 */

#include "tasks/task_latency.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "latency_queue.h"
#include "LatencyMeter.hpp"
#include "UartLogger.hpp"
#include "stm32f4xx.h"

volatile uint32_t isr_seq = 0;

void task_latency(void *pvParameters)
{
    uint32_t isr_cycle;
    uint32_t task_seq = 0;
    char buf[80];

    UartLogger::getInstance().log("[LAT] task started\r\n");
    UartLogger::getInstance().log("[LAT] seq,isr_cycle,task_cycle,delta_cycles,latency_us\r\n");

    for (;;)
    {
        if (xQueueReceive(xLatencyQueue, &isr_cycle, portMAX_DELAY) == pdTRUE)
        {
            uint32_t task_cycle = DWT->CYCCNT;  // 수신 직후 즉시 캡처

            LatencySample s;
            s.isr_cycle  = isr_cycle;
            s.task_cycle = task_cycle;
            s.seq        = task_seq++;

            LatencyMeter::formatCsvLine(s, SystemCoreClock, buf, sizeof(buf));
            UartLogger::getInstance().log(buf);
        }
    }
}
