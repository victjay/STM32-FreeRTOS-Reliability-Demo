/*
 * uart_dma_rx.h
 *
 *  Created on: Jun 5, 2026
 *      Author: embershine
 */

#ifndef INC_UART_DMA_RX_H_
#define INC_UART_DMA_RX_H_

#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"

/* Phase 6A-1 RX path selector (FaultInjector).
 *   defined   -> DMA circular + IDLE line (default)
 *   undefined -> legacy HAL_UART_Receive polling (byte-loss baseline) */
#define FI_RX_DMA

#ifdef __cplusplus
extern "C" {
#endif

/* TEMP DEBUG */
uint32_t uart_dma_rx_dbg_status(void);
uint32_t uart_dma_rx_dbg_errorcode(void);
uint32_t uart_dma_rx_dbg_rxstate(void);


/* TEMP DEBUG — remove after diagnosis */
void*    uart_dma_rx_dbg_hdmarx(void);
uint32_t uart_dma_rx_dbg_ndtr(void);

#define UART_DMA_RX_BUF_SIZE 64u

/* One command frame delimited by UART IDLE line. */
typedef struct {
    uint8_t  data[UART_DMA_RX_BUF_SIZE];
    uint16_t len;
} UartCmdFrame_t;

/* Create the command queue. Call BEFORE the scheduler starts so the
 * queue handle exists before the USART2 IDLE ISR can fire. */
void uart_dma_rx_init_queue(void);

/* Start circular DMA reception + enable IDLE interrupt.
 * Call from the consumer (FaultInjector) task after it starts. */
void uart_dma_rx_start(void);

/* Returns the command queue handle (consumer side). */
QueueHandle_t uart_dma_rx_get_queue(void);

/* Called from USART2_IRQHandler on IDLE detection (ISR context). */
void uart_dma_rx_on_idle(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_UART_DMA_RX_H_ */
