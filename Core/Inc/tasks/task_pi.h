/*
 * task_pi.h
 *
 *  Created on: Jun 3, 2026
 *      Author: embershine
 */

#ifndef INC_TASKS_TASK_PI_H_
#define INC_TASKS_TASK_PI_H_

#ifdef __cplusplus
extern "C" {
#endif

void PI_Init(void);
void Task_PI_Controller(void *argument);
void Task_PI_Low(void *argument);
void Task_PI_Medium(void *argument);
void Task_PI_High(void *argument);

#ifdef __cplusplus
}
#endif


#endif /* INC_TASKS_TASK_PI_H_ */
