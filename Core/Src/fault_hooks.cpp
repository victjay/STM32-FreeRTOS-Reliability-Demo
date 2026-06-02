/*
 * fault_hooks.cpp
 *
 *  Created on: Jun 2, 2026
 *      Author: embershine
 */
#include "FreeRTOS.h"
#include "task.h"
#include "usart.h"       // huart2 선언 위치 확인 필요
#include <string.h>      // strlen
#include <stdio.h>   // snprintf

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask,
                                               char *pcTaskName)
{
    /* pcTaskName: overflow 발생 task 이름 */
    char buf[64];
    snprintf(buf, sizeof(buf),
             "[FAULT] STACK OVERFLOW task=%s !!!\r\n", pcTaskName);

    ///* UartLogger 대신 직접 HAL — hook은 scheduler 중단 상태일 수 있음 */
    //HAL_UART_Transmit(&huart2, (uint8_t*)buf, strlen(buf), 1000);
    //for (;;) {} /* 의도적 halt */

    /* UART 전송 — 여러 번 시도 */
    for (int i = 0; i < 3; i++) {
        HAL_UART_Transmit(&huart2, (uint8_t*)buf, strlen(buf), 5000);
    }

    /* LED 빠른 점멸 — UART 안 나와도 시각적 확인 */
    while (1) {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
        HAL_Delay(100);
    }


}
