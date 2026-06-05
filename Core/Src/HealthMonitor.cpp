/*
 * HealthMonitor.cpp
 *
 *  Created on: Jun 4, 2026
 *      Author: embershine
 */


#include "HealthMonitor.hpp"
#include "UartLogger.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Singleton                                                           */
/* ------------------------------------------------------------------ */

HealthMonitor& HealthMonitor::getInstance()
{
    static HealthMonitor instance;
    return instance;
}

HealthMonitor::HealthMonitor()
    : faultDetected(false)
{
    memset(entries, 0, sizeof(entries));
}

/* ------------------------------------------------------------------ */
/* registerTask                                                        */
/* ------------------------------------------------------------------ */

void HealthMonitor::registerTask(HM_TaskId id, uint32_t timeoutMs)
{
    if (id >= HEALTH_MONITOR_MAX_TASKS) return;

    taskENTER_CRITICAL();
    entries[id].registered    = true;
    entries[id].timeoutTicks  = pdMS_TO_TICKS(timeoutMs);
    entries[id].lastHeartbeat = xTaskGetTickCount(); /* boot-time init */
    entries[id].faultLatched  = false;
    taskEXIT_CRITICAL();

    char buf[64];
    snprintf(buf, sizeof(buf),
             "[HM] registered task id=%d timeout=%lums\r\n",
             (int)id, (unsigned long)timeoutMs);
    UartLogger::getInstance().log(buf);
}

/* ------------------------------------------------------------------ */
/* heartbeat                                                           */
/* ------------------------------------------------------------------ */

void HealthMonitor::heartbeat(HM_TaskId id)
{
    if (id >= HEALTH_MONITOR_MAX_TASKS) return;

    taskENTER_CRITICAL();
    if (entries[id].registered) {
        entries[id].lastHeartbeat = xTaskGetTickCount();
    }
    taskEXIT_CRITICAL();
}

/* ------------------------------------------------------------------ */
/* check                                                               */
/* ------------------------------------------------------------------ */

void HealthMonitor::check()
{
    TickType_t now = xTaskGetTickCount();

    for (int i = 0; i < HEALTH_MONITOR_MAX_TASKS; i++) {

        taskENTER_CRITICAL();
        bool       reg      = entries[i].registered;
        bool       latched  = entries[i].faultLatched;
        TickType_t lastBeat = entries[i].lastHeartbeat;
        TickType_t timeout  = entries[i].timeoutTicks;
        taskEXIT_CRITICAL();

        if (!reg || latched) continue;

        TickType_t elapsed = now - lastBeat;

        if (elapsed > timeout) {
            taskENTER_CRITICAL();
            entries[i].faultLatched = true;
            faultDetected           = true;
            taskEXIT_CRITICAL();

            char buf[80];
            snprintf(buf, sizeof(buf),
                     "[HM] FAULT latched task id=%d elapsed=%lums timeout=%lums\r\n",
                     i,
                     (unsigned long)(elapsed * portTICK_PERIOD_MS),
                     (unsigned long)(timeout  * portTICK_PERIOD_MS));
            UartLogger::getInstance().log(buf);
        }
    }
}

/* ------------------------------------------------------------------ */
/* isFaultDetected                                                     */
/* ------------------------------------------------------------------ */

bool HealthMonitor::isFaultDetected()
{
    taskENTER_CRITICAL();
    bool val = faultDetected;
    taskEXIT_CRITICAL();
    return val;
}

/* ------------------------------------------------------------------ */
/* getFaultTaskId                                                      */
/* ------------------------------------------------------------------ */

int HealthMonitor::getFaultTaskId()
{
    taskENTER_CRITICAL();
    int result = -1;
    for (int i = 0; i < HEALTH_MONITOR_MAX_TASKS; i++) {
        if (entries[i].registered && entries[i].faultLatched) {
            result = i;
            break;
        }
    }
    taskEXIT_CRITICAL();
    return result;
}

/* ------------------------------------------------------------------ */
/* Task_HealthMonitor                                                  */
/* ------------------------------------------------------------------ */

/**
 * Priority: 5
 *   Detects CPU hog for tasks at priority <= 4.
 *   Higher-priority tasks or interrupt storms are NOT detectable
 *   by this monitor — known limitation.
 *
 * Stack:  256 words
 * Period: 500ms
 */
extern "C" void Task_HealthMonitor(void *argument)
{
    (void)argument;

    static bool faultLoggedOnce = false;

    HealthMonitor& hm = HealthMonitor::getInstance();

    hm.registerTask(HM_TASK_ACQ,  1500U);  /* ACQ:  500ms x 3 */
    hm.registerTask(HM_TASK_PROC, 2250U);  /* PROC: 750ms x 3 */

    UartLogger::getInstance().log("[HM] HealthMonitor started\r\n");

    for (;;) {
        hm.check();

        if (hm.isFaultDetected() && !faultLoggedOnce) {
            faultLoggedOnce = true;
            UartLogger::getInstance().log(
                "[HM] fault active - IWDG feed stop pending (Phase 4.3)\r\n");
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
