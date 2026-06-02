/*
 * task_monitor.cpp
 *
 *  Created on: Jun 2, 2026
 *      Author: embershine
 */

#include "tasks/task_monitor.h"
#include "FreeRTOS.h"
#include "task.h"
#include "UartLogger.hpp"
#include <stdio.h>

extern TaskHandle_t hAcquisition;
extern TaskHandle_t hProcessing;
extern TaskHandle_t hMonitor;


void Task_Monitor(void *argument)
{
    //UartLogger::log("[MON] task started\r\n");
    UartLogger::getInstance().log("[ACQ] task started\r\n");
    vTaskDelay(pdMS_TO_TICKS(2000)); /* 다른 task 먼저 실행되게 대기 */

    for (;;) {
        char buf[128];

        /* Stack high-water mark */
        UBaseType_t wmAcq  = uxTaskGetStackHighWaterMark(hAcquisition);
        UBaseType_t wmProc = uxTaskGetStackHighWaterMark(hProcessing);
        UBaseType_t wmMon  = uxTaskGetStackHighWaterMark(hMonitor);

        snprintf(buf, sizeof(buf),
                 "[MON] stack_hwm acq=%lu proc=%lu mon=%lu (words)\r\n",
                 (unsigned long)wmAcq, (unsigned long)wmProc, (unsigned long)wmMon);
        //UartLogger::log(buf);
        UartLogger::getInstance().log(buf);

        /* Heap free */
        size_t heapFree = xPortGetFreeHeapSize();
        size_t heapMin  = xPortGetMinimumEverFreeHeapSize();
        snprintf(buf, sizeof(buf),
                 "[MON] heap free=%u min_ever=%u (bytes)\r\n",
                 (unsigned)heapFree, (unsigned)heapMin);
        //UartLogger::log(buf);
        UartLogger::getInstance().log(buf);

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

