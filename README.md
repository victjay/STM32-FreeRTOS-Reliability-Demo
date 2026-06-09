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
- [x] Phase 1: Task Scheduling + Stack/Heap + HardFault pattern (extend)
- [x] Phase 2: ISR + DWT Latency + NVIC Priority
- [x] Phase 3: Mutex + Priority Inversion + Queue Policy
- [x] Phase 4: Fault Detection + Watchdog
  - [x] HardFault handler + FaultRecord + noinit reset pattern
  - [x] HealthMonitor (task heartbeat)
  - [x] FaultInjector (UART command triggered)
  - [x] IWDG watchdog fallback
- [x] Phase 5: Python Automation + README completion
- [x] Phase 6 (Bonus): UART RX DMA + IDLE-line detection
  - [x] Phase 6A-1: UART RX DMA circular + IDLE line + QueueFromISR
  - [x] Phase 6A-2: DMA RX error recovery policy + escalation to IWDG

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
a complete HardFault handling pattern. Phase 4 (HealthMonitor, FaultInjector, IWDG)
builds on this foundation and is now complete.

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
- Log file: `measurements/latency_20260603_115219.txt`

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
|---|---|---|
| `phase3_inversion_before_*.txt` | Phase 3B | Priority inversion with binary semaphore — High wait 508ms |
| `phase3_inversion_after_*.txt` | Phase 3B | Mutex priority inheritance — High wait 302ms |
| `phase3_event_semaphore_*.txt` | Phase 3C | ISR-to-task binary semaphore — button_event_count vs isr_raw_count |
| `phase3_queue_dropnew_*.txt` | Phase 3D | Queue full DROP-NEW — drop/overflow counters, stack HWM, heap |

---

## Phase 4 — Fault Detection + Watchdog Fallback

| Field | Content |
|---|---|
| **Problem** | Tasks can stop making progress (hang, suspension, resource starvation) while the scheduler still runs. The system must detect loss of liveness and reach a safe state when local recovery is not trustworthy. |
| **Design choice** | Heartbeat-based HealthMonitor (priority 5) detects per-task timeout. HealthMonitor is the **sole IWDG feed owner**: it refreshes the watchdog only when no fault is latched. Recovery is intentionally conservative — on fault latch, feeding stops and the IWDG resets the system, rather than attempting to resume an unknown-state task. A FaultInjector task triggers faults via UART for repeatable evidence. |
| **Implementation** | HealthMonitor: `registerTask`/`heartbeat`/`check`, fault latched and never cleared in software, all shared access under critical section. FaultInjector (UART polling): `TASK_SUSPEND` (vTaskSuspend on ACQ), `HEAP_STRESS` (pvPortMalloc loop), `RESET`. IWDG via CubeMX (prescaler 32, reload 3000, ~3s nominal). Watchdog recovery evidence stored in `.noinit` WatchdogRecord (fault_task_id, fault_latch_tick, feed_stop_tick, boot_count) and reported on next boot together with the RCC_CSR reset cause. |
| **Evidence** | TASK_SUSPEND → heartbeat timeout (~1.8s) → feed stop → IWDG reset → boot reports `reset cause: IWDG watchdog` and WatchdogRecord (task_id=0, boot_count). HEAP_STRESS → malloc failed hook count=1, heap free=0. |
| **Limitation** | IWDG real timeout varies ~2.0–5.7s due to LSI tolerance. Recovery latency equals the IWDG timeout and cannot be measured at cycle resolution because DWT/tick reset on reboot; only the fault latch tick (relative to boot) is captured precisely. CPU hog is detectable only for tasks at priority ≤ 4. |

### HealthMonitor as IWDG Feed Owner

The HealthMonitor task is the only owner of `HAL_IWDG_Refresh()`. If the monitor
itself stops running, the watchdog is no longer fed and the system resets. This
is intentional: if the monitor cannot run, the system state cannot be trusted.
Feed period (500ms) is kept well below the IWDG timeout (~3s nominal).

### Fault Injection Matrix

| # | Scenario | Trigger | Detection | Recovery Path | Evidence |
|---|---|---|---|---|---|
| 1 | Task unresponsive | `TASK_SUSPEND` (vTaskSuspend ACQ) | HealthMonitor heartbeat timeout (1500ms) | Feed stop → IWDG reset | `phase4_task_suspend_hm_timeout_*`, `phase4_iwdg_reset_*` |
| 2 | Queue full | Producer faster than consumer | DROP-NEW + drop/overflow counters | Local counting, no reset | `phase3_queue_dropnew_*` |
| 3 | Stack overflow | Deliberate recursion | HardFault escalation / overflow hook | Fault record + reset (Phase 1) | `fault_test_*` |
| 4 | Heap exhaustion | `HEAP_STRESS` (pvPortMalloc loop) | `vApplicationMallocFailedHook` count | No recovery, hook logged | `phase4_heap_stress_malloc_hook_*` |

Only scenario 1 escalates to a watchdog reset. Scenarios 2–4 are detected and
logged locally without reset, consistent with the conservative recovery policy:
a watchdog reset is used only when task state cannot be trusted.

### Measurements & Evidence

| File | Phase | Description |
|---|---|---|
| `phase4_task_suspend_hm_timeout_*` | Phase 4 | TASK_SUSPEND → HealthMonitor heartbeat timeout detection |
| `phase4_heap_stress_malloc_hook_*` | Phase 4 | HEAP_STRESS → malloc failed hook count=1, heap free=0 |
| `phase4_fi_reset_softwarereset_*` | Phase 4 | RESET command → software reset cause on boot |
| `phase4_iwdg_reset_*` | Phase 4 | Fault latch → IWDG feed stop → IWDG watchdog reset |
| `phase4_iwdg_bootcount_poweron_*` | Phase 4 | WatchdogRecord a WatchdogRecord across reset, boot_count validated |

---

## Phase 5 — Python Automation + Evidence Pipeline

**Completed:** 2026-06-05

| Field | Content |
|---|---|
| **Problem** | Fault scenarios and measurements were captured manually with a serial terminal. Manual capture is not repeatable, is error-prone, and does not produce structured evidence that can be cited directly in this README. |
| **Design choice** | A small host-side Python toolset (pyserial) owns the serial port and automates capture, fault injection, and log-to-CSV conversion. Because the firmware FaultInjector polls the same UART port, only one host process may hold the port; capture and command-send are therefore combined in one owner rather than split across processes. Commands use send-and-confirm retransmission to tolerate dropped bytes on the firmware's polling-based RX path. |
| **Implementation** | `uart_session.py` (interactive capture + manual send), `uart_io.py` (shared `UartSession` with `send_and_confirm`), `run_fault_matrix.py` (automated HEAP_STRESS → TASK_SUSPEND → RESET, one log per scenario), `log_parser.py` (latest-match log selection → `stack_watermark.csv`, `heap.csv`, `latency.csv`, `fault_matrix.csv`). Steady-state stack/heap are taken from a dedicated baseline capture, not from a fault run. See `tools/scripts/README.md`. |
| **Evidence** | Four CSV files generated under `measurements/csv/`. The fault matrix CSV reproduces all four scenarios with detection evidence and source-log attribution (stack overflow attributed to the Phase 1 extended log, not a UART command). |
| **Limitation** | The host-side retransmission masks but does not fix the firmware UART RX byte loss; the proper fix is UART RX DMA (Phase 6). Parsed values reflect demo workload, not certified worst-case conditions. |

### Measured Values (from generated CSV)

Steady-state baseline (`phase5_baseline_*.txt` → CSV):

| Task | Stack HWM (words) |
|---|---|
| ACQ | 368 |
| PROC | 198 |
| MON | 366 |

| Metric | Value |
|---|---|
| Heap free (baseline) | 6904 bytes |
| Heap min_ever (baseline) | 6904 bytes |
| ISR-to-task latency | ~16.5–16.8 us (1413 cycles @ 84MHz) |

**Note:** The baseline heap free (6904 B) reflects the Phase 4 minimal task set
(HealthMonitor + FaultInjector active; Phase 2/3 demo tasks disabled by compile
macro). It is higher than the 880 B measured in Phase 3, which had the full
demo task set active. The figure depends on which task set is compiled in.

### Toolset

| Script | Role |
|---|---|
| `uart_session.py` | Interactive UART capture + manual command send |
| `uart_io.py` | Shared serial I/O (`UartSession`, send-and-confirm) |
| `run_fault_matrix.py` | Automated 3-scenario fault injection |
| `log_parser.py` | Logs → `stack_watermark` / `heap` / `latency` / `fault_matrix` CSV |

### Measurements & Evidence

| File | Phase | Description |
|---|---|---|
| `phase5_baseline_*.txt` | Phase 5 | Steady-state capture for stack/heap baseline |
| `phase5_heap_stress_*.txt` | Phase 5 | Automated HEAP_STRESS run |
| `phase5_task_suspend_*.txt` | Phase 5 | Automated TASK_SUSPEND → IWDG reset run |
| `phase5_software_reset_*.txt` | Phase 5 | Automated RESET run |
| `csv/stack_watermark.csv` | Phase 5 | Per-sample task stack high-water marks |
| `csv/heap.csv` | Phase 5 | Per-sample free / min-ever heap |
| `csv/latency.csv` | Phase 5 | ISR-to-task latency samples |
| `csv/fault_matrix.csv` | Phase 5 | Per-scenario detection evidence with source attribution |

---

## Design Decisions / Evaluated Alternatives

### Watchdog: IWDG selected over WWDG

WWDG (Window Watchdog) was evaluated but not implemented. The project fault
model focused on task hang detection and watchdog-based system recovery. An
LSI-based IWDG was selected because it operates independently of the system
clock tree and provides a more suitable final fallback mechanism for RTOS
health monitoring.

### UART RX: polling now, DMA planned (Phase 6)

The firmware receives FaultInjector commands one byte at a time via polling
(`HAL_UART_Receive`), and the STM32F446 USART has no hardware RX FIFO. When the
FaultInjector task is preempted, back-to-back bytes at 115200 baud can be
overwritten before they are read, so a command may be received partially (for
example `[FI] unknown cmd: T`) or dropped. This byte loss was observed and
captured during Phase 5 automation.

The host-side mitigation (`send_and_confirm` retransmission in `uart_io.py`)
keeps automation reliable but does not address the root cause. The firmware-level
fix — UART RX via DMA with IDLE-line detection — is tracked as Phase 6 (Bonus).
The captured byte-loss evidence is the motivation for that work
(`measurements/session_20260605_125452_*.txt`).

---

## Phase 6A-1 — UART RX via DMA Circular + IDLE Line

**Completed:** 2026-06-06

| Field | Content |
|---|---|
| **Problem** | FaultInjector received commands with `HAL_UART_Receive` polling (1 byte, 100ms timeout). The STM32F446 USART has no hardware RX FIFO, so when the task was preempted, consecutive bytes overran and were lost — observed as partial commands (`[FI] unknown cmd: T`). |
| **Design choice** | Replace polling with USART2 RX DMA in circular mode plus UART IDLE-line detection for variable-length command framing. The IDLE ISR computes frame length from the DMA NDTR counter and posts one frame via `xQueueSendFromISR`; the FaultInjector task consumes frames with `xQueueReceive`. A compile switch `FI_RX_DMA` keeps the legacy polling path for direct before/after comparison. The `uart_dma_rx` module owns the command queue (producer-owns-transport); FaultInjector retrieves it via `uart_dma_rx_get_queue()`. |
| **Implementation** | DMA1 Stream5, Channel 4, circular, byte/byte (CubeMX-generated, RM0390-mapped). NVIC priority 6 for both USART2 and DMA1_Stream5 IRQ (FromISR-safe, configMAX_SYSCALL=5). IDLE handled in `USART2_IRQHandler` USER CODE: flag clear → `uart_dma_rx_on_idle()` → NDTR-based length → frame copy → queue. The queue is created before the scheduler starts; DMA reception starts on FaultInjector task entry. Existing `fi_dispatch()` reused unchanged after trailing CR/LF/whitespace trim. |
| **Evidence** | Normal: three spaced commands (HEAP_STRESS / TASK_SUSPEND / HEAP_STRESS) parsed as separate frames (len=12/13/12, data0='H'/'T'/'H') and dispatched correctly. See `measurements/phase6a1_dma_rx_20260606_142545.log`. Limitation baseline: five back-to-back `TASK_SUSPEND` (65B, no idle gap) overran the 64B circular buffer and yielded a single 1-byte frame. See `measurements/phase6a1_dma_rx_20260606_142226.log`. |
| **Limitation** | UART IDLE framing requires a small inter-command idle gap; sending more than the 64-byte buffer back-to-back with no gap overruns the circular buffer. The IDLE ISR copies up to 64 bytes (MVP); for higher throughput the ISR should publish indices only and defer copy/parse to task context. Buffer-overrun / DMA error / USART ORE handling is deferred to Phase 6A-2. |

### Debugging Note — HAL_OK but DMA Not Running

`HAL_UART_Receive_DMA` returned `HAL_OK`, yet no data was received and NDTR stayed
at the buffer size. Root cause: `main.c` had been manually converted to `main.cpp`,
so the `MX_DMA_Init()` call that CubeMX newly generated was never added to `main.cpp`.
DMA1 clock and the DMA1_Stream5 NVIC vector were therefore never enabled, so the
stream did not run despite the HAL success return. The fault was isolated by logging
the start status, `huart2.hdmarx`, `huart2.RxState`, and the DMA NDTR counter
step by step, separating "HAL reports success" from "hardware actually transferring".
Diagnostic hooks (`uart_dma_rx_dbg_*`) are retained for reuse in Phase 6A-2.

### RX Path Toggle

| `FI_RX_DMA` | RX path | Purpose |
|---|---|---|
| defined (default) | DMA circular + IDLE line | Phase 6A-1 |
| undefined | `HAL_UART_Receive` polling | legacy byte-loss baseline |

---

## Phase 6A-2 — UART/DMA RX Recovery Policy + Escalation

**Completed:** 2026-06-09

| Field | Content |
|---|---|
| **Problem** | A DMA RX path can hit recoverable errors (USART overrun, DMA transfer error). The system must recover locally when possible, but must not retry forever — repeated failure means the receive path can no longer be trusted and a controlled reset is safer than continuing. |
| **Design choice** | Graduated response built on the Phase 4 IWDG path: (1) a single error → local DMA restart + increment a consecutive-failure counter; (2) a normal frame → counter cleared (recovery confirmed); (3) three consecutive failures → the path is declared unreliable and the fault is escalated to HealthMonitor, which stops feeding the IWDG → controlled reset. Escalation reuses the existing Phase 4 reset path; no new reset mechanism. Application-level abnormal input (long frame / queue overflow) is counted only, never escalated. |
| **Implementation** | ISR (`HAL_UART_ErrorCallback`) and injection share one entry point `mark_error_pending()` (sets pending flag + error code + count, nothing else). `uart_dma_rx_service()` (task context) reads the pending flag, restarts DMA, increments `consecutive_fail`, and at threshold 3 calls `HealthMonitor_ReportExternalFault()` (first-fault-wins). The command task calls `service()` once per loop (100ms `xQueueReceive` timeout) and `notify_rx_ok()` on a valid/empty frame. Recovery success = a later normal frame, not the restart API return. FaultInjector commands drive the real path for deterministic verification. |
| **Evidence** | REPEAT3 → external fault → IWDG feed stop → reset → `WdgRecord v2 src=1 task_id=0xFFFFFFFF rx_fail=3 rx_err=0xE17`. TASK_SUSPEND still records `src=0 task_id=0` (Phase 4 path intact). LONG_FRAME counted only (`long_frame=1`, no reset). ERROR_ONCE → local restart, no reset. See `measurements/phase6a2_*.log`. |
| **Limitation** | Hardware ORE/TE reproduction is environment-dependent and not forced; the recovery policy is verified deterministically via command injection that joins the same pending-flag path. A single `bool` pending flag coalesces multiple errors arriving before the service task runs. The 3-failure threshold is a demo choice, not a certified value. |

### Recovery Escalation Ladder

| Consecutive failures | Judgment | Action |
|---|---|---|
| 0 (normal frame) | healthy | `consecutive_fail = 0` |
| 1 | transient fault | local DMA restart |
| 2 | suspicious, still recoverable | local DMA restart |
| 3 | repeated recovery failure → path unreliable | escalate → HealthMonitor → IWDG reset |

A normal frame at any point clears the counter — only *consecutive* failures escalate.

### Fault Classification

| Event | Escalated? | Handling |
|---|---|---|
| USART ORE / DMA transfer error | Yes | local restart, then escalate after 3 consecutive |
| Long frame / queue overflow | No | counted only (observation) |

### ISR vs Task Responsibility

| Concern | Location | Reason |
|---|---|---|
| Error detection, flag/count | ISR (`HAL_UART_ErrorCallback`) | minimal ISR work |
| Restart, counter, threshold, escalation | Task (`uart_dma_rx_service`) | policy decisions, may call HealthMonitor |
| `HealthMonitor_ReportExternalFault` | Task only | uses critical section; never ISR-safe |

The command task drives the RX recovery service periodically to avoid adding a
separate task in this demo; the recovery policy itself lives in `uart_dma_rx`,
not in FaultInjector.

### Design Notes

**Stack-aware logging.** `snprintf` consumes significant stack. On the deep call
chain (command task → dispatch → inject_error_repeat → service → escalation), a
stack buffer + `snprintf` overflowed the 256-word FaultInjector task stack. The
escalation log was reduced to a fixed string; diagnostic values are preserved in
the `.noinit` WatchdogRecord and reported at boot. ISR is not the only stack-
constrained context — a shallow task stack on a deep call chain fails the same way.

**Injection joins the real path.** Injected errors do not set the result directly.
They set the same pending flag as the real `HAL_UART_ErrorCallback`; escalation is
always decided inside `service()` (no counter shortcuts), so the test exercises the
actual recovery policy.

**Observability shares the RX channel.** `DMA_RX_STATS` arrives as a normal frame,
which itself clears `consecutive_fail` via `notify_rx_ok()` before the stats are
printed. So a single-error count is not directly observable through STATS; the
authoritative escalation evidence is the boot `WdgRecord` instead.

### Measurements & Evidence

| File | Phase | Description |
|---|---|---|
| phase6a2_escalation_20260609_133525.log | Phase 6A-2 | REPEAT3 → escalation → IWDG reset → WdgRecord src=1 rx_fail=3 rx_err=0xE17 |
| phase6a2_heartbeat_regression_20260609_133659.log | Phase 6A-2 | TASK_SUSPEND → WdgRecord src=0 (Phase 4 path intact) |
| phase6a2_long_frame_20260609_133600.log | Phase 6A-2 | LONG_FRAME counted only, not escalated |
| phase6a2_single_recovery_20260609_133448.log | Phase 6A-2 | ERROR_ONCE → local restart, no reset |






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
| 2026-06-05 | Task-level recovery is intentionally conservative; when a task's internal state is unknown or it may hold shared resources, watchdog reset is preferred over local recovery. (Phase 4) |
| 2026-06-05 | IWDG real timeout varies ~2.0–5.7s due to LSI tolerance (17–47kHz); the 3s target is nominal, not a certified timing guarantee. (Phase 4) |
| 2026-06-05 | HealthMonitor detects CPU hog only for tasks at priority <= 4; higher-priority tasks or interrupt storms are not detectable and rely on IWDG fallback. (Phase 4) |
| 2026-06-05 | RCC CSR reset flags are cumulative; a PIN reset flag may persist alongside a later IWDG or software reset cause. (Phase 4) |
| 2026-06-05 | boot_count relies on a magic guard against uninitialized .noinit memory; a 1-in-2^32 magic collision could skip initialization. (Phase 4) |
| 2026-06-05 | CubeMX regeneration strips __attribute__((naked)) from HardFault_Handler; it must be re-applied after each .ioc regeneration. (Phase 4) |
| 2026-06-05 | Polling-based UART RX loses bytes under task preemption (no hardware RX FIFO on STM32F446 USART); commands may be received partially or dropped. Host-side retransmission mitigates this; the firmware fix is UART RX DMA (Phase 6). (Phase 5) |
| 2026-06-05 | CSV evidence reflects demo workload under the Phase 4 minimal task set; values depend on which task set is compiled in and are not certified worst-case figures. (Phase 5) |
| 2026-06-05 | This is not production firmware: no MISRA compliance, no long-duration qualification, no full worst-case timing certification. (Phase 5) |
| 2026-06-06 | UART IDLE-based framing needs a small inter-command idle gap; a burst larger than the 64-byte DMA buffer with no gap overruns the circular buffer (observed: 5×13B → single 1-byte frame). |
| 2026-06-06 | The IDLE ISR copies up to 64 bytes inline; for higher baudrate/throughput the ISR should publish buffer indices only and defer copy/parse to task context. |
| 2026-06-06 | A manually converted main.cpp does not receive CubeMX-regenerated init calls (e.g. MX_DMA_Init); these must be re-added by hand after regeneration. |
| 2026-06-09 | Hardware ORE/TE is not force-reproduced; recovery policy is verified via command injection joining the same pending-flag path. |
| 2026-06-09 | A single bool pending flag coalesces multiple RX errors arriving before the service task runs. |
| 2026-06-09 | The 3 consecutive-failure escalation threshold is a demo choice, not a certified value. |
| 2026-06-09 | DMA_RX_STATS arrives as a normal frame and clears consecutive_fail before printing; single-error counts are observed via boot WdgRecord, not STATS. |
