#!/usr/bin/env python3
"""
NCD Test Report Generator - Cross-Platform Unified Build & Test
===============================================================
Builds (if needed), runs, and reports on Windows and Linux unit tests.
Produces test.results with build timestamps and per-test breakdowns.

This file is now a thin wrapper around test/runner.py for backward compatibility.
New code should call test/runner.py directly.

Exit codes:
    0 - All builds succeeded, all tests passed, zero skipped
    1 - Build failure, test failure, or skipped tests detected

Usage (from project root):
    python test/generate_report.py
    python test/runner.py
"""

import sys
from pathlib import Path

TEST_DIR = Path(__file__).parent.resolve()
sys.path.insert(0, str(TEST_DIR))

from runner import main

if __name__ == '__main__':
    main()
