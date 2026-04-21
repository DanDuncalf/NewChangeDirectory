#!/usr/bin/env python3
"""
Run a long-running command (e.g. test suite) with real-time output streaming
and automatic stall detection.  If no new output appears for <stall_timeout>
seconds the process is considered hung and is killed.

Usage:
    python tools/run_with_monitor.py cmd /c "Run-Tests-Safe.bat"
    python tools/run_with_monitor.py --stall-timeout 240 cmd /c "Run-Tests-Safe.bat integration"
    python tools/run_with_monitor.py --tail-lines 50 python test/generate_report.py

The script streams stdout/stderr live so you can watch progress instead of
waiting blindly for a long timeout.
"""

import sys
import os
import time
import subprocess
import threading
import argparse
from datetime import datetime, timedelta


def log(msg):
    ts = datetime.now().strftime("%H:%M:%S")
    print(f"[{ts}] [monitor] {msg}", flush=True)


def run_with_monitor(cmd, stall_timeout=180, total_timeout=None, tail_lines=30, poll_interval=2.0):
    """
    Run *cmd* with real-time output and stall detection.

    Args:
        cmd: list of command arguments
        stall_timeout: seconds without new output before declaring a hang (default 180)
        total_timeout: maximum total runtime in seconds (None = no limit)
        tail_lines: number of recent lines to print on each progress report
        poll_interval: how often to check the output buffer (seconds)
    """
    log(f"Starting: {' '.join(cmd)}")
    log(f"Stall timeout: {stall_timeout}s | Total timeout: {total_timeout or 'unlimited'}")

    start_time = time.time()
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,          # line-buffered
        encoding='utf-8',
        errors='replace'
    )

    output_buffer = []
    last_activity = time.time()
    last_report = time.time()
    report_interval = 30.0   # print a progress snapshot every 30s
    bytes_seen = 0
    lines_seen = 0

    def read_stream():
        nonlocal last_activity, bytes_seen, lines_seen
        try:
            for line in proc.stdout:
                output_buffer.append(line)
                bytes_seen += len(line)
                lines_seen += 1
                last_activity = time.time()
                # Print live, but limit to avoid flooding on very chatty programs
                print(line, end='', flush=True)
        except Exception as e:
            log(f"Reader thread exception: {e}")

    reader = threading.Thread(target=read_stream, daemon=True)
    reader.start()

    try:
        while proc.poll() is None:
            now = time.time()
            elapsed = now - start_time
            idle = now - last_activity

            # Total timeout check
            if total_timeout and elapsed >= total_timeout:
                log(f"TOTAL TIMEOUT exceeded ({total_timeout}s). Killing process...")
                proc.kill()
                proc.wait()
                return -1, "total_timeout"

            # Stall detection
            if idle >= stall_timeout:
                log(f"STALL DETECTED — no output for {int(idle)}s (threshold {stall_timeout}s). Killing process...")
                proc.kill()
                proc.wait()
                return -1, "stall_timeout"

            # Periodic progress report (even if output is still flowing)
            if now - last_report >= report_interval:
                tail = output_buffer[-tail_lines:] if output_buffer else []
                log(f"Progress — elapsed: {int(elapsed)}s, idle: {int(idle)}s, lines: {lines_seen}")
                if tail:
                    print("-" * 40 + " tail " + "-" * 40)
                    for ln in tail:
                        print(ln, end='')
                    print("-" * 86)
                last_report = now

            time.sleep(poll_interval)

        # Process finished normally
        exit_code = proc.returncode
        log(f"Process exited with code {exit_code} after {int(time.time() - start_time)}s")
        return exit_code, "completed"

    except KeyboardInterrupt:
        log("Interrupted by user. Killing process...")
        proc.kill()
        proc.wait()
        return -1, "interrupted"
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
        reader.join(timeout=2)


def main():
    parser = argparse.ArgumentParser(
        description="Run a command with live output streaming and stall detection."
    )
    parser.add_argument(
        "--stall-timeout",
        type=int,
        default=180,
        help="Kill process if no new output for N seconds (default: 180)"
    )
    parser.add_argument(
        "--total-timeout",
        type=int,
        default=None,
        help="Hard maximum runtime in seconds (default: none)"
    )
    parser.add_argument(
        "--tail-lines",
        type=int,
        default=30,
        help="Number of tail lines to show in periodic reports (default: 30)"
    )
    parser.add_argument(
        "--poll-interval",
        type=float,
        default=2.0,
        help="Poll interval in seconds (default: 2.0)"
    )
    parser.add_argument(
        "cmd",
        nargs=argparse.REMAINDER,
        help="Command to run (prefix with -- if it starts with -)"
    )

    args = parser.parse_args()
    if not args.cmd:
        parser.print_help()
        sys.exit(1)

    # Strip leading '--' if user passed it to separate our args from the command
    if args.cmd[0] == '--':
        args.cmd = args.cmd[1:]

    code, reason = run_with_monitor(
        cmd=args.cmd,
        stall_timeout=args.stall_timeout,
        total_timeout=args.total_timeout,
        tail_lines=args.tail_lines,
        poll_interval=args.poll_interval
    )

    if reason == "stall_timeout":
        log("Exiting with STALL DETECTED status.")
        sys.exit(2)
    if reason == "total_timeout":
        log("Exiting with TOTAL TIMEOUT status.")
        sys.exit(3)
    sys.exit(code)


if __name__ == "__main__":
    main()
