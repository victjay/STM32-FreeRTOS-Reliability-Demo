#!/usr/bin/env python3
"""
uart_io.py — Shared UART I/O helpers for the STM32 FreeRTOS demo.

Provides a single owner of the serial port plus a send-and-confirm
primitive. The firmware FaultInjector receives commands one byte at a
time via polling and has no hardware RX FIFO, so bytes can be dropped
under task preemption. send_and_confirm() compensates by retransmitting
until an expected response pattern is observed.

This module is imported by run_fault_matrix.py. It is intentionally
small and dependency-light (pyserial only).
"""

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


def timestamp() -> str:
    """Return a millisecond-resolution wall-clock timestamp string."""
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


class UartSession:
    """Owns a serial port and continuously captures incoming lines.

    A background thread reads lines into a shared in-memory buffer and
    optionally writes them to a log file. The main thread can send
    commands and wait for expected responses against the same buffer.
    """

    def __init__(self, port: str = DEFAULT_PORT, baud: int = DEFAULT_BAUD,
                 log_file=None):
        # timeout=1 lets the RX thread re-check the stop flag every second.
        self._ser = serial.Serial(port, baud, timeout=1)
        self._log_file = log_file
        self._lines = []                 # captured (timestamp, text) tuples
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._rx_loop, daemon=True)
        self._thread.start()

    def _rx_loop(self) -> None:
        """Background reader: append decoded lines to the shared buffer."""
        while not self._stop.is_set():
            try:
                raw = self._ser.readline()
            except serial.SerialException:
                # Port may drop briefly during a device reset.
                self._stop.set()
                break
            if not raw:
                continue
            text = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            if not text:
                continue
            stamp = timestamp()
            with self._lock:
                self._lines.append((stamp, text))
            print(text, flush=True)
            if self._log_file:
                self._log_file.write(f"{stamp} {text}\n")
                self._log_file.flush()

    def _log_tx(self, cmd: str) -> None:
        if self._log_file:
            self._log_file.write(f"{timestamp()} [TX] {cmd}\n")
            self._log_file.flush()

    def send(self, cmd: str) -> None:
        """Send a single ASCII command terminated by a newline."""
        self._ser.write((cmd + "\n").encode("ascii", errors="ignore"))
        self._log_tx(cmd)

    def send_and_confirm(self, cmd: str, expect: str,
                         attempts: int = 3, wait_s: float = 2.0) -> bool:
        """Send cmd and wait for a line containing `expect`.

        Retransmits up to `attempts` times to tolerate dropped bytes on
        the polling-based firmware RX path. Returns True if the expected
        response appeared, False otherwise.
        """
        for attempt in range(1, attempts + 1):
            # Mark the current buffer length so we only inspect new lines.
            with self._lock:
                start_index = len(self._lines)

            self.send(cmd)
            deadline = time.time() + wait_s

            while time.time() < deadline:
                with self._lock:
                    recent = [t for _, t in self._lines[start_index:]]
                if any(expect in line for line in recent):
                    return True
                time.sleep(0.05)

            print(f"[uart_io] '{cmd}' attempt {attempt}/{attempts} "
                  f"got no '{expect}'", flush=True)

        return False

    def wait_for(self, pattern: str, wait_s: float = 6.0) -> bool:
        """Wait until a line containing `pattern` appears. No sending.

        Used to capture post-reset boot lines after a reset-inducing
        command (the device disappears and re-enumerates).
        """
        deadline = time.time() + wait_s
        with self._lock:
            start_index = len(self._lines)
        while time.time() < deadline:
            with self._lock:
                recent = [t for _, t in self._lines[start_index:]]
            if any(pattern in line for line in recent):
                return True
            time.sleep(0.05)
        return False

    def close(self) -> None:
        """Stop the reader thread and release the serial port."""
        self._stop.set()
        time.sleep(0.2)
        try:
            self._ser.close()
        except serial.SerialException:
            pass
