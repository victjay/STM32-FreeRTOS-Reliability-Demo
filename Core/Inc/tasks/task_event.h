/*
 * task_event.h
 *
 *  Created on: Jun 4, 2026
 *      Author: embershine
 */

#ifndef INC_TASKS_TASK_EVENT_H_
#define INC_TASKS_TASK_EVENT_H_

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Phase 3 binary-semaphore event-signaling demo.
// Separate from the Phase 2A latency queue path (own task, semaphore, counters).
void EventDemo_Init(void);
void Task_Event(void *argument);

// ISR entry counter for observability ONLY (not used for any control logic,
// not a debounce mechanism). Shared between ISR and task, hence volatile.
// raw > event means multiple ISR triggers were coalesced by the binary
// semaphore. This can happen due to button bounce or because the consumer
// had not yet taken the semaphore.
extern volatile uint32_t g_isrRawCount;

#ifdef __cplusplus
}
#endif


#endif /* INC_TASKS_TASK_EVENT_H_ */
