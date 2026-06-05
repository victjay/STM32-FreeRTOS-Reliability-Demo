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
#include "iwdg.h"   // phase 4.3 IWDG feed
#include <stdio.h>
#include <string.h>

#include "stm32f4xx_it.h"   /* WatchdogRecord, WDG_MAGIC */

/* iwdg handle defined in iwdg.c */
extern "C" IWDG_HandleTypeDef hiwdg;



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
 *
 * IWDG feed owner:
 *   This task is the SOLE owner of the IWDG refresh.
 *   - Healthy state: HAL_IWDG_Refresh() is called every cycle.
 *   - Fault latched: feed stops, IWDG resets the system after timeout.
 *   - If this task itself stops running, the IWDG is no longer fed
 *     and the system resets. This is intentional: if the monitor
 *     cannot run, the system state cannot be trusted.
 *   Feed period (500ms) must stay well below the IWDG timeout
 *   (target ~3s, ~2-5.7s across LSI tolerance).
 */
extern "C" void Task_HealthMonitor(void *argument)
{
    (void)argument;

    static bool faultLoggedOnce = false;

    HealthMonitor& hm = HealthMonitor::getInstance();

    hm.registerTask(HM_TASK_ACQ,  1500U);
    hm.registerTask(HM_TASK_PROC, 2250U);

    /* Immediate first feed to secure boot margin before IWDG timeout */
    HAL_IWDG_Refresh(&hiwdg);

    UartLogger::getInstance().log("[HM] HealthMonitor started (IWDG feed owner)\r\n");

    for (;;) {
        hm.check();

        if (!hm.isFaultDetected()) {
            /* Healthy: feed the watchdog */
            HAL_IWDG_Refresh(&hiwdg);
        }
        else {
            if (!faultLoggedOnce) {
                faultLoggedOnce = true;

                /* Save watchdog recovery record to .noinit (survives reset) */
                TickType_t now = xTaskGetTickCount();
                watchdog_record.magic            = WDG_MAGIC;
                watchdog_record.version          = WDG_VER;
                watchdog_record.fault_task_id    = (uint32_t)hm.getFaultTaskId();
                watchdog_record.fault_latch_tick = now;
                watchdog_record.feed_stop_tick   = now;  /* same cycle as latch */
                /* boot_count is managed in check_fault_on_boot(), not here */

                UartLogger::getInstance().log(
                    "[HM] fault active - stopping IWDG feed, reset imminent\r\n");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
