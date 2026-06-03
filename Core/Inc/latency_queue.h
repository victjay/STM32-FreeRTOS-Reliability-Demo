/*
 * latency_queue.h
 *
 *  Created on: Jun 3, 2026
 *      Author: embershine
 */

#ifndef INC_LATENCY_QUEUE_H_
#define INC_LATENCY_QUEUE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "queue.h"

extern QueueHandle_t xLatencyQueue;

#ifdef __cplusplus
}
#endif

#endif /* INC_LATENCY_QUEUE_H_ */
