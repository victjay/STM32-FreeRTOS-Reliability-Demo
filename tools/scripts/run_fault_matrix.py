#!/usr/bin/env python3
"""
run_fault_matrix.py — Automated fault injection runner.

Executes the three UART-driven FaultInjector scenarios in sequence and
saves a separate log file per scenario under measurements/. Each command
uses send-and-confirm retransmission to tolerate dropped bytes on the
firmware's polling-based UART RX path.

The fourth fault-matrix scenario (stack overflow) is NOT driven here.
It was verified in Phase 1 (extended) via a deliberate recursive
overflow that escalates to HardFault, and its evidence log already
exists under measurements/. It is referenced manually in the README and
fault_matrix CSV with source = "Phase 1 extended, manual trigger".

Scenario order rationale:
  1. HEAP_STRESS first   — non-reset; confirms malloc-failed hook while
                           the system keeps running.
  2. TASK_SUSPEND second — triggers HealthMonitor timeout then IWDG
                           reset; we capture the post-reset boot lines.
  3. RESET last          — software NVIC_SystemReset; clean reboot.

TASK_SUSPEND and RESET both reset the target. On a NUCLEO board the
ST-LINK virtual COM port stays enumerated through a target reset, so the
same serial session continues to receive the [BOOT] lines.

Usage:
    python run_fault_matrix.py
    python run_fault_matrix.py --port /dev/ttyACM0 --baud 115200
"""

import argparse
import os
import sys
import time
from datetime import datetime

from uart_io import UartSession, DEFAULT_PORT, DEFAULT_BAUD

MEASUREMENTS_DIR = "measurements"

# Each scenario: (name, command, expected FI echo, post-reset boot pattern)
# boot_pattern = None means the scenario does not reset the device.
SCENARIOS = [
    ("heap_stress", "HEAP_STRESS", "[FI] HEAP_STRESS", None),
    ("task_suspend", "TASK_SUSPEND", "[FI] TASK_SUSPEND",
     "[BOOT] reset cause: IWDG watchdog"),
    ("software_reset", "RESET", "[FI] RESET",
     "[BOOT] reset cause: software reset"),
]


def run_scenario(port: str, baud: int, name: str, command: str,
                 expect: str, boot_pattern) -> bool:
    """Run one scenario in its own serial session and log file.

    A fresh session per scenario keeps each log self-contained and avoids
    cross-scenario noise. Returns True if the scenario reached its
    expected evidence, False otherwise.
    """
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_path = os.path.join(MEASUREMENTS_DIR, f"phase5_{name}_{stamp}.txt")

    print(f"\n===== scenario: {name} =====")
    print(f"log: {log_path}")

    try:
        log_file = open(log_path, "w", encoding="utf-8")
    except OSError as exc:
        print(f"failed to open log file: {exc}", file=sys.stderr)
        return False

    session = None
    ok = False
    try:
        session = UartSession(port, baud, log_file=log_file)

        # Let a few baseline lines accumulate before injecting the fault.
        time.sleep(2.0)

        confirmed = session.send_and_confirm(command, expect)
        if not confirmed:
            print(f"[{name}] command not confirmed after retries")
            return False

        if boot_pattern is None:
            # Non-reset scenario: capture a little aftermath then finish.
            time.sleep(3.0)
            ok = True
        else:
            # Reset scenario: wait for the device to reboot and report.
            print(f"[{name}] waiting for reset/boot evidence...")
            ok = session.wait_for(boot_pattern, wait_s=10.0)
            if ok:
                # Capture a couple of post-boot lines for context.
                time.sleep(2.0)
            else:
                print(f"[{name}] boot pattern '{boot_pattern}' not seen")
    finally:
        if session:
            session.close()
        log_file.close()
        print(f"[{name}] saved: {log_path}  result={'OK' if ok else 'FAIL'}")

    return ok


def main() -> int:
    parser = argparse.ArgumentParser(description="Fault matrix runner")
    parser.add_argument("--port", default=DEFAULT_PORT, help="serial port")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD,
                        help="baud rate")
    args = parser.parse_args()

    os.makedirs(MEASUREMENTS_DIR, exist_ok=True)

    results = {}
    for name, command, expect, boot_pattern in SCENARIOS:
        results[name] = run_scenario(
            args.port, args.baud, name, command, expect, boot_pattern)
        # Brief gap so the device settles before the next scenario.
        time.sleep(2.0)

    print("\n===== summary =====")
    for name, ok in results.items():
        print(f"  {name:16s} {'OK' if ok else 'FAIL'}")
    print("  stack_overflow   SKIPPED (see Phase 1 extended log)")

    # Exit non-zero if any scenario failed, for scripting convenience.
    return 0 if all(results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
