/*
 * task_processing.cpp
 *
 *  Created on: Jun 2, 2026
 *      Author: embershine
 */

#include "tasks/task_processing.h"
#include "FreeRTOS.h"
#include "task.h"
#include "UartLogger.hpp"
#include "HealthMonitor.hpp"
#include <stdio.h>


void Task_Processing(void *argument)
{
    //UartLogger::log("[PROC] task started\r\n");
    UartLogger::getInstance().log("[PROC] task started\r\n");
    for (;;) {
        /* 처리 시뮬레이션 */
        volatile uint32_t dummy = 0;
        for (volatile int j = 0; j < 30000; j++) dummy++;
        //UartLogger::log("[PROC] processing tick\r\n");
        UartLogger::getInstance().log("[PROC] processing tick\r\n");

//        /* BEFORE */
//        vTaskDelay(pdMS_TO_TICKS(750));

        /* AFTER */
        HealthMonitor::getInstance().heartbeat(HM_TASK_PROC);  /* work complete */
        vTaskDelay(pdMS_TO_TICKS(750));

    }
}
