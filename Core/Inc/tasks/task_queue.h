/*
 * task_queue.h
 *
 *  Created on: Jun 4, 2026
 *      Author: embershine
 */

#ifndef INC_TASKS_TASK_QUEUE_H_
#define INC_TASKS_TASK_QUEUE_H_

#ifdef __cplusplus
extern "C" {
#endif

// Phase 3 queue-full DROP-NEW demo.
// Producer (fast) outpaces Consumer (slow) so the queue fills and new items
// are dropped. Fully separate from the Phase 2A latency queue path.
void QueueDemo_Init(void);
void Task_QueueProducer(void *argument);
void Task_QueueConsumer(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* INC_TASKS_TASK_QUEUE_H_ */
