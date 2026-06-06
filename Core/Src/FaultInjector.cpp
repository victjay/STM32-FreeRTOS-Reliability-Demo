/*
 * FaultInjector.cpp
 *
 *  Created on: Jun 5, 2026
 *      Author: embershine
 */

#include "FaultInjector.hpp"
#include "UartLogger.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>
#include "queue.h"
#include "uart_dma_rx.h"

/* ACQ task handle — declared in main.cpp */
extern TaskHandle_t hAcquisition;

/* UART handle — declared in usart.c */
extern UART_HandleTypeDef huart2;

/* g_malloc_failed_hook_count defined in freertos.c */
extern "C" volatile uint32_t g_malloc_failed_hook_count;

#define FI_CMD_BUF_SIZE  32

/* ------------------------------------------------------------------ */
/* Command dispatch                                                    */
/* ------------------------------------------------------------------ */

static void fi_dispatch(const char *cmd)
{
    if (strcmp(cmd, "TASK_SUSPEND") == 0) {
        UartLogger::getInstance().log(
            "[FI] TASK_SUSPEND: suspending ACQ externally\r\n");
        /* Externally suspend ACQ — heartbeat stops.
         * HealthMonitor will detect timeout after 1500ms. */
        vTaskSuspend(hAcquisition);
    }
    else if (strcmp(cmd, "HEAP_STRESS") == 0) {
        UartLogger::getInstance().log(
            "[FI] HEAP_STRESS: exhausting heap via pvPortMalloc loop\r\n");

        uint32_t before = g_malloc_failed_hook_count;
        while (pvPortMalloc(64) != NULL) { /* exhaust heap */ }

        char buf[64];
        snprintf(buf, sizeof(buf),
                 "[FI] HEAP_STRESS: done hook_count=%lu\r\n",
                 (unsigned long)g_malloc_failed_hook_count);
        UartLogger::getInstance().log(buf);

        if (g_malloc_failed_hook_count > before) {
            UartLogger::getInstance().log(
                "[FI] HEAP_STRESS: malloc failed hook confirmed\r\n");
        }
    }
    else if (strcmp(cmd, "RESET") == 0) {
        UartLogger::getInstance().log("[FI] RESET: NVIC_SystemReset\r\n");
        /* Allow UART TX to complete before reset */
        vTaskDelay(pdMS_TO_TICKS(50));
        NVIC_SystemReset();
    }
    else {
        char buf[48];
        snprintf(buf, sizeof(buf), "[FI] unknown cmd: %s\r\n", cmd);
        UartLogger::getInstance().log(buf);
    }
}

/* ------------------------------------------------------------------ */
/* Task_FaultInjector                                                  */
/* ------------------------------------------------------------------ */

/**
 * Priority: 2
 *   Lower than HealthMonitor (5) and ACQ (3).
 *   Does not interfere with normal scheduling.
 *
 * Stack: 256 words
 *
 * UART polling: HAL_UART_Receive with 100ms timeout per byte.
 * Command format: ASCII string terminated by '\n'.
 * Max command length: FI_CMD_BUF_SIZE - 1 bytes.
 */
//extern "C" void Task_FaultInjector(void *argument)
//{
//    (void)argument;
//
//    char    buf[FI_CMD_BUF_SIZE];
//    uint8_t byte;
//    uint8_t idx = 0;
//
//    UartLogger::getInstance().log("[FI] FaultInjector started\r\n");
//
//    for (;;) {
//        HAL_StatusTypeDef status =
//            HAL_UART_Receive(&huart2, &byte, 1, 100);
//
//        if (status == HAL_OK) {
//            if (byte == '\n' || byte == '\r') {
//                if (idx > 0) {
//                    buf[idx] = '\0';
//                    fi_dispatch(buf);
//                    idx = 0;
//                }
//            }
//            else {
//                if (idx < FI_CMD_BUF_SIZE - 1) {
//                    buf[idx++] = (char)byte;
//                } else {
//                    /* Buffer overflow — discard and reset */
//                    UartLogger::getInstance().log(
//                        "[FI] cmd buffer overflow, discarding\r\n");
//                    idx = 0;
//                }
//            }
//        }
//        /* HAL_TIMEOUT is expected every 100ms — no action needed */ // <-fail
//        if (status == HAL_TIMEOUT) {
//            vTaskDelay(pdMS_TO_TICKS(10));  /* yield to lower priority tasks */
//        }
//    }
//}

extern "C" void Task_FaultInjector(void *argument)
{
    (void)argument;

    char buf[FI_CMD_BUF_SIZE];

    UartLogger::getInstance().log("[FI] FaultInjector started\r\n");

#ifdef FI_RX_DMA
    /* ---- Phase 6A-1: DMA circular + IDLE line ---- */
    UartLogger::getInstance().log("[FI] RX mode: DMA + IDLE\r\n");

    /* Start DMA RX now that this consumer task is alive.
     * The command queue was created before the scheduler started. */
    uart_dma_rx_start();

    /* TEMP DEBUG */
    {
        char dbg[100];
        snprintf(dbg, sizeof(dbg),
                 "[FI] st=%lu errcode=0x%lX RxState=0x%lX hdmarx=%p NDTR=%lu\r\n",
                 (unsigned long)uart_dma_rx_dbg_status(),
                 (unsigned long)uart_dma_rx_dbg_errorcode(),
                 (unsigned long)uart_dma_rx_dbg_rxstate(),
                 uart_dma_rx_dbg_hdmarx(),
                 (unsigned long)uart_dma_rx_dbg_ndtr());
        UartLogger::getInstance().log(dbg);
    }
    QueueHandle_t cmdQueue = uart_dma_rx_get_queue();

    UartCmdFrame_t frame;

    for (;;) {
        if (xQueueReceive(cmdQueue, &frame, portMAX_DELAY) == pdTRUE) {

        	 /* TEMP DEBUG — remove after diagnosis */
        	    {
        	        char dbg[48];
        	        snprintf(dbg, sizeof(dbg), "[FI] frame.len=%u data0=%02X\r\n",
        	                 (unsigned)frame.len,
        	                 (frame.len > 0) ? frame.data[0] : 0);
        	        UartLogger::getInstance().log(dbg);
        	    }

            /* IDLE delimits the frame; trim trailing CR/LF/whitespace so
             * existing string commands still match in fi_dispatch(). */
            uint16_t n = frame.len;
            while (n > 0u &&
                   (frame.data[n - 1] == '\r' ||
                    frame.data[n - 1] == '\n' ||
                    frame.data[n - 1] == ' '  ||
                    frame.data[n - 1] == '\t')) {
                n--;
            }

            if (n == 0u) {
                continue;   /* empty frame (e.g. lone CRLF) — ignore */
            }
            if (n > (FI_CMD_BUF_SIZE - 1)) {
                UartLogger::getInstance().log("[FI] cmd too long, discarding\r\n");
                continue;
            }

            memcpy(buf, frame.data, n);
            buf[n] = '\0';

            fi_dispatch(buf);   /* unchanged dispatch logic */
        }
    }
#else
    /* ---- Legacy baseline: HAL_UART_Receive polling (byte-loss prone) ---- */
    UartLogger::getInstance().log("[FI] RX mode: polling (baseline)\r\n");

    uint8_t byte;
    uint8_t idx = 0;

    for (;;) {
        HAL_StatusTypeDef status = HAL_UART_Receive(&huart2, &byte, 1, 100);

        if (status == HAL_OK) {
            if (byte == '\n' || byte == '\r') {
                if (idx > 0) {
                    buf[idx] = '\0';
                    fi_dispatch(buf);
                    idx = 0;
                }
            } else {
                if (idx < FI_CMD_BUF_SIZE - 1) {
                    buf[idx++] = (char)byte;
                } else {
                    UartLogger::getInstance().log("[FI] cmd buffer overflow, discarding\r\n");
                    idx = 0;
                }
            }
        }
        if (status == HAL_TIMEOUT) {
            vTaskDelay(pdMS_TO_TICKS(10));  /* yield to lower priority tasks */
        }
    }
#endif
}
