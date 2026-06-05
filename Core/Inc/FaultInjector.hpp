/*
 * FaultInjector.hpp
 *
 *  Created on: Jun 5, 2026
 *      Author: embershine
 */

#ifndef INC_FAULTINJECTOR_HPP_
#define INC_FAULTINJECTOR_HPP_

/**
 * FaultInjector.hpp
 *
 * Receives fault injection commands via UART polling.
 * Supported commands (terminated by '\n'):
 *   TASK_SUSPEND  - externally suspends ACQ task via vTaskSuspend()
 *                   Expected: ACQ heartbeat stops, HealthMonitor detects timeout
 *   HEAP_STRESS   - exhausts heap via pvPortMalloc() loop
 *                   Expected: malloc failed hook triggered, IWDG fallback
 *   RESET         - immediate system reset via NVIC_SystemReset()
 *
 * CPU_HOG command is reserved for Phase 4 Bonus (not implemented here).
 *
 * UART: polling with 100ms timeout per byte (HAL_UART_Receive).
 * No DMA RX — deferred to Phase 5/Bonus.
 */

#include "FreeRTOS.h"
#include "task.h"

extern "C" void Task_FaultInjector(void *argument);



#endif /* INC_FAULTINJECTOR_HPP_ */
