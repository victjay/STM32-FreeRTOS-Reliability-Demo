#!/usr/bin/env python3
"""
log_parser.py — Offline parser: UART logs -> CSV evidence files.

Reads captured UART logs from measurements/ and produces four CSV files
under measurements/csv/. When several files match a pattern, the most
recently modified one is used.

Outputs:
  stack_watermark.csv  per-sample task stack high-water marks (words)
  heap.csv             per-sample free / min-ever heap (bytes)
  latency.csv          ISR-to-task latency samples (already CSV upstream)
  fault_matrix.csv     one row per fault scenario with detection evidence

Source selection (latest match of each glob):
  stack/heap   <- phase5_task_suspend_*.txt   (longest steady-state run)
  latency      <- latency_*.txt               (normalized + validated)
  fault matrix <- phase5_<scenario>_*.txt and fault_test_*.log

The stack-overflow fault-matrix row comes from a Phase 1 (extended) log,
not from a UART command, and is labelled accordingly.

This module performs no serial I/O and is safe to run any time.

Usage:
    python log_parser.py
    python log_parser.py --measurements measurements --out measurements/csv
"""

import argparse
import csv
import glob
import os
import re
import sys

# Source log glob patterns
GLOB_BASELINE = "phase5_baseline_*.txt"
GLOB_TASK_SUSPEND = "phase5_task_suspend_*.txt"
GLOB_HEAP_STRESS = "phase5_heap_stress_*.txt"
GLOB_SOFTWARE_RESET = "phase5_software_reset_*.txt"
GLOB_LATENCY = "latency_*.txt"
GLOB_STACK_OVERFLOW = "fault_test_*.log"

# Output CSV filenames
CSV_STACK_WATERMARK = "stack_watermark.csv"
CSV_HEAP = "heap.csv"
CSV_LATENCY = "latency.csv"
CSV_FAULT_MATRIX = "fault_matrix.csv"

# Lines logged by the firmware (timestamps prepended by the capture tool
# are tolerated by searching anywhere in the line).
RE_STACK = re.compile(
    r"\[MON\]\s+stack_hwm\s+acq=(\d+)\s+proc=(\d+)\s+mon=(\d+)")
RE_HEAP = re.compile(
    r"\[MON\]\s+heap\s+free=(\d+)\s+min_ever=(\d+)")
RE_FAULT_LATCH = re.compile(
    r"\[HM\]\s+FAULT latched\s+task id=(\d+)\s+elapsed=(\d+)ms\s+"
    r"timeout=(\d+)ms")
RE_RESET_CAUSE = re.compile(r"\[BOOT\]\s+reset cause:\s+(.+?)\s*(?:=|$)")
RE_HEAP_HOOK = re.compile(r"hook_count=(\d+)")
RE_HEAP_ZERO = re.compile(r"\[MON\]\s+heap\s+free=0\b")
# Phase 1 HardFault evidence (values appear as CFSR=0x..., HFSR=0x...).
RE_CFSR = re.compile(r"CFSR=(0x[0-9A-Fa-f]+)")
RE_HFSR = re.compile(r"HFSR=(0x[0-9A-Fa-f]+)")


def latest_match(measurements: str, pattern: str):
    """Return the most recently modified file matching pattern, or None."""
    matches = glob.glob(os.path.join(measurements, pattern))
    if not matches:
        return None
    return max(matches, key=os.path.getmtime)


def read_lines(path: str):
    """Read a log file as a list of lines (utf-8, errors replaced)."""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.readlines()


def parse_stack(measurements: str, out_dir: str) -> None:
    #src = latest_match(measurements, "phase5_task_suspend_*.txt")
    src = latest_match(measurements, GLOB_BASELINE)
    out_path = os.path.join(out_dir, "stack_watermark.csv")
    if not src:
        print("[stack] no source log found, skipping")
        return

    rows = []
    for i, line in enumerate(read_lines(src)):
        m = RE_STACK.search(line)
        if m:
            rows.append((i, int(m.group(1)), int(m.group(2)),
                         int(m.group(3))))

    with open(out_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["sample", "acq_words", "proc_words", "mon_words"])
        for n, (_, acq, proc, mon) in enumerate(rows):
            w.writerow([n, acq, proc, mon])

    print(f"[stack] {len(rows)} samples from {os.path.basename(src)} "
          f"-> {out_path}")


def parse_heap(measurements: str, out_dir: str) -> None:
    #src = latest_match(measurements, "phase5_task_suspend_*.txt")
    src = latest_match(measurements, GLOB_BASELINE)
    out_path = os.path.join(out_dir, "heap.csv")
    if not src:
        print("[heap] no source log found, skipping")
        return

    rows = []
    for line in read_lines(src):
        m = RE_HEAP.search(line)
        if m:
            rows.append((int(m.group(1)), int(m.group(2))))

    with open(out_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["sample", "free_bytes", "min_ever_bytes"])
        for n, (free, min_ever) in enumerate(rows):
            w.writerow([n, free, min_ever])

    print(f"[heap] {len(rows)} samples from {os.path.basename(src)} "
          f"-> {out_path}")


def parse_latency(measurements: str, out_dir: str) -> None:
    """Normalize the latency CSV log into latency.csv with validation.

    The firmware already emits CSV rows; this copies the latest log,
    keeping only the header and numeric data rows (dropping any stray
    boot/log lines that may have been captured alongside).
    """
    #src = latest_match(measurements, "latency_*.txt")
    src = latest_match(measurements, GLOB_LATENCY)
    out_path = os.path.join(out_dir, "latency.csv")
    if not src:
        print("[latency] no source log found, skipping")
        return

    header = None
    data_rows = []
    for line in read_lines(src):
        line = line.strip()
        if not line:
            continue
        # Strip any leading capture timestamp before the CSV content.
        # CSV content starts at the first token that looks like the header
        # ("seq,...") or a row of comma-separated integers/floats.
        idx = line.find("seq,")
        if idx != -1:
            header = line[idx:]
            continue
        # Data row: split on comma, accept if all fields are numeric.
        candidate = line.split(",")
        if len(candidate) >= 2 and all(
                _is_number(tok.strip()) for tok in candidate):
            data_rows.append([tok.strip() for tok in candidate])

    with open(out_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        if header:
            w.writerow(header.split(","))
        for row in data_rows:
            w.writerow(row)

    note = "" if header else " (warning: header 'seq,...' not found)"
    print(f"[latency] {len(data_rows)} rows from {os.path.basename(src)} "
          f"-> {out_path}{note}")


def _is_number(tok: str) -> bool:
    try:
        float(tok)
        return True
    except ValueError:
        return False


def parse_fault_matrix(measurements: str, out_dir: str) -> None:
    #out_path = os.path.join(out_dir, "fault_matrix.csv")
    out_path = os.path.join(out_dir, CSV_FAULT_MATRIX)
    rows = []

    # --- task_hang -------------------------------------------------------
    #src = latest_match(measurements, "phase5_task_suspend_*.txt")
    src = latest_match(measurements, GLOB_TASK_SUSPEND)
    if src:
        lines = read_lines(src)
        detection = "not found"
        reset_cause = "not found"
        for line in lines:
            m = RE_FAULT_LATCH.search(line)
            if m:
                detection = (f"HealthMonitor timeout: elapsed={m.group(2)}ms "
                             f"timeout={m.group(3)}ms")
            r = RE_RESET_CAUSE.search(line)
            if r and "IWDG" in r.group(1):
                reset_cause = r.group(1).strip()
        rows.append(["task_hang", "TASK_SUSPEND", detection,
                     "stop IWDG feed -> watchdog reset", reset_cause,
                     os.path.basename(src)])
    else:
        print("[fault_matrix] task_suspend log missing")

    # --- heap_exhaustion -------------------------------------------------
    #src = latest_match(measurements, "phase5_heap_stress_*.txt")
    src = latest_match(measurements, GLOB_HEAP_STRESS)
    if src:
        lines = read_lines(src)
        hook = "not found"
        heap_zero = "no"
        for line in lines:
            m = RE_HEAP_HOOK.search(line)
            if m:
                hook = f"malloc_failed_hook count={m.group(1)}"
            if RE_HEAP_ZERO.search(line):
                heap_zero = "yes"
        detection = f"{hook}; heap_free=0:{heap_zero}"
        rows.append(["heap_exhaustion", "HEAP_STRESS", detection,
                     "no local recovery (fault state retained)", "none",
                     os.path.basename(src)])
    else:
        print("[fault_matrix] heap_stress log missing")

    # --- software_reset --------------------------------------------------
    #src = latest_match(measurements, "phase5_software_reset_*.txt")
    src = latest_match(measurements, GLOB_SOFTWARE_RESET)
    if src:
        lines = read_lines(src)
        reset_cause = "not found"
        for line in lines:
            r = RE_RESET_CAUSE.search(line)
            if r and "software" in r.group(1).lower():
                reset_cause = r.group(1).strip()
        rows.append(["software_reset", "RESET",
                     "FI RESET command -> NVIC_SystemReset",
                     "controlled software reset", reset_cause,
                     os.path.basename(src)])
    else:
        print("[fault_matrix] software_reset log missing")

    # --- stack_overflow (Phase 1 extended, manual trigger) ---------------
    #src = latest_match(measurements, "fault_test_*.log")
    src = latest_match(measurements, GLOB_STACK_OVERFLOW)
    if src:
        text = "".join(read_lines(src))
        cfsr = RE_CFSR.search(text)
        hfsr = RE_HFSR.search(text)
        detection = "HardFault escalation"
        if cfsr:
            detection += f" CFSR={cfsr.group(1)}"
        if hfsr:
            detection += f" HFSR={hfsr.group(1)}"
        rows.append(["stack_overflow", "manual (Phase 1 extended)",
                     detection,
                     "naked HardFault handler -> NVIC_SystemReset",
                     "software reset (via NVIC)",
                     os.path.basename(src)])
    else:
        print("[fault_matrix] fault_test log missing "
              "(stack_overflow row omitted)")

    with open(out_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["scenario", "command", "detection_evidence",
                    "recovery_path", "reset_cause", "source_log"])
        w.writerows(rows)

    print(f"[fault_matrix] {len(rows)} scenarios -> {out_path}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Parse UART logs to CSV")
    parser.add_argument("--measurements", default="measurements",
                        help="directory containing log files")
    parser.add_argument("--out", default=None,
                        help="output dir (default: <measurements>/csv)")
    args = parser.parse_args()

    if not os.path.isdir(args.measurements):
        print(f"measurements dir not found: {args.measurements}",
              file=sys.stderr)
        return 1

    out_dir = args.out or os.path.join(args.measurements, "csv")
    os.makedirs(out_dir, exist_ok=True)

    parse_stack(args.measurements, out_dir)
    parse_heap(args.measurements, out_dir)
    parse_latency(args.measurements, out_dir)
    parse_fault_matrix(args.measurements, out_dir)

    print(f"\nDone. CSV files written to {out_dir}/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
