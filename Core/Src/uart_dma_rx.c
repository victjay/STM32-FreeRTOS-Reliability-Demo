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

extern UART_HandleTypeDef huart2;

static QueueHandle_t s_cmdQueue = NULL;
static uint8_t       s_rx_buf[UART_DMA_RX_BUF_SIZE];
static uint16_t      s_read_pos;   /* circular read index (last consumed) */

static volatile HAL_StatusTypeDef s_start_status = HAL_ERROR;
static volatile uint32_t          s_uart_errorcode;
static volatile uint32_t 	      s_rxstate_before;

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
