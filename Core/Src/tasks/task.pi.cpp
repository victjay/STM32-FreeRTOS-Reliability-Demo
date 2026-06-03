/*
 * task.pi.cpp
 *
 *  Created on: Jun 3, 2026
 *      Author: embershine
 */

#include "tasks/task_pi.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "UartLogger.hpp"
#include "main.h"       // HAL chain + DWT (core_cm4.h) + SystemCoreClock
#include "dwt_init.h"   // DWT cycle counter, already enabled in Phase 2
#include <stdio.h>

// ---- Lock under test (BEFORE state) ----
// Binary semaphore has NO priority inheritance. This is intentional:
// it is what makes the priority inversion reproducible.
static SemaphoreHandle_t g_piLock = NULL;

// ---- Handshake semaphores (controller <-> worker tasks) ----
static SemaphoreHandle_t sem_start_low         = NULL;  // ctrl -> Low
static SemaphoreHandle_t sem_start_high        = NULL;  // ctrl -> High
static SemaphoreHandle_t sem_start_med         = NULL;  // ctrl -> Medium
static SemaphoreHandle_t sem_low_has_lock      = NULL;  // Low  -> ctrl
static SemaphoreHandle_t sem_high_about_to_take = NULL; // High -> ctrl

// CPU-bound busy-wait that stays preemptible by higher-priority tasks.
// Uses the DWT cycle counter so duration is tied to real cycles, not an
// un-calibrated nop count. NOTE: if an ISR preempts this loop, the ISR
// cycles are included in the measured window (documented as a limitation).
static void busy_cycles(uint32_t cycles)
{
    uint32_t start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - start) < cycles) {
        __NOP();
    }
}

// Convert milliseconds to core cycles at the runtime clock (not hardcoded).
static uint32_t ms_to_cycles(uint32_t ms)
{
    return (uint32_t)((uint64_t)SystemCoreClock * ms / 1000U);
}

void PI_Init(void)
{
    // Lock under test: created then made available once.
    g_piLock = xSemaphoreCreateBinary();
    xSemaphoreGive(g_piLock);

    // Handshake semaphores start empty, so the first take() blocks until give().
    sem_start_low          = xSemaphoreCreateBinary();
    sem_start_high         = xSemaphoreCreateBinary();
    sem_start_med          = xSemaphoreCreateBinary();
    sem_low_has_lock       = xSemaphoreCreateBinary();
    sem_high_about_to_take = xSemaphoreCreateBinary();
}

// Controller (priority 4): enforces the scenario order, then blocks forever.
void Task_PI_Controller(void *argument)
{
    UartLogger::getInstance().log("[PI_CTRL] sequencing scenario\r\n");

    // 1. Low runs first and acquires the lock.
    xSemaphoreGive(sem_start_low);
    xSemaphoreTake(sem_low_has_lock, portMAX_DELAY);

    // 2. High runs; it signals right before blocking on the lock.
    xSemaphoreGive(sem_start_high);
    xSemaphoreTake(sem_high_about_to_take, portMAX_DELAY);

    // 3. Only now start Medium. Its CPU burn delays Low (the lock holder),
    //    which inflates High's wait -> the priority inversion.
    xSemaphoreGive(sem_start_med);

    UartLogger::getInstance().log("[PI_CTRL] done, blocking\r\n");
    for (;;) { vTaskDelay(portMAX_DELAY); }
}

// Low (priority 1): holds the lock during a long CPU-bound critical section.
void Task_PI_Low(void *argument)
{
    char buf[96];
    xSemaphoreTake(sem_start_low, portMAX_DELAY);

    xSemaphoreTake(g_piLock, portMAX_DELAY);
    snprintf(buf, sizeof(buf), "[PI_LOW] acquired lock tick=%lu\r\n",
             (unsigned long)xTaskGetTickCount());
    UartLogger::getInstance().log(buf);

    xSemaphoreGive(sem_low_has_lock);   // tell controller before High starts

    busy_cycles(ms_to_cycles(300));     // hold lock during CPU work

    xSemaphoreGive(g_piLock);
    snprintf(buf, sizeof(buf), "[PI_LOW] released lock tick=%lu\r\n",
             (unsigned long)xTaskGetTickCount());
    UartLogger::getInstance().log(buf);

    for (;;) { vTaskDelay(portMAX_DELAY); }
}

// Medium (priority 2): pure CPU hog, owns no lock.
void Task_PI_Medium(void *argument)
{
    char buf[96];
    xSemaphoreTake(sem_start_med, portMAX_DELAY);

    snprintf(buf, sizeof(buf), "[PI_MED] start CPU burn tick=%lu\r\n",
             (unsigned long)xTaskGetTickCount());
    UartLogger::getInstance().log(buf);

    busy_cycles(ms_to_cycles(200));

    snprintf(buf, sizeof(buf), "[PI_MED] end CPU burn tick=%lu\r\n",
             (unsigned long)xTaskGetTickCount());
    UartLogger::getInstance().log(buf);

    for (;;) { vTaskDelay(portMAX_DELAY); }
}

// High (priority 3): measures how long it is forced to wait for the lock.
void Task_PI_High(void *argument)
{
    char buf[128];
    xSemaphoreTake(sem_start_high, portMAX_DELAY);

    uint32_t   c0 = DWT->CYCCNT;
    TickType_t t0 = xTaskGetTickCount();

    // Signal controller right before the blocking take (closes most of the race).
    xSemaphoreGive(sem_high_about_to_take);

    xSemaphoreTake(g_piLock, portMAX_DELAY);

    uint32_t   c1 = DWT->CYCCNT;
    TickType_t t1 = xTaskGetTickCount();

    uint32_t wait_cycles = c1 - c0;
    uint32_t wait_us = (uint32_t)((uint64_t)wait_cycles * 1000000U / SystemCoreClock);

    snprintf(buf, sizeof(buf),
             "[PI_HIGH] got lock wait_cycles=%lu wait_us=%lu wait_ticks=%lu\r\n",
             (unsigned long)wait_cycles,
             (unsigned long)wait_us,
             (unsigned long)(t1 - t0));
    UartLogger::getInstance().log(buf);

    xSemaphoreGive(g_piLock);

    for (;;) { vTaskDelay(portMAX_DELAY); }
}
