/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "cmsis_os.h"
#include "UartLogger.hpp"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "tasks/task_acquisition.h"
#include "tasks/task_processing.h"
#include "tasks/task_monitor.h"

#include <cstring>    // strlen
#include <cstdio>     // snprintf
#include "stm32f4xx_it.h"  // FaultRecord, FAULT_MAGIC, fault_record, iwdg time

#include "dwt_init.h"
#include "latency_queue.h"
#include "LatencyMeter.hpp"

#include "tasks/task_latency.h" // phase 2a
#include "gpio.h"
#include "tasks/task_pi.h"  // phase 2b
#include "semphr.h"

#include "tasks/task_event.h"  // phase 3 binary sema
#include "tasks/task_queue.h"  // phase 3 queue-full demo

#include "HealthMonitor.hpp"   // phase 4
#include "FaultInjector.hpp"
#include "iwdg.h"          	// phase 4.3 — IWDG

#include "uart_dma_rx.h"	// phase 6 dma
#include "dma.h"   /* phase 6: MX_DMA_Init prototype */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* Phase demo enable flags
 * Set to 0 to disable task creation for Phase 4 bring-up.
 * Does NOT delete code — set back to 1 to restore previous phase demos. */
#define ENABLE_PHASE2_LATENCY_TASK  0   /* LAT task only (LatencyMeter class kept) */
#define ENABLE_PHASE3_PI_DEMO       0   /* PI_CTRL / PI_HIGH / PI_MED / PI_LOW     */
#define ENABLE_PHASE3_QUEUE_DEMO    0   /* QPROD / QCONS                           */
#define ENABLE_PHASE3_EVENT_DEMO    0   /* EVENT                                   */
#define ENABLE_PHASE4_HEALTH_DEMO   1   /* HealthMonitor / FaultInjector           */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
//UART_HandleTypeDef huart2;

/* Definitions for defaultTask */
//osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE BEGIN PV */
extern osThreadId_t defaultTaskHandle;

QueueHandle_t xLatencyQueue = NULL;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void StartDefaultTask(void *argument);

/* USER CODE BEGIN PFP */
TaskHandle_t hAcquisition = NULL;
TaskHandle_t hProcessing  = NULL;
TaskHandle_t hMonitor     = NULL;
extern "C" void check_fault_on_boot(void);

TaskHandle_t hPI_Controller = NULL;
TaskHandle_t hPI_Low        = NULL;
TaskHandle_t hPI_Medium     = NULL;
TaskHandle_t hPI_High       = NULL;

extern "C" void PI_Init(void);

TaskHandle_t hEvent = NULL;
extern "C" void EventDemo_Init(void);

TaskHandle_t hQueueProducer = NULL;
TaskHandle_t hQueueConsumer = NULL;
extern "C" void QueueDemo_Init(void);

TaskHandle_t hHealthMonitor = NULL;
extern "C" void Task_HealthMonitor(void *argument);

TaskHandle_t hFaultInjector  = NULL;
extern "C" void Task_FaultInjector(void *argument);

extern "C" WatchdogRecord watchdog_record;

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  xTaskCreate(Task_Acquisition, "ACQ",  512, NULL, 3, &hAcquisition);
  xTaskCreate(Task_Processing,  "PROC", 256, NULL, 2, &hProcessing);
  xTaskCreate(Task_Monitor,     "MON",  512, NULL, 1, &hMonitor);



//  /* BEFORE */
//  PI_Init();
//  xTaskCreate(Task_PI_Controller, "PI_CTRL", 256, NULL, 4, &hPI_Controller);
//  xTaskCreate(Task_PI_High,       "PI_HIGH", 256, NULL, 3, &hPI_High);
//  xTaskCreate(Task_PI_Medium,     "PI_MED",  256, NULL, 2, &hPI_Medium);
//  xTaskCreate(Task_PI_Low,        "PI_LOW",  256, NULL, 1, &hPI_Low);

  /* AFTER */
#if ENABLE_PHASE3_PI_DEMO
  PI_Init();
  xTaskCreate(Task_PI_Controller, "PI_CTRL", 256, NULL, 4, &hPI_Controller);
  xTaskCreate(Task_PI_High,       "PI_HIGH", 256, NULL, 3, &hPI_High);
  xTaskCreate(Task_PI_Medium,     "PI_MED",  256, NULL, 2, &hPI_Medium);
  xTaskCreate(Task_PI_Low,        "PI_LOW",  256, NULL, 1, &hPI_Low);
#endif



//  /* BEFORE */
//  EventDemo_Init();
//  xTaskCreate(Task_Event, "EVENT", 256, NULL, 2, &hEvent);

  /* AFTER */
#if ENABLE_PHASE3_EVENT_DEMO
  EventDemo_Init();
  xTaskCreate(Task_Event, "EVENT", 256, NULL, 2, &hEvent);
#endif

//  /* BEFORE */
//  QueueDemo_Init();
//  BaseType_t qpOk = xTaskCreate(Task_QueueProducer, "QPROD", 128, NULL, 2, &hQueueProducer);
//  BaseType_t qcOk = xTaskCreate(Task_QueueConsumer, "QCONS", 256, NULL, 1, &hQueueConsumer);
//  if (qpOk != pdPASS || qcOk != pdPASS) {
//      UartLogger::getInstance().log("[QUEUE] ERROR: xTaskCreate failed\r\n");
//  }

  /* AFTER */
#if ENABLE_PHASE3_QUEUE_DEMO
  QueueDemo_Init();
  BaseType_t qpOk = xTaskCreate(Task_QueueProducer, "QPROD", 128, NULL, 2, &hQueueProducer);
  BaseType_t qcOk = xTaskCreate(Task_QueueConsumer, "QCONS", 256, NULL, 1, &hQueueConsumer);
  if (qpOk != pdPASS || qcOk != pdPASS) {
      UartLogger::getInstance().log("[QUEUE] ERROR: xTaskCreate failed\r\n");
  }
#endif

  /* BEFORE: 해당 위치에 아무것도 없음 */

  /* AFTER */
#if ENABLE_PHASE4_HEALTH_DEMO
  /* Phase 4: HealthMonitor and FaultInjector tasks — added after heap freed */
  xTaskCreate(Task_HealthMonitor, "HM", 256, NULL, 5, &hHealthMonitor);
  xTaskCreate(Task_FaultInjector,  "FI", 256, NULL, 2, &hFaultInjector);
#endif


  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  DWT_Init();
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();              /* phase 6: must run before USART2 (DMA link) */
  MX_USART2_UART_Init();
  MX_IWDG_Init();   /* phase 4.3 — start IWDG (counter begins immediately) */
  /* USER CODE BEGIN 2 */
  check_fault_on_boot();

  char buf[40];
  snprintf(buf, sizeof(buf), "[SYS] SystemCoreClock=%lu\r\n", SystemCoreClock);
  UartLogger::getInstance().log(buf);

#ifdef FI_RX_DMA
  uart_dma_rx_init_queue();   /* create cmd queue before scheduler/ISR */
#endif

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  ///* BEFORE */
  //xTaskCreate(task_latency, "LAT", 256, NULL, 3, NULL);

  /* AFTER */
  #if ENABLE_PHASE2_LATENCY_TASK
    xTaskCreate(task_latency, "LAT", 256, NULL, 3, NULL);
  #endif

  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
//static void MX_USART2_UART_Init(void)
//{
//
//  /* USER CODE BEGIN USART2_Init 0 */
//
//  /* USER CODE END USART2_Init 0 */
//
//  /* USER CODE BEGIN USART2_Init 1 */
//
//  /* USER CODE END USART2_Init 1 */
//  huart2.Instance = USART2;
//  huart2.Init.BaudRate = 115200;
//  huart2.Init.WordLength = UART_WORDLENGTH_8B;
//  huart2.Init.StopBits = UART_STOPBITS_1;
//  huart2.Init.Parity = UART_PARITY_NONE;
//  huart2.Init.Mode = UART_MODE_TX_RX;
//  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
//  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
//  if (HAL_UART_Init(&huart2) != HAL_OK)
//  {
//Error_Handler();
//  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

//}


/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  * gpio.c
  */


/* USER CODE BEGIN 4 */
//extern "C" void check_fault_on_boot(void)
//{
//    if (fault_record.magic == FAULT_MAGIC) {
//        char buf[128];
//        snprintf(buf, sizeof(buf),
//            "[BOOT] PrevFault CFSR=0x%08lX HFSR=0x%08lX "
//            "PC=0x%08lX MSP=0x%08lX PSP=0x%08lX LR=0x%08lX\r\n",
//            fault_record.cfsr, fault_record.hfsr,
//            fault_record.pc,   fault_record.msp,
//            fault_record.psp,  fault_record.lr);
//        HAL_UART_Transmit(&huart2, (uint8_t*)buf, strlen(buf), 1000);
//        fault_record.magic = 0U;
//    }
//}

extern "C" void check_fault_on_boot(void)
{
    /* Log reset cause from RCC CSR flags */
    uint32_t csr = RCC->CSR;

    if (csr & RCC_CSR_PINRSTF)  {
        UartLogger::getInstance().log("[BOOT] reset cause: PIN reset (button) ==============================>\r\n");
    }
    if (csr & RCC_CSR_IWDGRSTF) {
        UartLogger::getInstance().log("[BOOT] reset cause: IWDG watchdog  ==============================>\r\n");
    }
    if (csr & RCC_CSR_SFTRSTF)  {
        UartLogger::getInstance().log("[BOOT] reset cause: software reset  ==============================>\r\n");
    }
    if (csr & RCC_CSR_PORRSTF)  {
        UartLogger::getInstance().log("[BOOT] reset cause: power-on reset  ==============================>\r\n");
    }

    /* Clear reset flags */
    __HAL_RCC_CLEAR_RESET_FLAGS();

    /* HardFault record check */
    if (fault_record.magic == FAULT_MAGIC) {
        char buf[128];
        snprintf(buf, sizeof(buf),
            "[BOOT] PrevFault CFSR=0x%08lX HFSR=0x%08lX "
            "PC=0x%08lX MSP=0x%08lX PSP=0x%08lX LR=0x%08lX\r\n",
            fault_record.cfsr, fault_record.hfsr,
            fault_record.pc,   fault_record.msp,
            fault_record.psp,  fault_record.lr);
        HAL_UART_Transmit(&huart2, (uint8_t*)buf, strlen(buf), 1000);
        fault_record.magic = 0U;
    }

    /* Watchdog recovery record (Phase 4.3) */
    if (watchdog_record.boot_magic != WDG_BOOT_MAGIC) {
        /* First boot after power-on: .noinit holds garbage, initialise */
        watchdog_record.boot_magic = WDG_BOOT_MAGIC;
        watchdog_record.boot_count = 0U;
    }
    watchdog_record.boot_count++;   /* always increment on boot */

//    WatchdogRecord v1
//    if (watchdog_record.magic == WDG_MAGIC) {
//        char wbuf[200];
//        snprintf(wbuf, sizeof(wbuf),
//            "[BOOT] WdgRecord task_id=%lu latch_tick=%lu feed_stop_tick=%lu "
//            "boot_count=%lu iwdg_timeout~3000ms(2040-5650ms range)\r\n",
//            (unsigned long)watchdog_record.fault_task_id,
//            (unsigned long)watchdog_record.fault_latch_tick,
//            (unsigned long)watchdog_record.feed_stop_tick,
//            (unsigned long)watchdog_record.boot_count);
//        HAL_UART_Transmit(&huart2, (uint8_t*)wbuf, strlen(wbuf), 1000);
//        watchdog_record.magic = 0U;   /* clear after reading */
//    }

    //    WatchdogRecord v2
    if (watchdog_record.magic == WDG_MAGIC) {
            char wbuf[256];

            if (watchdog_record.version == WDG_VER) {
                /* v2 record: safe to read v2 escalation fields */
                snprintf(wbuf, sizeof(wbuf),
                    "[BOOT] WdgRecord v%lu src=%lu task_id=%lu latch_tick=%lu "
                    "feed_stop_tick=%lu rx_fail=%lu rx_err=0x%lX "
                    "boot_count=%lu iwdg_timeout~3000ms(2040-5650ms range)\r\n",
                    (unsigned long)watchdog_record.version,
                    (unsigned long)watchdog_record.fault_source,
                    (unsigned long)watchdog_record.fault_task_id,
                    (unsigned long)watchdog_record.fault_latch_tick,
                    (unsigned long)watchdog_record.feed_stop_tick,
                    (unsigned long)watchdog_record.rx_consecutive_fail,
                    (unsigned long)watchdog_record.rx_last_error_code,
                    (unsigned long)watchdog_record.boot_count);
            } else {
                /* magic OK but version mismatch (e.g. stale v1 after reflash):
                 * legacy layout — do NOT read v2 fields */
                snprintf(wbuf, sizeof(wbuf),
                    "[BOOT] WdgRecord (legacy v%lu) task_id=%lu latch_tick=%lu "
                    "feed_stop_tick=%lu boot_count=%lu\r\n",
                    (unsigned long)watchdog_record.version,
                    (unsigned long)watchdog_record.fault_task_id,
                    (unsigned long)watchdog_record.fault_latch_tick,
                    (unsigned long)watchdog_record.feed_stop_tick,
                    (unsigned long)watchdog_record.boot_count);
            }

            HAL_UART_Transmit(&huart2, (uint8_t*)wbuf, strlen(wbuf), 1000);
            watchdog_record.magic = 0U;   /* clear after reading */
        }


}
/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  /* Infinite loop */
  for(;;)
  {
      //osDelay(1);
	  HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
	  UartLogger::getInstance().log("Hello FreeRTOS\r\n");
	  osDelay(500);
  }
  /* USER CODE END 5 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
