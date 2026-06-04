# STM32 FreeRTOS Reliability & Debug Demo

This project was built to connect FPGA/SoC-level hardware experience with embedded RTOS software practice.
It demonstrates hardware-aware embedded software design through ISR boundary management,
deterministic timing measurement, resource ownership, stack/heap monitoring,
fault detection, watchdog fallback, and evidence-based debugging.

## Hardware

- Board: NUCLEO-F446RE
- MCU: STM32F446RE (Cortex-M4, 84MHz)
- Flash: 512KB / RAM: 128KB

## Environment

- STM32CubeIDE 2.1.1
- STM32CubeMX 6.16.1 (NOT 6.17.0 — FreeRTOS CMSIS_V2 panel bug on STM32F4)
- FW Package: STM32Cube FW_F4 V1.28.3
- FreeRTOS CMSIS-RTOS V2
- USART2 → /dev/ttyACM0 → 115200 baud

## C/C++ Boundary

Application layer is implemented in C++. HAL and ISR boundary remain in C
for determinism and compatibility with CubeMX-generated code.

| Layer | Language | Reason |
|---|---|---|
| HAL / CubeMX generated | C | CubeMX regeneration compatibility |
| ISR handlers | C | Deterministic, no C++ overhead |
| Application tasks / classes | C++ | OOP structure, UartLogger, fault interfaces |

## Phases

- [x] Phase 0: Environment setup + LED Blink + FreeRTOS Hello World
- [x] Phase 1: Task Scheduling + Stack/Heap + HardFault pattern (심화)
- [x] Phase 2: ISR + DWT Latency + NVIC Priority
- [x] Phase 3: Mutex + Priority Inversion + Queue Policy
- [ ] Phase 4: Fault Detection + Watchdog
  - [x] HardFault handler + FaultRecord + noinit reset pattern
  - [ ] HealthMonitor (task heartbeat)
  - [ ] FaultInjector (UART command triggered)
  - [ ] IWDG watchdog fallback
- [ ] Phase 5: Python Automation + README completion

---

## Phase 0 — Environment Setup

**Completed:** 2026-06-02

- STM32CubeIDE 2.1.1 + CubeMX 6.16.1 separated workflow established
- NUCLEO-F446RE board verified (SWD, LED, UART)
- FreeRTOS CMSIS_V2 + TIM6 Timebase configured
- UartLogger C++ MVP verified (mutex-protected UART output)
- C++ project boundary established (main.cpp, HAL in C)

**Note:** CubeIDE 2.0.0+ no longer integrates CubeMX. Workflow:
CubeMX standalone → Generate Code → Import to CubeIDE.

---

## Phase 1 — Task Scheduling + Stack/Heap

**Completed:** 2026-06-02

| | |
|---|---|
| **Problem** | Real-time systems require predictable task timing and memory safety. |
| **Design choice** | Use `vTaskDelayUntil()` for periodic tasks because it compensates for execution time, unlike `vTaskDelay()` which measures from the end of the previous execution. Use stack high-water marks to set safe task stack sizes. |
| **Implementation** | Created three tasks (ACQ/PROC/MON) with priorities 3/2/1. Measured stack usage with `uxTaskGetStackHighWaterMark()`. Monitored heap with `xPortGetFreeHeapSize()` and `xPortGetMinimumEverFreeHeapSize()`. Triggered deliberate recursive stack overflow to verify HardFault behavior. |
| **Evidence** | UART log showing task states, vTaskDelayUntil tick values, stack watermark per task, heap free/min_ever. See `measurements/phase1_uart_log.txt`. |
| **Limitation** | Stack/heap values are measured under demo workload, not certified worst-case conditions. Production firmware requires load testing and safety margin. |

### Measured Values

| Task | Stack HWM (words) |
|---|---|
| ACQ | 433 |
| PROC | 205 |
| MON | 423 |

| Metric | Value |
|---|---|
| Heap free | 9176 bytes |
| Heap min_ever | 9176 bytes |

### Stack Overflow Observation

`configCHECK_FOR_STACK_OVERFLOW = 2` performs a software check at context switch time only.
Recursive overflow corrupted PSP before the next context switch,
causing HardFault escalation before the FreeRTOS hook could fire.
This is expected behavior and led to the HardFault pattern investigation below.

---

## Phase 1 (Extended) — HardFault Pattern

**Completed:** 2026-06-02

This work started as Phase 1 stack overflow investigation and evolved into
a complete HardFault handling pattern. Full Phase 4 (HealthMonitor, FaultInjector, IWDG) remains pending.

| | |
|---|---|
| **Problem** | A corrupted PSP from task stack overflow causes exception stacking failure, which escalates to HardFault. The handler must operate without HAL or RTOS APIs which may themselves be corrupted. |
| **Design choice** | FaultRecord saved to `.noinit` RAM survives soft-reset. UART output deferred to boot context where HAL is safe. `NVIC_SystemReset()` used for immediate controlled reset. IWDG reserved as final fallback for task hang (pending). |
| **Implementation** | `naked` HardFault_Handler passes MSP/PSP/LR to `HardFault_Minimal`. CFSR/HFSR/PC saved to FaultRecord. USART2 register direct polling used for immediate fault message (no HAL). `check_fault_on_boot()` reports full FaultRecord via UART on next boot. |
| **Evidence** | CFSR=0x00009600 (STKERR+BFARVALID), HFSR=0x40000000 (FORCED escalation), BFAR=0x1FFFBFEC, PSP=0x1FFFBFC8. BFAR and PSP differ by 0x24, confirming stacking attempted access to corrupted PSP region outside valid SRAM (0x20000000~). See `measurements/fault_test_20260602_223708.log`. |
| **Limitation** | FaultRecord valid after soft-reset only, not POR-safe. PC extraction skipped when stacking itself failed (STKERR set), recorded as 0xFFFFFFFF. Magic value only, no CRC. |

### Fault Register Values (measured)

| Register | Value | Interpretation |
|---|---|---|
| CFSR | 0x00009600 | BFSR: STKERR + BFARVALID set |
| HFSR | 0x40000000 | FORCED escalation to HardFault |
| BFAR | 0x1FFFBFEC | Outside valid SRAM (starts 0x20000000) |
| PSP  | 0x1FFFBFC8 | Corrupted by stack overflow |
| MSP  | 0x2001FFE0 | Normal |
| BFAR−PSP delta | 0x24 | Stacking failure address near corrupted PSP |

### Reset Strategy

| Event | Reset method | Reason |
|---|---|---|
| HardFault | `NVIC_SystemReset()` | Fault cause known, immediate controlled reset |
| Task hang (pending) | IWDG timeout | Software unresponsive, final fallback |

### HardFault Handler Design Note

The HardFault handler intentionally avoids HAL and FreeRTOS APIs.
Because task stack overflow can corrupt PSP and RTOS state, the fault path
records minimal diagnostic state via SCB registers and falls back to system reset.
Fault reason is reported via UART on next boot.

During development, three approaches were tried before reaching the final design:

1. `HAL_UART_Transmit` directly — second fault due to corrupted PSP stack frame
2. naked ASM + `HAL_UART_Transmit` — HAL unreliable in fault context confirmed
3. USART2 register polling + FaultRecord + `NVIC_SystemReset()` — final design, verified

---

## Measurements & Evidence

| File | Phase | Description |
|---|---|---|
| `phase1_uart_log.txt` | Phase 1 | Task UART output, vTaskDelayUntil ticks, stack watermark, heap monitor |
| `phase1_stackoverflow_log.txt` | Phase 1 extended (attempt 1) | USART2 register polling in fault context — fault message confirmed, while(1) halt |
| `phase1_stackoverflow_log_nakedASM.txt` | Phase 1 extended (attempt 2) | naked ASM + HAL_UART_Transmit — second fault observed, HAL unreliable in fault context confirmed |
| `fault_test_20260602_223708.log` | Phase 1 extended (final) | FaultRecord + register polling + NVIC_SystemReset — full fault/boot report cycle verified |

The progression across three attempts reflects deliberate fault diagnosis:
register values (CFSR/HFSR/BFAR) were used at each stage to understand
failure mode and inform the next design decision.

---

## Phase 2A — ISR-to-Task Communication + DWT Latency

### Problem
ISR-to-task response time matters in real-time embedded systems;
measuring it requires cycle-accurate timing.

### Design Choice
- EXTI ISR + FreeRTOS queue to defer processing from interrupt context to task context
- DWT CYCCNT for cycle-accurate latency measurement (nanosecond resolution on Cortex-M4)
- ISR captures DWT->CYCCNT immediately on entry (first instruction)
- Task captures DWT->CYCCNT immediately on queue receive

### Implementation
- PC13 (B1 Button) EXTI falling edge → xQueueSendFromISR() → LatencyTask
- DWT->CYCCNT captured at ISR entry and task receive
- latency_us = (delta_cycles × 1,000,000) / SystemCoreClock
- LatencyMeter C++ class handles calculation and CSV formatting

### Evidence
- Measured ISR-to-task latency: **~16.8us** (1413 cycles @ 84MHz)
- Log file: `measurements/latency_20260603_115219.txt'

---

## Phase 2B — NVIC Interrupt Priority

### Problem
FreeRTOS restricts which interrupts can safely call RTOS APIs;
violating this causes system freeze or hard faults.

### Design Choice
Only interrupts at or below configMAX_SYSCALL_INTERRUPT_PRIORITY
can call FromISR APIs. Higher-priority interrupts must not call
any FreeRTOS API.

### Implementation
- configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5
- SystemCoreClock = 84MHz (verified at runtime)
- Safe test: Priority 6 → xQueueSendFromISR() normal operation
- Unsafe test: Priority 4 → configASSERT fires → system freeze

### Evidence
- measurements/nvic_safe_priority6_*.txt
- measurements/nvic_unsafe_priority4_*.txt

### Interrupt Priority Table

| Priority | Zone     | FreeRTOS FromISR API | Result                       |
|----------|----------|----------------------|------------------------------|
| 0~4      | Unsafe   | Not allowed          | System freeze (configASSERT) |
| 5        | Boundary | configMAX_SYSCALL    | —                            |
| 6~15     | Safe     | Allowed              | Normal operation             |

---
## Phase 3A — Mutex + UartLogger Priority Inheritance

**Completed:** 2026-06-04

| | |
|---|---|
| **Problem** | Shared UART access from multiple tasks can interleave output and cause priority inversion if a low-priority task holds the port while a high-priority task waits. |
| **Design choice** | Use a mutex (not a binary semaphore) for UART protection. FreeRTOS mutexes support priority inheritance, which bounds the blocking time of a high-priority waiter. UartLogger uses `osMutexPrioInherit` explicitly rather than relying on defaults. |
| **Implementation** | UartLogger singleton creates its mutex with `osMutexAttr_t` setting `osMutexPrioInherit`. All `log()` calls acquire/release the mutex around `HAL_UART_Transmit`. |
| **Evidence** | All subsequent multi-task logs (PI, EVENT, QUEUE) share UART without corruption. |
| **Limitation** | Mutex protects ordering, not throughput; heavy logging can make UART the system bottleneck (observed in Phase 3D, mitigated by rate-limiting consumer output). |

---

## Phase 3B — Priority Inversion (Mutex Inheritance)

**Completed:** 2026-06-04

| | |
|---|---|
| **Problem** | When a low-priority task holds a lock a high-priority task needs, a medium-priority task can preempt the low task and indirectly block the high task — unbounded priority inversion. |
| **Design choice** | Reproduce inversion deterministically, then mitigate with mutex priority inheritance. A controller task (P=4) sequences the scenario via handshake semaphores so ordering does not depend on startup delays. A compile switch `PI_USE_MUTEX` toggles the lock between binary semaphore (no inheritance, BEFORE) and mutex (inheritance, AFTER). |
| **Implementation** | Four tasks: Controller(4), High(3), Medium(2), Low(1). Low takes the lock and runs a DWT-cycle-based critical section (300ms). Medium runs a 200ms CPU burn. High measures lock-wait via DWT->CYCCNT. busy_cycles() uses runtime SystemCoreClock, not a hardcoded value. |
| **Evidence** | BEFORE (binary): High wait = 508ms. AFTER (mutex): High wait = 302ms. The ~206ms reduction equals the removed Medium interference. See `measurements/phase3_inversion_before_*.txt` and `phase3_inversion_after_*.txt`. |
| **Limitation** | Priority inversion scenario is simplified for demonstration; real systems require design-level avoidance, not just inheritance. A residual TOCTOU gap exists between High's "about to take" signal and the actual blocking take, but no task can preempt High in that window in this setup. |

### Measured Values

| Mode | Lock type | High wait (us) | High wait (ticks) |
|---|---|---|---|
| BEFORE | binary semaphore | 508061 (~508ms) | 508 |
| AFTER | mutex (prio inherit) | 302343 (~302ms) | 302 |

Cross-check (AFTER): 25,397,064 cycles ÷ 84,000,000 Hz = 302.3ms. Tick, microsecond, and cycle measurements agree.

### Event Ordering Evidence

In BEFORE, Medium starts (tick 45) while Low holds the lock, delaying High.
In AFTER, Low inherits High's priority, finishes its critical section, and High
acquires the lock *before* Medium runs (Medium start delayed to tick 358).
This ordering shift is the direct evidence of priority inheritance.

---

## Phase 3C — Binary Semaphore Event Signaling (ISR → Task)

**Completed:** 2026-06-04

| | |
|---|---|
| **Problem** | An interrupt must hand off an event to a task without doing work in ISR context. A binary semaphore is the canonical mechanism for ISR-to-task deferred signaling. |
| **Design choice** | The PC13 EXTI ISR gives a binary semaphore; a consumer task blocks on it. This path is kept fully separate from the Phase 2A latency queue path (own task, own semaphore, own counter). Both FromISR calls share one `xHigherPriorityTaskWoken`, with a single `portYIELD_FROM_ISR`. |
| **Implementation** | `xSemaphoreGiveFromISR(g_buttonEventSem, ...)` in the EXTI ISR (priority 6, FromISR-safe per Phase 2B). Consumer increments `button_event_count`. An observability-only `g_isrRawCount` (volatile, not used for control) counts ISR entries to expose semaphore coalescing. |
| **Evidence** | Button presses produced `button_event_count == isr_raw_count` (1:1) with no bounce in the capture. See `measurements/phase3_event_semaphore_*.txt`. |
| **Limitation** | A binary semaphore coalesces multiple pending signals into one; `raw > event` (not observed here) would indicate bounce or a slow consumer. This is intended behavior, not debounce — no debounce logic is implemented. |

### Coalescing Note

`raw > event` means multiple ISR triggers were coalesced by the binary
semaphore. This can happen due to button bounce or because the consumer had
not yet taken the semaphore. `g_isrRawCount` is a diagnostic counter only.

---

## Phase 3D — Queue Full Policy (DROP-NEW)

**Completed:** 2026-06-04

| | |
|---|---|
| **Problem** | When a producer outpaces a consumer, the queue fills. The overflow policy determines what data is lost and whether the loss is visible. |
| **Design choice** | DROP-NEW: on a full queue the new item is dropped and existing queued items are preserved (`xQueueSend` with timeout 0, no `xQueueOverwrite`). Preserving queued order matters more than keeping only the newest value for this event-stream demo. Two counters are kept separate by design. |
| **Implementation** | Dedicated queue (depth 5, item `uint32_t`), separate from the Phase 2A latency queue. Producer (P=2, 10ms) generates items; Consumer (P=1, 100ms) drains one per cycle and prints counters every 10th cycle (~1 line/sec). Producer does not log per-drop, to avoid UART becoming the bottleneck. |
| **Evidence** | drop_count and overflow_count rose together at ~90/sec (Producer 10x faster than Consumer). See `measurements/phase3_queue_dropnew_*.txt`. |
| **Limitation** | The two counters increment together in this demo; they diverge only once a send timeout or partial-retention policy is introduced. Queue policy must match application data-loss requirements. |

### Counter Semantics

| Counter | Meaning |
|---|---|
| `queue_drop_count` | Items actually lost (data loss) |
| `queue_overflow_count` | Queue-full events (saturation) |

They measure different concepts and are kept separate so that introducing a
send timeout or a priority-based retention policy does not require restructuring
the instrumentation.

---

## Stack / Heap Sizing Validation

**Completed:** 2026-06-04

Task stack sizes were validated using `uxTaskGetStackHighWaterMark()` rather
than guessed. The queue consumer initially failed at 128 words: a stack
overflow occurred during `snprintf`-based logging, detected by the FreeRTOS
stack overflow hook. Increasing to 256 words gave a measured peak usage of
130 words. The producer, which performs no logging, uses only 34 words.

### Stack Sizing (Phase 3D tasks)

| Task | Allocated | Peak used (HWM) | Margin | Verdict |
|---|---|---|---|---|
| QPROD | 128 words (512 B) | 34 words (136 B) | 94 words (73%) | adequate |
| QCONS | 256 words (1024 B) | 130 words (520 B) | 126 words (49%) | adequate; 128 words overflowed |

### Heap Budget (runtime, full task set)

| Metric | Value |
|---|---|
| Total heap (`configTOTAL_HEAP_SIZE`) | 15360 B |
| Free heap (runtime) | 880 B |
| Used heap | 14480 B (94%) |

**Observation:** Free heap dropped from 9176 B (Phase 1) to 880 B after adding
the Phase 3 task set (4 priority-inversion tasks, 1 event consumer, producer/
consumer pair). The system is now near its heap budget; adding further tasks
would require reducing existing stack allocations. This is why stack sizes were
validated by measurement rather than over-provisioned.

## Measurements & Evidence

| File | Phase | Description |
| `phase3_inversion_before_*.txt` | Phase 3B | Priority inversion with binary semaphore — High wait 508ms |
| `phase3_inversion_after_*.txt` | Phase 3B | Mutex priority inheritance — High wait 302ms |
| `phase3_event_semaphore_*.txt` | Phase 3C | ISR-to-task binary semaphore — button_event_count vs isr_raw_count |
| `phase3_queue_dropnew_*.txt` | Phase 3D | Queue full DROP-NEW — drop/overflow counters, stack HWM, heap |

---

## Known Limitations

| Date | Known Limitation |
|---|---|
| 2026-06-02 | C++ usage is limited to application layer; HAL and ISR boundary remain in C for determinism. |
| 2026-06-02 | Stack/heap values are measured under demo workload, not certified worst-case conditions. |
| 2026-06-02 | `configCHECK_FOR_STACK_OVERFLOW = 2` is a software check at context switch time only; HardFault occurs before the hook fires if stack overflows between switches. |
| 2026-06-02 | Task-level recovery is intentionally conservative; if a task may hold shared resources, watchdog reset is safer than local recovery. |
| 2026-06-02 | FaultRecord is retained across soft-reset only; power-on reset clears .noinit RAM. |
| 2026-06-02 | PC extraction is skipped when exception stacking itself fails (STKERR set); recorded as 0xFFFFFFFF. |
| 2026-06-02 | FaultRecord validity relies on magic value only, no CRC. Silent corruption between fault and reset is not detected. |
| 2026-06-02 | NVIC_SystemReset() triggers system reset but does not guarantee peripheral re-initialization order. Boot sequence is assumed to complete normally. |
| 2026-06-02 | HardFault_Handler naked attribute is outside USER CODE blocks and must be manually verified after CubeMX regeneration. vApplicationStackOverflowHook consolidated into freertos.c USER CODE BEGIN 4. |
| 2026-06-03 | ISR-to-task latency measured under limited load; production needs worst-case analysis under full system load. (Phase 2A) |
| 2026-06-03 | Only priority 4 (unsafe) and 6 (safe) were tested; full verification requires testing all used interrupt priorities. (Phase 2B) |
| 2026-06-04 | Priority inversion scenario is simplified for demonstration; real systems require design-level avoidance, not just inheritance. (Phase 3B) |
| 2026-06-04 | A residual TOCTOU gap exists between High's signal and its blocking lock take; safe here only because no task can preempt High in that window. (Phase 3B) |
| 2026-06-04 | Binary semaphore coalesces multiple pending signals; this is signaling behavior, not debounce. No debounce logic is implemented. (Phase 3C) |
| 2026-06-04 | Queue drop_count and overflow_count increment together in this demo; they diverge only with a send timeout or partial-retention policy. (Phase 3D) |
| 2026-06-04 | Free heap is 880 B of 15360 B (94% used); the task set is near the heap budget. Adding tasks requires reducing existing stack allocations. (Phase 3) |
| TBD | Watchdog is used as final fallback, not selective recovery. (Phase 4) |
| TBD | This is not production firmware: no MISRA compliance, no long-duration qualification. (Phase 5) |
