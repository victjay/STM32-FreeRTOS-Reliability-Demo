#!/usr/bin/env python3
"""
uart_session.py — Interactive UART session for STM32 FreeRTOS demo.

Owns the serial port exclusively and handles both directions:
  - RX: background thread reads lines, prints to screen, and logs to file.
  - TX: main thread reads stdin commands and sends them with a newline.

Because the FaultInjector task polls the same UART port, only one process
may hold /dev/ttyACM0 at a time. This script is that single owner, so
minicom must not be running simultaneously.

Usage:
    python uart_session.py
    python uart_session.py --port /dev/ttyACM0 --baud 115200

Commands (typed at the prompt):
    TASK_SUSPEND   suspend ACQ task to trigger HealthMonitor timeout
    HEAP_STRESS    exhaust heap via pvPortMalloc loop
    RESET          NVIC_SystemReset
    quit / exit    stop the session (or Ctrl+C)
"""

import argparse
import sys
import threading
import time
from datetime import datetime

try:
    import serial
except ImportError:
    sys.exit("pyserial is not installed. Run: pip install pyserial")

DEFAULT_PORT = "/dev/ttyACM0"
DEFAULT_BAUD = 115200


def make_log_path() -> str:
    """Build a timestamped log file path under measurements/."""
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return f"measurements/session_{stamp}.txt"


def rx_loop(ser: serial.Serial, log_file, stop_event: threading.Event) -> None:
    """Read lines from the serial port until stop_event is set.

    Each line is printed to stdout and written to the log file.
    Decoding errors are replaced rather than raised, since fault
    scenarios may emit partial or garbled bytes around a reset.
    """
    while not stop_event.is_set():
        try:
            raw = ser.readline()
        except serial.SerialException as exc:
            # Port may drop during a device reset (IWDG / NVIC_SystemReset).
            print(f"\n[uart_session] serial error: {exc}", flush=True)
            stop_event.set()
            break

        if not raw:
            # readline timed out with no data; loop and check stop_event.
            continue

        line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
        if line:
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            print(line, flush=True)
            log_file.write(f"{timestamp} {line}\n")
            log_file.flush()


def main() -> int:
    parser = argparse.ArgumentParser(description="Interactive UART session")
    parser.add_argument("--port", default=DEFAULT_PORT, help="serial port")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD,
                        help="baud rate")
    parser.add_argument("--out", default=None,
                        help="log file path (default: measurements/session_*.txt)")
    args = parser.parse_args()

    log_path = args.out or make_log_path()

    try:
        # timeout=1 lets the RX thread check stop_event roughly every second.
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except serial.SerialException as exc:
        print(f"Failed to open {args.port}: {exc}", file=sys.stderr)
        print("Check the port path and that minicom is not running.",
              file=sys.stderr)
        return 1

    stop_event = threading.Event()

    try:
        log_file = open(log_path, "w", encoding="utf-8")
    except OSError as exc:
        ser.close()
        print(f"Failed to open log file {log_path}: {exc}", file=sys.stderr)
        return 1

    print(f"[uart_session] port={args.port} baud={args.baud}")
    print(f"[uart_session] logging to {log_path}")
    print("[uart_session] type a command and press Enter; 'quit' to exit.\n")

    rx_thread = threading.Thread(
        target=rx_loop, args=(ser, log_file, stop_event), daemon=True)
    rx_thread.start()

    try:
        while not stop_event.is_set():
            try:
                cmd = input()
            except EOFError:
                # stdin closed (e.g. piped input ended).
                break

            cmd = cmd.strip()
            if cmd.lower() in ("quit", "exit"):
                break
            if not cmd:
                continue

            # Firmware expects an ASCII command terminated by newline.
            ser.write((cmd + "\n").encode("ascii", errors="ignore"))
            log_file.write(
                f"{datetime.now().strftime('%H:%M:%S.%f')[:-3]} "
                f"[TX] {cmd}\n")
            log_file.flush()
    except KeyboardInterrupt:
        print("\n[uart_session] interrupted.", flush=True)
    finally:
        stop_event.set()
        # Give the RX thread a moment to exit its current readline.
        time.sleep(0.2)
        ser.close()
        log_file.close()
        print(f"[uart_session] saved: {log_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
