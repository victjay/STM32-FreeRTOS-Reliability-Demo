/*
 * uart_dma_rx.c
 *
 *  Created on: Jun 5, 2026
 *      Author: embershine
 */

/* USART2 circular DMA RX with IDLE-line framing.
 * Variable-length command frames are delimited by UART IDLE, not by length.
 * The IDLE ISR computes frame length from DMA NDTR and posts one frame
 * to the command queue. Frame parsing/dispatch is done in task context. */

#include "uart_dma_rx.h"
#include "main.h"            /* huart2 via usart.h chain */
#include "HealthMonitor_C.h"   /* HealthMonitor_ReportExternalFault (task-only) */
#include "stm32f4xx_it.h"      /* WDG_SRC_UART_DMA_RX */
#include <stdbool.h>          /* bool / true / false in C */

extern UART_HandleTypeDef huart2;

static QueueHandle_t s_cmdQueue = NULL;
static uint8_t       s_rx_buf[UART_DMA_RX_BUF_SIZE];
static uint16_t      s_read_pos;   /* circular read index (last consumed) */

static volatile HAL_StatusTypeDef s_start_status = HAL_ERROR;
static volatile uint32_t          s_uart_errorcode;
static volatile uint32_t 	      s_rxstate_before;

/* ---- Phase 6A-2: RX error recovery state ---- */

/* ISR-written (HAL_UART_ErrorCallback): observation + signaling only */
static volatile bool     s_rx_error_pending;   /* ISR sets, service clears */
static volatile uint32_t s_rx_last_error_code; /* ISR captures huart->ErrorCode */
static volatile uint32_t s_rx_error_count;     /* total HW errors (evidence) */

/* Task-only: recovery policy state */
static uint32_t s_consecutive_fail;            /* escalation counter; 0 on normal frame */
static uint32_t s_rx_restart_count;            /* total local restarts (evidence) */

/* ISR-written: application-level, NOT escalated (observation only) */
static volatile uint32_t s_long_frame_count;   /* frame larger than buffer */
static volatile uint32_t s_app_overflow_count; /* queue/app overflow */

#define UART_DMA_RX_FAIL_THRESHOLD  3U          /* consecutive fails → escalation */


void uart_dma_rx_init_queue(void)
{
    /* Depth 8 frames; each frame is one IDLE-delimited command. */
    s_cmdQueue = xQueueCreate(8, sizeof(UartCmdFrame_t));
    s_read_pos = 0u;
}

QueueHandle_t uart_dma_rx_get_queue(void)
{
    return s_cmdQueue;
}

void uart_dma_rx_start(void)
{
    /* Clear any leftover BUSY_RX state before starting circular DMA RX. */
    HAL_UART_AbortReceive(&huart2);

	/* Circular DMA keeps the stream running; HT/TC are ignored,
     * framing is done by IDLE only. */
    s_rxstate_before = (uint32_t)huart2.RxState;   /* BEFORE the call */

    s_start_status = HAL_UART_Receive_DMA(&huart2, s_rx_buf, UART_DMA_RX_BUF_SIZE);
    s_uart_errorcode = huart2.ErrorCode;

//    HAL_UART_Receive_DMA(&huart2, s_rx_buf, UART_DMA_RX_BUF_SIZE);
//    s_uart_errorcode = huart2.ErrorCode;

    s_read_pos = 0u;   /* sync read index to DMA start (NDTR == BUF_SIZE) */

    __HAL_UART_CLEAR_IDLEFLAG(&huart2);
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);
}

/* TEMP DEBUG */
uint32_t uart_dma_rx_dbg_status(void)    { return (uint32_t)s_start_status; }
uint32_t uart_dma_rx_dbg_errorcode(void) { return s_uart_errorcode; }
uint32_t uart_dma_rx_dbg_rxstate(void)   { return (uint32_t)huart2.RxState; }

/* ISR context. IDLE flag is already cleared by the IRQ handler.
 *
 * MVP limitation: circular DMA keeps writing during this copy.
 * At very high throughput the copied region could be overwritten before
 * it is consumed. For production, the ISR should publish buffer indices
 * only and defer copy/parse to task context. Acceptable here because
 * command frames are short and human/script paced. */
void uart_dma_rx_on_idle(void)
{
    if (s_cmdQueue == NULL) {
        return; /* queue not created yet — drop */
    }

    uint16_t dma_pos = UART_DMA_RX_BUF_SIZE
                     - (uint16_t)__HAL_DMA_GET_COUNTER(huart2.hdmarx);

    if (dma_pos == s_read_pos) {
        return; /* nothing new */
    }

    UartCmdFrame_t frame;
    uint16_t n = 0u;

    if (dma_pos > s_read_pos) {
        n = dma_pos - s_read_pos;
        for (uint16_t i = 0u; i < n; i++) {
            frame.data[i] = s_rx_buf[s_read_pos + i];
        }
    } else {
        /* wrapped: read_pos..end then 0..dma_pos */
        uint16_t first = UART_DMA_RX_BUF_SIZE - s_read_pos;
        for (uint16_t i = 0u; i < first; i++) {
            frame.data[n++] = s_rx_buf[s_read_pos + i];
        }
        for (uint16_t i = 0u; i < dma_pos; i++) {
            frame.data[n++] = s_rx_buf[i];
        }
    }
    frame.len  = n;
    s_read_pos = dma_pos;

    /* Observation: frame filled the whole buffer = likely long-frame/flood.
     * NOT escalated (application-level), counted only. */
    if (n >= UART_DMA_RX_BUF_SIZE) {
        s_long_frame_count++;
    }

    BaseType_t hpw = pdFALSE;
    (void)xQueueSendFromISR(s_cmdQueue, &frame, &hpw);
    portYIELD_FROM_ISR(hpw);
}

/* TEMP DEBUG helpers (C-only, no C++ logger). Remove after diagnosis. */
void* uart_dma_rx_dbg_hdmarx(void)
{
    return (void*)huart2.hdmarx;
}

uint32_t uart_dma_rx_dbg_ndtr(void)
{
    return (huart2.hdmarx != NULL)
         ? (uint32_t)__HAL_DMA_GET_COUNTER(huart2.hdmarx)
         : 0xFFFFFFFFu;   /* sentinel: hdmarx is NULL */
}


uint32_t uart_dma_rx_get_error_count(void)        { return s_rx_error_count; }
uint32_t uart_dma_rx_get_restart_count(void)      { return s_rx_restart_count; }
uint32_t uart_dma_rx_get_long_frame_count(void)   { return s_long_frame_count; }
uint32_t uart_dma_rx_get_app_overflow_count(void) { return s_app_overflow_count; }

/* ISR context (called from HAL_UART_IRQHandler error path).
 * MINIMAL ONLY: no log, no restart, no HealthMonitor, no FreeRTOS API. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart != &huart2) {
        return;
    }
    s_rx_last_error_code = huart->ErrorCode;
    s_rx_error_pending   = true;
    s_rx_error_count++;
}

/* Restart circular DMA reception after an error. Returns HAL status.
 * Note: HAL_OK here means "restart issued", NOT "receive path recovered".
 * Recovery is confirmed only when a normal frame is later received. */
static HAL_StatusTypeDef uart_dma_rx_restart(void)
{
    HAL_UART_AbortReceive(&huart2);                       /* clear BUSY/error state */
    __HAL_UART_CLEAR_IDLEFLAG(&huart2);

    HAL_StatusTypeDef st =
        HAL_UART_Receive_DMA(&huart2, s_rx_buf, UART_DMA_RX_BUF_SIZE);

    s_read_pos = 0u;
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);
    return st;
}

/* TASK CONTEXT ONLY. Called periodically by the consumer task. */
void uart_dma_rx_service(void)
{
    bool     pending;
    uint32_t err;

    taskENTER_CRITICAL();
    pending = s_rx_error_pending;
    err     = s_rx_last_error_code;
    s_rx_error_pending = false;
    taskEXIT_CRITICAL();

    if (!pending) {
        return;
    }

    (void)uart_dma_rx_restart();   /* restart issued (recovery not yet confirmed) */
    s_rx_restart_count++;
    s_consecutive_fail++;          /* cleared only on a normal frame (notify_rx_ok) */

    if (s_consecutive_fail >= UART_DMA_RX_FAIL_THRESHOLD) {
        /* repeated local recovery failure → escalate to IWDG path */
        HealthMonitor_ReportExternalFault(WDG_SRC_UART_DMA_RX,
                                          err,
                                          s_consecutive_fail);
    }
}

/* TASK CONTEXT ONLY. A normal frame was received ⇒ recovery confirmed. */
void uart_dma_rx_notify_rx_ok(void)
{
    s_consecutive_fail = 0u;
}


