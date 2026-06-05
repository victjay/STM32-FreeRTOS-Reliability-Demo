/*
 * HealthMonitor.hpp
 *
 *  Created on: Jun 4, 2026
 *      Author: embershine
 */

#ifndef INC_HEALTHMONITOR_HPP_
#define INC_HEALTHMONITOR_HPP_

#pragma once
/**
 * HealthMonitor.hpp
 *
 * Monitors task liveness via periodic heartbeats.
 * Each monitored task calls heartbeat() after completing its work.
 * HealthMonitor::check() detects timeout and latches a fault flag.
 *
 * Priority note:
 *   Task_HealthMonitor runs at priority 5.
 *   CPU hog detection is possible for tasks at priority <= 4.
 *   Higher-priority tasks or interrupt storms are NOT detectable
 *   by this monitor — known limitation.
 *
 * Thread safety:
 *   heartbeat(), check(), isFaultDetected(), getFaultTaskId()
 *   access shared state via taskENTER_CRITICAL / taskEXIT_CRITICAL.
 */

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>
#include <stdbool.h>

#define HEALTH_MONITOR_MAX_TASKS  4

typedef enum {
    HM_TASK_ACQ  = 0,
    HM_TASK_PROC = 1,
} HM_TaskId;

struct HM_Entry {
    bool       registered;
    TickType_t timeoutTicks;
    TickType_t lastHeartbeat;
    bool       faultLatched;
};

class HealthMonitor {
public:
    static HealthMonitor& getInstance();

    void registerTask(HM_TaskId id, uint32_t timeoutMs);
    void heartbeat(HM_TaskId id);
    void check();

    /* Non-const: accesses shared state under critical section */
    bool isFaultDetected();
    int  getFaultTaskId();

private:
    HealthMonitor();

    HM_Entry entries[HEALTH_MONITOR_MAX_TASKS];
    bool     faultDetected;
};

extern "C" void Task_HealthMonitor(void *argument);



#endif /* INC_HEALTHMONITOR_HPP_ */
