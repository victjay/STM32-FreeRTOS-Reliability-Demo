/*
 * HealthMonitor_c.h
 *
 *  Created on: Jun 8, 2026
 *      Author: embershine
 */

#ifndef INC_HEALTHMONITOR_C_H_
#define INC_HEALTHMONITOR_C_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Report an external (non-task) fault, e.g. UART/DMA RX escalation.
 * TASK CONTEXT ONLY. Do NOT call from ISR or DMA callback.
 * First-fault-wins: ignored if a fault is already latched. */
void HealthMonitor_ReportExternalFault(uint32_t source,
                                       uint32_t errorCode,
                                       uint32_t consecutiveFail);

#ifdef __cplusplus
}
#endif

#endif /* INC_HEALTHMONITOR_C_H_ */
