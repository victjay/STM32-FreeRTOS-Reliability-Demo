/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f4xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usart.h"
#include <string.h>
#include "latency_queue.h"
#include "semphr.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
//
// --- FaultRecord: .noinit section, soft-reset retained ---
__attribute__((section(".noinit")))
FaultRecord fault_record;

//extern QueueHandle_t  xLatencyQueue;       // Phase 2A latency path (existing)
extern SemaphoreHandle_t g_buttonEventSem; // Phase 3 event path
extern volatile uint32_t g_isrRawCount;    // observability-only ISR entry count

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern TIM_HandleTypeDef htim6;

/* USER CODE BEGIN EV */
extern volatile uint32_t isr_seq;
/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
__attribute__((naked)) void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */
    __asm volatile(
        "mrs r0, msp        \n"
        "mrs r1, psp        \n"
        "mov r2, lr         \n"   // EXC_RETURN
        "b   HardFault_Minimal \n"
    );
  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32F4xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32f4xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles EXTI line[15:10] interrupts.
  */
void EXTI15_10_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI15_10_IRQn 0 */

  // Before:
//  uint32_t capture = DWT->CYCCNT;  // most firstly capture
//
//  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
//
//  if (xLatencyQueue != NULL) {
//      xQueueSendFromISR(xLatencyQueue, &capture, &xHigherPriorityTaskWoken);
//  }
  // After:
  g_isrRawCount++;                 // observability only: count every ISR entry
  uint32_t capture = DWT->CYCCNT;  // most firstly capture
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  // Phase 2A: latency queue path (NULL-checked)
  if (xLatencyQueue != NULL) {
      xQueueSendFromISR(xLatencyQueue, &capture, &xHigherPriorityTaskWoken);
  }
  // Phase 3: binary semaphore event path (NULL-checked, separate consumer)
  if (g_buttonEventSem != NULL) {
      xSemaphoreGiveFromISR(g_buttonEventSem, &xHigherPriorityTaskWoken);
  }
  /* USER CODE END EXTI15_10_IRQn 0 */

  HAL_GPIO_EXTI_IRQHandler(B1_Pin);
  /* USER CODE BEGIN EXTI15_10_IRQn 1 */
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  /* USER CODE END EXTI15_10_IRQn 1 */
}

/**
  * @brief This function handles TIM6 global interrupt and DAC1, DAC2 underrun error interrupts.
  */
void TIM6_DAC_IRQHandler(void)
{
  /* USER CODE BEGIN TIM6_DAC_IRQn 0 */

  /* USER CODE END TIM6_DAC_IRQn 0 */
  HAL_TIM_IRQHandler(&htim6);
  /* USER CODE BEGIN TIM6_DAC_IRQn 1 */

  /* USER CODE END TIM6_DAC_IRQn 1 */
}

/* USER CODE BEGIN 1 */

// --- C fault handler: MSP 기반, HAL/RTOS API 없음 ---
__attribute__((noreturn, noinline))
void HardFault_Minimal(uint32_t msp, uint32_t psp, uint32_t lr)
{
    // PC 추출: EXC_RETURN bit2 = 0이면 MSP에 frame, 1이면 PSP에 frame
    uint32_t pc = 0xFFFFFFFFU;
    if ((lr & 0x4U) == 0U) {
        // MSP frame: [sp+24] = PC
        uint32_t *frame = (uint32_t *)msp;
        pc = frame[6];
    }
    // PSP가 손상된 경우 PSP frame 접근 생략 → pc = 0xFFFFFFFF 유지

    fault_record.magic   = FAULT_MAGIC;
    fault_record.version = FAULT_VER;
    fault_record.cfsr    = SCB->CFSR;
    fault_record.hfsr    = SCB->HFSR;
    fault_record.pc      = pc;
    fault_record.msp     = msp;
    fault_record.psp     = psp;
    fault_record.lr      = lr;

    // USART2 register direct polling (HAL 없이)
    static const char msg[] = "[FAULT] HardFault — resetting\r\n";
    for (uint32_t i = 0U; i < sizeof(msg) - 1U; i++) {
        while ((USART2->SR & USART_SR_TXE) == 0U) {}
        USART2->DR = (uint8_t)msg[i];
    }
    while ((USART2->SR & USART_SR_TC) == 0U) {}

    NVIC_SystemReset();
}
/* USER CODE END 1 */
