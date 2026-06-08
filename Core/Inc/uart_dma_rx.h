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

/* ---- Phase 6A-2: RX error recovery ---- */

/* Service the RX recovery policy. TASK CONTEXT ONLY.
 * Checks the error-pending flag (set by HAL_UART_ErrorCallback in ISR),
 * restarts DMA, increments consecutive failure counter, and escalates
 * to HealthMonitor after 3 consecutive failures. */
void uart_dma_rx_service(void);

/* Notify that a normal frame was received (recovery confirmed).
 * TASK CONTEXT ONLY. Clears the consecutive failure counter. */
void uart_dma_rx_notify_rx_ok(void);

/* Observation counters (evidence). */
uint32_t uart_dma_rx_get_error_count(void);
uint32_t uart_dma_rx_get_restart_count(void);
uint32_t uart_dma_rx_get_long_frame_count(void);
uint32_t uart_dma_rx_get_app_overflow_count(void);

/* ---- Phase 6A-2 / step 4: test injection hooks (TASK CONTEXT ONLY) ---- */

/* Inject one RX error into the SAME pending-flag path as the real
 * HAL_UART_ErrorCallback. Does NOT perform recovery itself; the next
 * uart_dma_rx_service() handles it. */
void uart_dma_rx_inject_error(uint32_t errorCode);

/* Inject `count` errors, running the real service path after each one.
 * This drives restart + consecutive_fail + threshold escalation through
 * the actual recovery policy (no counter shortcuts). TASK CONTEXT ONLY. */
void uart_dma_rx_inject_error_repeat(uint32_t errorCode, uint32_t count);

/* Inject one application-level long-frame event (observation only,
 * NOT escalated). */
void uart_dma_rx_inject_long_frame(void);

/* Snapshot of recovery counters for the DMA_RX_STATS command. */
uint32_t uart_dma_rx_get_consecutive_fail(void);   /* current consecutive_fail */

#ifdef __cplusplus
}
#endif

#endif /* INC_UART_DMA_RX_H_ */
