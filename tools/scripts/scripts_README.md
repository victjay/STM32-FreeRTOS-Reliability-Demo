# Python Automation Scripts

Host-side tooling for the STM32 FreeRTOS Reliability & Debug Demo.
These scripts capture UART logs from the NUCLEO-F446RE board, drive the
firmware FaultInjector, and convert captured logs into CSV evidence.

All scripts are host-side only. They do not modify firmware.

## Scripts

| Script | Role |
|---|---|
| `uart_session.py` | Interactive UART session: live log capture to file + manual command sending. |
| `uart_io.py` | Shared serial I/O module (`UartSession` class, send-and-confirm). Imported by `run_fault_matrix.py`; not run directly. |
| `run_fault_matrix.py` | Automated fault-injection runner. Executes the UART-driven fault scenarios in sequence and saves one log per scenario. |
| `log_parser.py` | Offline parser. Reads captured logs and writes `stack_watermark.csv`, `heap.csv`, `latency.csv`, and `fault_matrix.csv`. |

## Setup

```bash
cd ~/git/projects/STM32-FreeRTOS-Reliability-Demo
python3 -m venv tools/venv
source tools/venv/bin/activate
pip install -r tools/scripts/requirements.txt
```

The virtual environment (`tools/venv/`) is git-ignored. Re-create it with
the steps above on a fresh checkout.

## Serial port ownership (important)

The firmware FaultInjector polls the same UART port (`/dev/ttyACM0`) that
these scripts use. Only one process may hold the port at a time.

- Do not run `minicom` while any of these scripts are running.
- Do not run `uart_session.py` and `run_fault_matrix.py` at the same time.

On a NUCLEO board the ST-LINK virtual COM port stays enumerated through a
target MCU reset, so the same serial session continues to receive the
post-reset `[BOOT]` lines.

## Usage

### Capture a session (and optionally send commands)

```bash
python tools/scripts/uart_session.py
# type a command and press Enter; 'quit' to exit
```

Custom port / output path:

```bash
python tools/scripts/uart_session.py \
    --port /dev/ttyACM0 --baud 115200 \
    --out measurements/session_$(date +%Y%m%d_%H%M%S).txt
```

To capture a clean baseline for steady-state stack/heap evidence, reset
the board first, then run a session with no commands for ~15 seconds and
save it as `measurements/phase5_baseline_<timestamp>.txt`.

### Run the fault matrix

```bash
python tools/scripts/run_fault_matrix.py
```

Runs three scenarios in order and writes one log each under
`measurements/`:

1. `HEAP_STRESS` (non-reset) — exhausts heap, confirms malloc-failed hook.
2. `TASK_SUSPEND` (IWDG reset) — triggers HealthMonitor timeout, then the
   watchdog resets the board.
3. `RESET` (software reset) — `NVIC_SystemReset`.

The fourth fault-matrix scenario, stack overflow, is not driven here. It
was verified in Phase 1 (extended) via a deliberate recursive overflow
that escalates to HardFault; its log (`fault_test_*.log`) is referenced
by the parser with `source = manual (Phase 1 extended)`.

### Parse logs to CSV

```bash
python tools/scripts/log_parser.py
```

Writes four CSV files to `measurements/csv/`. When multiple logs match a
pattern, the most recently modified one is used. Stack/heap prefer a
`phase5_baseline_*.txt` log when present and fall back to a
`phase5_task_suspend_*.txt` log otherwise.

## Known behavior: UART RX byte loss

The firmware receives commands one byte at a time via polling
(`HAL_UART_Receive`), and the STM32F446 USART has no hardware RX FIFO.
When the FaultInjector task is preempted, bytes arriving back-to-back at
115200 baud can be overwritten before they are read, so a command may be
received partially (for example, logged as `[FI] unknown cmd: T`) or
dropped entirely.

`uart_io.py` compensates on the host side with `send_and_confirm()`,
which retransmits a command until the expected firmware response is
observed (default 3 attempts). This is a host-side mitigation, not a
firmware fix.

The proper fix is a firmware change — UART RX via DMA with IDLE-line
detection — which is tracked separately as a Phase 6 (Bonus) item. The
measured byte loss is the motivation for that work; see the captured
evidence log referenced in the project Phase 6 notes.

## Output files

| Path | Description |
|---|---|
| `measurements/session_*.txt` | Interactive session capture. |
| `measurements/phase5_baseline_*.txt` | Steady-state capture for stack/heap. |
| `measurements/phase5_<scenario>_*.txt` | Per-scenario fault-injection logs. |
| `measurements/csv/stack_watermark.csv` | Per-sample task stack high-water marks (words). |
| `measurements/csv/heap.csv` | Per-sample free / min-ever heap (bytes). |
| `measurements/csv/latency.csv` | ISR-to-task latency samples. |
| `measurements/csv/fault_matrix.csv` | One row per fault scenario with detection evidence. |
