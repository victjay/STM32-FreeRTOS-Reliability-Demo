/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f4xx_it.h
  * @brief   This file contains the headers of the interrupt handlers.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __STM32F4xx_IT_H
#define __STM32F4xx_IT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
/* ===== FaultRecord: HardFault structure ===== */
#define FAULT_MAGIC  0xDEADBEEFU
#define FAULT_VER    1U

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t bfar;
    uint32_t mmfar;
    uint32_t pc;
    uint32_t msp;
    uint32_t psp;
    uint32_t lr;
} FaultRecord;

extern FaultRecord fault_record;
/* ===== FaultRecord end ===== */

//    WatchdogRecord v1
///* ===== WatchdogRecord: IWDG recovery evidence (Phase 4.3) ===== */
//#define WDG_MAGIC  0x1DDC0DE5U        /* IWDG recovery record magic */
//#define WDG_VER    1U
//#define WDG_BOOT_MAGIC  0xB007C0DEU   /* separate magic for boot_count init */
//typedef struct {
//    uint32_t magic;
//    uint32_t version;
//    uint32_t fault_task_id;    /* HM_TaskId that triggered the fault */
//    uint32_t fault_latch_tick; /* tick when HealthMonitor latched fault */
//    uint32_t feed_stop_tick;   /* tick when IWDG feed stopped (same cycle) */
//    uint32_t boot_count;       /* incremented every boot, persists across reset */
//    uint32_t boot_magic;       /* validates boot_count across power cycles */
//} WatchdogRecord;
//extern WatchdogRecord watchdog_record;
///* ===== WatchdogRecord end ===== */

//    WatchdogRecord v2
/* ===== WatchdogRecord: IWDG recovery evidence (Phase 4.3, extended 6A-2) ===== */
#define WDG_MAGIC  0x1DDC0DE5U        /* IWDG recovery record magic */
#define WDG_VER    2U                 /* v2: added RX-DMA escalation fields */
#define WDG_BOOT_MAGIC  0xB007C0DEU   /* separate magic for boot_count init */

/* fault_source values (WDG_VER >= 2) */
#define WDG_SRC_TASK_HEARTBEAT  0U    /* HealthMonitor task timeout (Phase 4) */
#define WDG_SRC_UART_DMA_RX     1U    /* UART/DMA RX recovery escalation (6A-2) */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t fault_task_id;    /* HM_TaskId that triggered the fault (heartbeat src) */
    uint32_t fault_latch_tick; /* tick when fault was latched */
    uint32_t feed_stop_tick;   /* tick when IWDG feed stopped (same cycle) */
    uint32_t boot_count;       /* incremented every boot, persists across reset */
    uint32_t boot_magic;       /* validates boot_count across power cycles */
    /* ---- v2 additions (6A-2 UART/DMA RX recovery) ---- */
    uint32_t fault_source;        /* WDG_SRC_* : which subsystem escalated */
    uint32_t rx_consecutive_fail; /* consecutive DMA RX recovery failures at escalation */
    uint32_t rx_last_error_code;  /* last DMA/UART error code observed */
} WatchdogRecord;
extern WatchdogRecord watchdog_record;
/* ===== WatchdogRecord end ===== */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void DebugMon_Handler(void);
void DMA1_Stream5_IRQHandler(void);
void USART2_IRQHandler(void);
void EXTI15_10_IRQHandler(void);
void TIM6_DAC_IRQHandler(void);
/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /* __STM32F4xx_IT_H */
