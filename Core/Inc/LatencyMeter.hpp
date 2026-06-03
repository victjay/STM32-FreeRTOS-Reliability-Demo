/*
 * LatencyMeter.hpp
 *
 *  Created on: Jun 3, 2026
 *      Author: embershine
 */

#ifndef INC_LATENCYMETER_HPP_
#define INC_LATENCYMETER_HPP_

#pragma once
#include <stdint.h>
#include <stdio.h>

struct LatencySample {
    uint32_t isr_cycle;
    uint32_t task_cycle;
    uint32_t seq;
};

class LatencyMeter {
public:
    static float cycleToUs(uint32_t delta_cycles, uint32_t core_clock_hz);
    static void formatCsvLine(const LatencySample& s,
                              uint32_t core_clock_hz,
                              char* buf, size_t buf_len);
};



#endif /* INC_LATENCYMETER_HPP_ */
