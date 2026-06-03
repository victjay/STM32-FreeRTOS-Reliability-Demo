/*
 * LatencyMeter.cpp
 *
 *  Created on: Jun 3, 2026
 *      Author: embershine
 */

#include "LatencyMeter.hpp"
#include "tasks/task_latency.h"
#include <stdio.h>

float LatencyMeter::cycleToUs(uint32_t delta_cycles, uint32_t core_clock_hz)
{
    return (float)delta_cycles * 1000000.0f / (float)core_clock_hz;
}

void LatencyMeter::formatCsvLine(const LatencySample& s,
                                  uint32_t core_clock_hz,
                                  char* buf, size_t buf_len)
{
    uint32_t delta = s.task_cycle - s.isr_cycle;
    float us = cycleToUs(delta, core_clock_hz);
    snprintf(buf, buf_len, "%lu,%lu,%lu,%lu,%.3f\r\n",
             s.seq, s.isr_cycle, s.task_cycle, delta, us);
}
