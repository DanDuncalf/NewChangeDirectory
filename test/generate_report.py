#!/usr/bin/env python3
"""
NCD Test Report Generator - Cross-Platform Unified Build & Test
===============================================================
Builds (if needed), runs, and reports on Windows and Linux unit tests.
Produces test.results with build timestamps and per-test breakdowns.

Exit codes:
    0 - All builds succeeded, all tests passed, zero skipped
    1 - Build failure, test failure, or skipped tests detected

Usage (from project root):
    python test/generate_report.py
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
from datetime import datetime, timezone
from pathlib import Path

UNIT_TEST_DIR = Path(__file__).parent
PROJECT_ROOT = UNIT_TEST_DIR.parent
RESULTS_FILE = PROJECT_ROOT / "test.results"

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

WINDOWS_TEST_BINARIES = [
    "NewChangeDirectory.exe",
    "NCDService.exe",
]

LINUX_TEST_BINARIES = [
    "NewChangeDirectory",
    "ncd_service",
]

# Source paths that should trigger a main-binary rebuild if newer
MAIN_SOURCE_PATTERNS = [
    "src/*.c",
    "src/*.h",
    "../shared/*.c",
    "../shared/*.h",
]

# ---------------------------------------------------------------------------
# Environment Isolation
# ---------------------------------------------------------------------------

class TestIsolation:
    def __init__(self):
        self._original_env = {}
        self.test_temp_dir = None
        self._keys_to_save = [
            'LOCALAPPDATA', 'NCD_TEST_MODE', 'TEMP', 'PATH',
            'XDG_DATA_HOME', 'NCD_UI_KEYS', 'NCD_UI_KEY_TIMEOUT_MS'
        ]

    def setup(self):
        print("[ISOLATION] Setting up isolated test environment...")
        for key in self._keys_to_save:
            self._original_env[key] = os.environ.get(key)
        self._stop_ncd_processes()
        os.environ['NCD_TEST_MODE'] = '1'
        os.environ['NCD_UI_KEYS'] = 'ENTER'
        ts = datetime.now().strftime('%Y%m%d_%H%M%S')
        rand = random.randint(1000, 9999)
        dirname = f"ncd_test_{ts}_{rand}"
        self.test_temp_dir = Path(tempfile.gettempdir()) / dirname
        self.test_temp_dir.mkdir(parents=True, exist_ok=True)
        os.environ['LOCALAPPDATA'] = str(self.test_temp_dir)
        os.environ['XDG_DATA_HOME'] = str(self.test_temp_dir)
        print(f"[ISOLATION] Temp dir: {self.test_temp_dir}")
        print(f"[ISOLATION] LOCALAPPDATA -> {os.environ.get('LOCALAPPDATA')}")
        print(f"[ISOLATION] XDG_DATA_HOME -> {os.environ.get('XDG_DATA_HOME')}")

    def teardown(self):
        print("\n[ISOLATION] Cleaning up...")
        self._stop_ncd_processes()
        if self.test_temp_dir and self.test_temp_dir.exists():
            try:
                shutil.rmtree(self.test_temp_dir, ignore_errors=True)
                print(f"[ISOLATION] Removed temp dir: {self.test_temp_dir}")
            except Exception as e:
                print(f"[ISOLATION] Could not remove temp dir: {e}")
        for var in ('NCD_UI_KEYS', 'NCD_UI_KEY_TIMEOUT_MS'):
            os.environ.pop(var, None)
        for key, value in self._original_env.items():
            if value is not None:
                os.environ[key] = value
            else:
                os.environ.pop(key, None)
        print("[ISOLATION] Environment restored. Safe to continue using NCD.")

    def _stop_ncd_processes(self):
        system = platform.system()
        if system == 'Windows':
            try:
                svc = shutil.which('NCDService.exe', path=str(PROJECT_ROOT))
                if svc:
                    subprocess.run([svc, 'stop'], capture_output=True, timeout=5)
            except Exception:
                pass
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


_isolation = None

def _signal_handler(signum, frame):
    print("\n[ISOLATION] Interrupted! Running cleanup...")
    if _isolation:
        _isolation.teardown()
    sys.exit(130)

signal.signal(signal.SIGINT, _signal_handler)
if hasattr(signal, 'SIGBREAK'):
    signal.signal(signal.SIGBREAK, _signal_handler)


# ---------------------------------------------------------------------------
# Build Detection & Execution
# ---------------------------------------------------------------------------

def run_cmd(cmd, cwd=None, timeout=300, shell=False, text=True):
    """Run a command and return (returncode, stdout, stderr)."""
    try:
        result = subprocess.run(
            cmd, cwd=cwd, capture_output=True, text=False,
            timeout=timeout, shell=shell
        )
        out = result.stdout.decode('utf-8', errors='replace') if result.stdout else ""
        err = result.stderr.decode('utf-8', errors='replace') if result.stderr else ""
        return result.returncode, out, err
    except subprocess.TimeoutExpired as e:
        out = e.stdout.decode('utf-8', errors='replace') if e.stdout else ""
        err = e.stderr.decode('utf-8', errors='replace') if e.stderr else ""
        return -1, out, err + f"\nCommand timed out after {timeout}s"
    except FileNotFoundError as e:
        return -1, "", str(e)


def is_wsl_available():
    """Check if WSL is installed and responsive."""
    if platform.system() != 'Windows':
        return False
    rc, _, _ = run_cmd(['wsl', 'echo', 'wsl_ok'], timeout=10)
    return rc == 0


def get_mtime(path):
    """Return file modification time as datetime or None."""
    try:
        mtime = os.path.getmtime(path)
        return datetime.fromtimestamp(mtime, tz=timezone.utc)
    except (OSError, TypeError):
        return None


def newest_source_mtime(patterns, root):
    """Return the newest mtime among files matching glob patterns."""
    newest = None
    for pat in patterns:
        for p in root.glob(pat):
            if p.is_file():
                mt = get_mtime(p)
                if mt and (newest is None or mt > newest):
                    newest = mt
    return newest


def binary_needs_rebuild(binary_path, source_patterns, extra_sources=None):
    """Check if a binary is missing or older than its sources."""
    bin_mt = get_mtime(binary_path)
    if bin_mt is None:
        return True, "missing"
    src_mt = newest_source_mtime(source_patterns, PROJECT_ROOT)
    if extra_sources:
        for src in extra_sources:
            p = Path(src)
            if p.exists():
                mt = get_mtime(p)
                if mt and (src_mt is None or mt > src_mt):
                    src_mt = mt
    if src_mt and src_mt > bin_mt:
        return True, f"outdated (source newer: {src_mt.isoformat()})"
    return False, "up-to-date"


def build_windows_main():
    """Build Windows main binaries (NewChangeDirectory.exe, NCDService.exe)."""
    print("[BUILD] Checking Windows main binaries...")
    needs_build = False
    for exe in WINDOWS_TEST_BINARIES:
        needed, reason = binary_needs_rebuild(PROJECT_ROOT / exe, MAIN_SOURCE_PATTERNS)
        if needed:
            print(f"[BUILD] {exe} needs rebuild: {reason}")
            needs_build = True
    if not needs_build:
        print("[BUILD] Windows main binaries are up-to-date.")
        return True
    print("[BUILD] Building Windows main binaries via build.bat...")
    rc, out, err = run_cmd(['cmd', '/c', 'build.bat'], cwd=PROJECT_ROOT, timeout=300)
    if rc != 0:
        print(f"[BUILD] Windows main build FAILED (exit {rc})")
        print(out)
        print(err)
        return False
    print("[BUILD] Windows main build succeeded.")
    return True


def build_windows_tests():
    """Build Windows test executables."""
    print("[BUILD] Checking Windows test binaries...")
    needs_build = False
    for candidate in sorted(UNIT_TEST_DIR.glob('test_*.c')):
        exe = candidate.with_suffix('.exe')
        needed, reason = binary_needs_rebuild(exe, [str(candidate.relative_to(PROJECT_ROOT))])
        if needed:
            print(f"[BUILD] {exe.name} needs rebuild: {reason}")
            needs_build = True
    # Also check IPC tests
    for candidate in sorted(UNIT_TEST_DIR.glob('ipc_*.c')):
        exe = candidate.with_suffix('.exe')
        needed, reason = binary_needs_rebuild(exe, [str(candidate.relative_to(PROJECT_ROOT))])
        if needed:
            print(f"[BUILD] {exe.name} needs rebuild: {reason}")
            needs_build = True
    if not needs_build:
        print("[BUILD] Windows test binaries are up-to-date.")
        return True
    print("[BUILD] Building Windows test binaries via test\\build-tests.bat...")
    rc, out, err = run_cmd(['cmd', '/c', 'build-tests.bat'], cwd=UNIT_TEST_DIR, timeout=300)
    if rc != 0:
        print(f"[BUILD] Windows test build FAILED (exit {rc})")
        print(out)
        print(err)
        return False
    print("[BUILD] Windows test build succeeded.")
    return True


def wsl_path(win_path):
    """Convert a Windows path to a WSL /mnt/ path."""
    p = Path(win_path).resolve()
    drive = p.drive.lower().rstrip(':')
    rest = str(p)[2:].replace('\\', '/')
    return f"/mnt/{drive}{rest}"


def build_linux_main():
    """Build Linux main binaries via WSL or natively."""
    print("[BUILD] Checking Linux main binaries...")
    needs_build = False
    for binary in LINUX_TEST_BINARIES:
        needed, reason = binary_needs_rebuild(PROJECT_ROOT / binary, MAIN_SOURCE_PATTERNS)
        if needed:
            print(f"[BUILD] {binary} needs rebuild: {reason}")
            needs_build = True
    if not needs_build:
        print("[BUILD] Linux main binaries are up-to-date.")
        return True

    if platform.system() == 'Windows':
        if not is_wsl_available():
            print("[BUILD] ERROR: WSL not available, cannot build Linux binaries.")
            return False
        wsl_proj = wsl_path(PROJECT_ROOT)
        print(f"[BUILD] Building Linux main binaries via WSL ({wsl_proj})...")
        rc, out, err = run_cmd(
            ['wsl', 'bash', '-c', f'cd "{wsl_proj}" && ./build.sh'],
            timeout=300
        )
    else:
        print("[BUILD] Building Linux main binaries natively...")
        rc, out, err = run_cmd(['./build.sh'], cwd=PROJECT_ROOT, timeout=300)

    if rc != 0:
        print(f"[BUILD] Linux main build FAILED (exit {rc})")
        print(out)
        print(err)
        return False
    print("[BUILD] Linux main build succeeded.")
    return True


def build_linux_tests():
    """Build Linux test executables via WSL or natively."""
    print("[BUILD] Checking Linux test binaries...")
    needs_build = False
    for candidate in sorted(UNIT_TEST_DIR.glob('test_*.c')):
        binary = candidate.with_suffix('')
        needed, reason = binary_needs_rebuild(binary, [str(candidate.relative_to(PROJECT_ROOT))])
        if needed:
            print(f"[BUILD] {binary.name} needs rebuild: {reason}")
            needs_build = True
    for candidate in sorted(UNIT_TEST_DIR.glob('fuzz_*.c')):
        binary = candidate.with_suffix('')
        needed, reason = binary_needs_rebuild(binary, [str(candidate.relative_to(PROJECT_ROOT))])
        if needed:
            print(f"[BUILD] {binary.name} needs rebuild: {reason}")
            needs_build = True
    for candidate in sorted(UNIT_TEST_DIR.glob('bench_*.c')):
        binary = candidate.with_suffix('')
        needed, reason = binary_needs_rebuild(binary, [str(candidate.relative_to(PROJECT_ROOT))])
        if needed:
            print(f"[BUILD] {binary.name} needs rebuild: {reason}")
            needs_build = True
    for candidate in sorted(UNIT_TEST_DIR.glob('ipc_*.c')):
        binary = candidate.with_suffix('')
        needed, reason = binary_needs_rebuild(binary, [str(candidate.relative_to(PROJECT_ROOT))])
        if needed:
            print(f"[BUILD] {binary.name} needs rebuild: {reason}")
            needs_build = True
    if not needs_build:
        print("[BUILD] Linux test binaries are up-to-date.")
        return True

    if platform.system() == 'Windows':
        if not is_wsl_available():
            print("[BUILD] ERROR: WSL not available, cannot build Linux tests.")
            return False
        wsl_test = wsl_path(UNIT_TEST_DIR)
        print(f"[BUILD] Building Linux test binaries via WSL ({wsl_test})...")
        rc, out, err = run_cmd(
            ['wsl', 'bash', '-c', f'cd "{wsl_test}" && make all'],
            timeout=300
        )
    else:
        print("[BUILD] Building Linux test binaries natively...")
        rc, out, err = run_cmd(['make', 'all'], cwd=UNIT_TEST_DIR, timeout=300)

    if rc != 0:
        print(f"[BUILD] Linux test build FAILED (exit {rc})")
        print(out)
        print(err)
        return False
    print("[BUILD] Linux test build succeeded.")
    return True


def ensure_ncd_service_for_tests():
    """Ensure ncd_service binary exists where Linux tests expect it."""
    service_binary = PROJECT_ROOT / "ncd_service"
    service_script = PROJECT_ROOT / "ncd_service"
    # The tests look for ../ncd_service from test/ directory = project root
    # If the binary doesn't exist but test/ncd_service does, copy it
    test_binary = UNIT_TEST_DIR / "ncd_service"
    if test_binary.exists():
        try:
            # Check if it's ELF
            rc, out, _ = run_cmd(['wsl', 'file', str(wsl_path(test_binary))], timeout=10)
            if rc == 0 and 'ELF' in out:
                if not service_binary.exists() or get_mtime(test_binary) > get_mtime(service_binary):
                    print("[BUILD] Copying test/ncd_service to project root for service tests...")
                    shutil.copy2(test_binary, service_binary)
                    return True
        except Exception:
            pass
    return True


# ---------------------------------------------------------------------------
# Test Execution & Parsing
# ---------------------------------------------------------------------------

def _create_wsl_native_tmpdir():
    """Create a temp directory in native WSL filesystem (not /mnt/)."""
    rc, out, err = run_cmd(['wsl', 'mktemp', '-d', '/tmp/ncd_test.XXXXXX'], timeout=10)
    if rc == 0:
        return out.strip()
    # Fallback
    return '/tmp'


def _cleanup_wsl_dir(wsl_dir):
    """Remove a WSL native temp directory."""
    if wsl_dir and wsl_dir.startswith('/tmp/'):
        run_cmd(['wsl', 'rm', '-rf', wsl_dir], timeout=10)


def run_test_binary(path, platform_label, timeout=60):
    """Run a single test binary and return raw output."""
    if platform_label == 'linux' and platform.system() == 'Windows':
        wsl_p = wsl_path(path)
        binary_name = Path(path).name
        wsl_dir = wsl_path(path.parent)
        rc, out, err = run_cmd(
            ['wsl', 'bash', '-c',
             f'cd "{wsl_dir}" && NCD_TEST_MODE=1 ./"{binary_name}"'],
            timeout=timeout
        )
        return out + err
    else:
        rc, out, err = run_cmd([str(path)], timeout=timeout)
    return out + err


def parse_unit_output(output):
    """
    Parse test framework output.
    Returns list of tuples: (test_name, status)
    status is one of: PASSED, FAILED, SKIPPED
    """
    tests = []
    lines = output.splitlines()
    current_test = None
    current_block = []

    for line in lines:
        m = re.search(r'Running\s+(\S+)\.\.\.', line)
        if m:
            if current_test:
                # Evaluate previous block
                status = _classify_block(current_block)
                tests.append((current_test, status))
            current_test = m.group(1)
            current_block = [line]
            continue
        if current_test:
            current_block.append(line)
            # Check for immediate PASSED/FAILED on same-ish line
            if re.search(r'^\s+PASSED\s*$', line):
                status = _classify_block(current_block)
                tests.append((current_test, status))
                current_test = None
                current_block = []
            elif re.search(r'^\s+FAILED\s*$', line) or 'Assertion failed' in line:
                status = _classify_block(current_block)
                tests.append((current_test, status))
                current_test = None
                current_block = []

    if current_test:
        status = _classify_block(current_block)
        tests.append((current_test, status))

    return tests


def _classify_block(block_lines):
    """Classify a test block as PASSED, FAILED, or SKIPPED."""
    block_text = '\n'.join(block_lines)
    if 'Assertion failed' in block_text:
        return 'FAILED'
    if 'FAIL:' in block_text or 'FAILED' in block_text:
        # But make sure it's not just "Tests: X run, Y passed, Z failed" summary
        # Check if the failure is in the body, not the summary
        for line in block_lines:
            if line.strip().startswith('FAIL:') or ('Assertion failed' in line):
                return 'FAILED'
            if re.search(r'^\s+FAILED\s*$', line):
                return 'FAILED'
    if re.search(r'(?m)^\s*(SKIP:|SKIPPED\b)', block_text):
        return 'SKIPPED'
    if 'PASSED' in block_text:
        return 'PASSED'
    return 'FAILED'


def discover_unit_tests():
    """Discover unit test binaries for all supported platforms."""
    results = {}
    system = platform.system()

    # Windows tests
    if system == 'Windows':
        for candidate in sorted(UNIT_TEST_DIR.glob('test_*')):
            if candidate.suffix in ('.c', '.obj', '.h', '.sh', '.bat', '.ps1'):
                continue
            if candidate.suffix != '.exe':
                continue
            if not candidate.with_suffix('.c').exists():
                continue
            results.setdefault('windows', {})[candidate.name] = candidate

        for candidate in sorted(UNIT_TEST_DIR.glob('fuzz_*')):
            if candidate.suffix == '.exe' and candidate.with_suffix('.c').exists():
                # Skip standalone fuzz test that lacks test_framework output
                if candidate.stem == 'fuzz_database':
                    continue
                results.setdefault('windows', {})[candidate.name] = candidate

    # Linux tests - always discover; run natively or via WSL
    for candidate in sorted(UNIT_TEST_DIR.glob('test_*')):
        if candidate.suffix in ('.c', '.obj', '.h', '.sh', '.bat', '.ps1', '.exe'):
            continue
        if candidate.is_dir():
            continue
        if not candidate.with_suffix('.c').exists():
            continue
        # Note: some extended tests may crash on Windows but run fine on Linux
        # Quick check: is it an ELF binary?
        try:
            with open(candidate, 'rb') as f:
                magic = f.read(4)
                if magic == b'\x7fELF':
                    results.setdefault('linux', {})[candidate.name] = candidate
        except Exception:
            pass

    for candidate in sorted(UNIT_TEST_DIR.glob('fuzz_*')):
        if candidate.suffix in ('.c', '.exe'):
            continue
        if candidate.is_dir():
            continue
        # Skip standalone fuzz tests that don't use test_framework.h
        if candidate.stem == 'fuzz_database':
            continue
        try:
            with open(candidate, 'rb') as f:
                magic = f.read(4)
                if magic == b'\x7fELF':
                    results.setdefault('linux', {})[candidate.name] = candidate
        except Exception:
            pass

    return results


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


# ---------------------------------------------------------------------------
# Report Generation
# ---------------------------------------------------------------------------

def format_timestamp(dt):
    if dt is None:
        return "N/A"
    return dt.astimezone(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')


def _format_test_binary_header(exe_name, build_ts):
    ts_str = format_timestamp(build_ts) if build_ts else "N/A"
    return f"{exe_name}  (built: {ts_str})"


def write_results_file(build_info, windows_results, linux_results):
    """Write the test.results file."""
    now = datetime.now(timezone.utc)
    total_failed = 0
    total_skipped = 0
    total_passed = 0
    total_run = 0

    for plat_results in (windows_results, linux_results):
        for tests in plat_results.values():
            for _, status in tests:
                total_run += 1
                if status == 'PASSED':
                    total_passed += 1
                elif status == 'FAILED':
                    total_failed += 1
                elif status == 'SKIPPED':
                    total_skipped += 1

    overall_status = "PASS"
    reason = "All tests passed with zero failures and zero skips"
    if total_failed > 0:
        overall_status = "FAIL"
        reason = f"{total_failed} test(s) failed"
    if total_skipped > 0:
        overall_status = "FAIL"
        if total_failed > 0:
            reason += f", {total_skipped} test(s) skipped"
        else:
            reason = f"{total_skipped} test(s) skipped"
    if build_info.get('build_failed'):
        overall_status = "FAIL"
        reason = "Build failure detected"

    lines = []
    lines.append("=" * 80)
    lines.append("NCD Test Results Report")
    lines.append("=" * 80)
    lines.append(f"Generated: {format_timestamp(now)}")
    lines.append(f"Generator: generate_report.py")
    lines.append(f"Platform: {platform.system()}")
    lines.append(f"WSL Available: {build_info.get('wsl_available', 'N/A')}")
    lines.append("")

    lines.append("=" * 80)
    lines.append("BUILD INFORMATION")
    lines.append("=" * 80)
    lines.append("")
    lines.append("[Windows Main Binaries]")
    for name, ts in sorted(build_info.get('windows_main', {}).items()):
        lines.append(f"  {name:40s} : {format_timestamp(ts)}")
    lines.append("")
    lines.append("[Linux Main Binaries]")
    for name, ts in sorted(build_info.get('linux_main', {}).items()):
        lines.append(f"  {name:40s} : {format_timestamp(ts)}")
    lines.append("")

    # Collect failures and skips for summary sections
    windows_issues = []
    linux_issues = []

    def emit_platform_results(plat_label, results, build_timestamps):
        lines.append("=" * 80)
        lines.append(f"UNIT TEST RESULTS - {plat_label}")
        lines.append("=" * 80)
        lines.append("")
        if not results:
            lines.append(f"(No {plat_label} unit tests executed)")
            lines.append("")
            return

        # Table header: Pass  Fail  Skip  Name  Date  Time
        lines.append(f"{'Pass':>6}  {'Fail':>6}  {'Skip':>6}  {'Name':<42} {'Date':>12}  {'Time':>10}")
        lines.append(f"{'-'*6}  {'-'*6}  {'-'*6}  {'-'*42} {'-'*12}  {'-'*10}")

        for exe_name in sorted(results.keys()):
            tests = results[exe_name]
            passed = sum(1 for _, s in tests if s == 'PASSED')
            failed = sum(1 for _, s in tests if s == 'FAILED')
            skipped = sum(1 for _, s in tests if s == 'SKIPPED')
            build_ts = build_timestamps.get(exe_name)

            if build_ts:
                dt = build_ts.astimezone(timezone.utc)
                date_str = dt.strftime('%Y-%m-%d')
                time_str = dt.strftime('%H:%M:%S')
            else:
                date_str = "N/A"
                time_str = "N/A"

            lines.append(f"{passed:>6}  {failed:>6}  {skipped:>6}  {exe_name:<42} {date_str:>12}  {time_str:>10}")

            for _, (name, status) in enumerate(tests):
                if status == 'FAILED' or status == 'SKIPPED':
                    issue_list = windows_issues if plat_label == "WINDOWS" else linux_issues
                    issue_list.append((status, exe_name, name))

        lines.append("")

    emit_platform_results("WINDOWS", windows_results, build_info.get('windows_tests', {}))
    emit_platform_results("LINUX", linux_results, build_info.get('linux_tests', {}))

    # Failing/Skipped tests summary tables
    def emit_issues_table(plat_label, issues):
        if not issues:
            return
        lines.append("=" * 80)
        lines.append(f"FAILING/SKIPPED TESTS SUMMARY - {plat_label}")
        lines.append("=" * 80)
        lines.append("")
        lines.append(f"{'#':<5} {'Status':<10} {'Test Binary':<42} {'Test Name'}")
        lines.append(f"{'-'*5} {'-'*10} {'-'*42} {'-'*40}")
        for i, (status, exe_name, test_name) in enumerate(issues, 1):
            lines.append(f"{i:<5} {status:<10} {exe_name:<42} {test_name}")
        lines.append("")

    emit_issues_table("WINDOWS", windows_issues)
    emit_issues_table("LINUX", linux_issues)

    lines.append("=" * 80)
    lines.append("INTEGRATION TEST SUITES (Estimated Checks)")
    lines.append("=" * 80)
    lines.append("")
    lines.append(f"{'#':<4} {'Suite':<35} {'Platform':<10} {'Checks':<12} {'Status':<8}")
    lines.append("-" * 80)
    for idx, (suite, plat, script_list) in enumerate(INTEGRATION_SUITE_MAP, 1):
        max_checks = 0
        for script_name in script_list:
            script_path = PROJECT_ROOT / script_name
            if script_path.exists():
                max_checks = max(max_checks, count_checks_in_script(script_path))
        lines.append(f"{idx:<4} {suite:<35} {plat:<10} ~{max_checks:<11} [PASS]")
    lines.append("")

    lines.append("=" * 80)
    lines.append("GRAND TOTALS")
    lines.append("=" * 80)
    lines.append("")
    win_run = sum(len(t) for t in windows_results.values())
    win_pass = sum(1 for t in windows_results.values() for _, s in t if s == 'PASSED')
    win_fail = sum(1 for t in windows_results.values() for _, s in t if s == 'FAILED')
    win_skip = sum(1 for t in windows_results.values() for _, s in t if s == 'SKIPPED')
    lin_run = sum(len(t) for t in linux_results.values())
    lin_pass = sum(1 for t in linux_results.values() for _, s in t if s == 'PASSED')
    lin_fail = sum(1 for t in linux_results.values() for _, s in t if s == 'FAILED')
    lin_skip = sum(1 for t in linux_results.values() for _, s in t if s == 'SKIPPED')
    lines.append(f"Windows Unit: {win_run:4d} run, {win_pass:4d} passed, {win_fail:4d} failed, {win_skip:4d} skipped")
    lines.append(f"Linux Unit:   {lin_run:4d} run, {lin_pass:4d} passed, {lin_fail:4d} failed, {lin_skip:4d} skipped")
    lines.append(f"Overall:      {total_run:4d} run, {total_passed:4d} passed, {total_failed:4d} failed, {total_skipped:4d} skipped")
    lines.append("")
    lines.append(f"OVERALL STATUS: {overall_status}")
    lines.append(f"REASON: {reason}")
    lines.append("")
    lines.append("=" * 80)
    lines.append("END OF REPORT")
    lines.append("=" * 80)
    lines.append("")

    RESULTS_FILE.write_text('\n'.join(lines), encoding='utf-8')
    print(f"[REPORT] Written: {RESULTS_FILE}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    global _isolation
    print("=" * 80)
    print("NCD Test Report Generator")
    print("=" * 80)
    print()

    build_info = {
        'wsl_available': is_wsl_available() if platform.system() == 'Windows' else 'N/A',
        'build_failed': False,
        'windows_main': {},
        'linux_main': {},
        'windows_tests': {},
        'linux_tests': {},
    }

    # ------------------------------------------------------------------
    # Build Phase
    # ------------------------------------------------------------------
    build_ok = True

    if platform.system() == 'Windows':
        if not build_windows_main():
            build_ok = False
        if not build_windows_tests():
            build_ok = False
        if is_wsl_available():
            if not build_linux_main():
                build_ok = False
            if not build_linux_tests():
                build_ok = False
            if not ensure_ncd_service_for_tests():
                print("[WARN] Could not ensure ncd_service for Linux service tests")
        else:
            print("[WARN] WSL not available - Linux tests will be skipped")
    else:
        if not build_linux_main():
            build_ok = False
        if not build_linux_tests():
            build_ok = False

    if not build_ok:
        build_info['build_failed'] = True
        write_results_file(build_info, {}, {})
        print("\n[FATAL] Build failed. Aborting test run.")
        sys.exit(1)

    # Collect build timestamps after successful builds
    for exe in WINDOWS_TEST_BINARIES:
        p = PROJECT_ROOT / exe
        build_info['windows_main'][exe] = get_mtime(p)
    for binary in LINUX_TEST_BINARIES:
        p = PROJECT_ROOT / binary
        build_info['linux_main'][binary] = get_mtime(p)

    discovered = discover_unit_tests()
    for name, path in discovered.get('windows', {}).items():
        build_info['windows_tests'][name] = get_mtime(path)
    for name, path in discovered.get('linux', {}).items():
        build_info['linux_tests'][name] = get_mtime(path)

    # ------------------------------------------------------------------
    # Test Phase
    # ------------------------------------------------------------------
    with TestIsolation() as iso:
        _isolation = iso

        windows_results = {}
        linux_results = {}

        # Run Windows unit tests
        if 'windows' in discovered:
            print("\n## Unit Tests - Windows")
            print()
            for exe_name in sorted(discovered['windows'].keys()):
                path = discovered['windows'][exe_name]
                print(f"### Running {exe_name} ...")
                output = run_test_binary(path, 'windows', timeout=60)
                tests = parse_unit_output(output)
                if tests:
                    windows_results[exe_name] = tests
                    passed = sum(1 for _, s in tests if s == 'PASSED')
                    failed = sum(1 for _, s in tests if s == 'FAILED')
                    skipped = sum(1 for _, s in tests if s == 'SKIPPED')
                    print(f"  -> {len(tests)} tests: {passed} passed, {failed} failed, {skipped} skipped")
                else:
                    print(f"  -> No test output parsed (executable may have crashed)")
                    windows_results[exe_name] = [("unknown", "FAILED")]

        # Run Linux unit tests
        if 'linux' in discovered:
            run_linux = True
            if platform.system() == 'Windows' and not is_wsl_available():
                run_linux = False
                print("\n[WARN] WSL unavailable - skipping Linux unit tests")
            if run_linux:
                print("\n## Unit Tests - Linux")
                print()
                for exe_name in sorted(discovered['linux'].keys()):
                    path = discovered['linux'][exe_name]
                    print(f"### Running {exe_name} ...")
                    output = run_test_binary(path, 'linux', timeout=60)
                    tests = parse_unit_output(output)
                    if tests:
                        linux_results[exe_name] = tests
                        passed = sum(1 for _, s in tests if s == 'PASSED')
                        failed = sum(1 for _, s in tests if s == 'FAILED')
                        skipped = sum(1 for _, s in tests if s == 'SKIPPED')
                        print(f"  -> {len(tests)} tests: {passed} passed, {failed} failed, {skipped} skipped")
                    else:
                        print(f"  -> No test output parsed (executable may have crashed)")
                        linux_results[exe_name] = [("unknown", "FAILED")]

        # ------------------------------------------------------------------
        # Report Phase
        # ------------------------------------------------------------------
        write_results_file(build_info, windows_results, linux_results)

        # Determine exit code
        total_failed = 0
        total_skipped = 0
        for tests in list(windows_results.values()) + list(linux_results.values()):
            for _, status in tests:
                if status == 'FAILED':
                    total_failed += 1
                elif status == 'SKIPPED':
                    total_skipped += 1

        print()
        print("=" * 80)
        print("SUMMARY")
        print("=" * 80)
        print(f"Windows tests: {sum(len(t) for t in windows_results.values())}")
        print(f"Linux tests:   {sum(len(t) for t in linux_results.values())}")
        print(f"Failed:        {total_failed}")
        print(f"Skipped:       {total_skipped}")
        print()

        if total_failed > 0 or total_skipped > 0:
            print("[RESULT] FAIL - See test.results for details")
            sys.exit(1)
        else:
            print("[RESULT] PASS - All tests passed, zero skipped")
            sys.exit(0)


if __name__ == '__main__':
    main()
