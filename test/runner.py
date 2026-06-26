#!/usr/bin/env python3
"""
NCD Test Runner — Python-only build & test orchestration.
============================================================
Replaces Run-Tests-Safe.bat, Run-NcdTests.ps1, and generate_report.py internals.

Usage:
    python test/runner.py [suite] [options]

Suites:
    all             Run all tests (default)
    unit            Run only unit tests
    integration     Run only integration tests
    service         Run only service tests
    ncd             Run only NCD standalone tests
    ncd-service     Run only NCD with service tests
    windows         Run all Windows tests (skip WSL)
    wsl             Run only WSL tests

Options:
    --skip-build      Skip build phase
    --windows-only    Run only Windows tests (skip WSL)
    --wsl-only        Run only WSL tests (skip Windows)
    --no-service      Skip tests requiring service
    --quick           Skip fuzz/benchmark tests
    --check           Check environment only
    --repair          Repair corrupted environment
    --verbose         Verbose output
    --help            Show this help

Exit codes:
    0 - All tests passed, zero failures, zero skipped, zero timeouts
    1 - Build failure, test failure, skipped test, or timeout detected
"""

import argparse
import ctypes
import os
import platform
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

# Ensure test/ is on path so ncd_testlib can be imported
TEST_DIR = Path(__file__).parent.resolve()
PROJECT_ROOT = TEST_DIR.parent
sys.path.insert(0, str(TEST_DIR))

from ncd_testlib.env import TestIsolation
from ncd_testlib.build import (
    build_all,
    get_mtime,
    is_wsl_available,
    WINDOWS_MAIN_BINARIES,
    LINUX_MAIN_BINARIES,
    WINDOWS_ALT_SERVICE_BINARIES,
    LINUX_ALT_SERVICE_BINARIES,
)
from ncd_testlib.discovery import discover_unit_tests
from ncd_testlib.executor import run_test_binary, parse_unit_output
from ncd_testlib.integration import run_all_integration_suites
from ncd_testlib.platform_info import (
    get_host_env_summary,
    get_binary_arch,
    get_binary_version_info,
    is_native_execution,
)

RESULTS_FILE = PROJECT_ROOT / "test.results"


def is_shell_elevated():
    """Return True when the current shell has the privileges required by the suite."""
    if platform.system() == "Windows":
        try:
            return bool(ctypes.windll.shell32.IsUserAnAdmin())
        except Exception:
            return False
    geteuid = getattr(os, "geteuid", None)
    if geteuid is None:
        return False
    return geteuid() == 0


def suite_requires_wsl(args):
    """Return True when the selected suite expects Linux/WSL coverage on Windows."""
    if args.windows_only:
        return False
    return args.suite in ("all", "unit", "wsl")


def suite_requires_elevation(args):
    """Return True when the selected suite needs privileged test fixtures."""
    return args.suite != "unit"


def get_preflight_error(args):
    """Return a fatal preflight error string, or None when the suite can proceed."""
    if suite_requires_elevation(args) and not is_shell_elevated():
        return "Test runner must be launched from an elevated shell. Aborting before build and test execution."

    if platform.system() == "Windows" and suite_requires_wsl(args) and not is_wsl_available():
        return "WSL is required for the selected suite, but it is not available."

    return None


def format_timestamp(dt):
    if dt is None:
        return "N/A"
    return dt.astimezone(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')


def write_results_file(build_info, windows_results, linux_results, integration_results=None):
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

    # Accumulate integration test counts by platform
    win_int_pass = win_int_fail = win_int_skip = win_int_run = 0
    lin_int_pass = lin_int_fail = lin_int_skip = lin_int_run = 0
    if integration_results:
        for r in integration_results:
            if r['name'].startswith('WSL'):
                lin_int_run += r['total']
                lin_int_pass += r['passed']
                lin_int_fail += r['failed']
                lin_int_skip += r['skipped']
            else:
                win_int_run += r['total']
                win_int_pass += r['passed']
                win_int_fail += r['failed']
                win_int_skip += r['skipped']

    overall_status = "PASS"
    reason = "All tests passed with zero failures"
    if build_info.get('preflight_error'):
        overall_status = "FAIL"
        reason = build_info['preflight_error']
    elif total_failed > 0 or win_int_fail > 0 or lin_int_fail > 0:
        overall_status = "FAIL"
        reason = f"{total_failed + win_int_fail + lin_int_fail} test(s) failed"
    elif total_skipped > 0 or win_int_skip > 0 or lin_int_skip > 0:
        overall_status = "FAIL"
        reason = f"{total_skipped + win_int_skip + lin_int_skip} test(s) skipped (skipped = failure)"
    if build_info.get('build_failed'):
        overall_status = "FAIL"
        reason = "Build failure detected"

    lines = []
    lines.append("=" * 80)
    lines.append("NCD Test Results Report")
    lines.append("=" * 80)
    lines.append(f"Generated: {format_timestamp(now)}")
    lines.append(f"Generator: runner.py")
    lines.append(f"Platform: {platform.system()}")
    lines.append(f"WSL Available: {build_info.get('wsl_available', 'N/A')}")
    lines.append("")
    lines.append("-" * 80)
    lines.append("HOST ENVIRONMENTS")
    lines.append("-" * 80)
    for env_line in get_host_env_summary(build_info):
        lines.append(f"  {env_line}")
    lines.append("")

    lines.append("=" * 80)
    lines.append("BUILD INFORMATION")
    lines.append("=" * 80)
    lines.append("")
    lines.append("[Windows Main Binaries]")
    for name, ts in sorted(build_info.get('windows_main', {}).items()):
        arch = get_binary_arch(PROJECT_ROOT / "src" / "ncd" / name) if (PROJECT_ROOT / "src" / "ncd" / name).exists() else None
        ver = get_binary_version_info(PROJECT_ROOT / "src" / "ncd" / name) if (PROJECT_ROOT / "src" / "ncd" / name).exists() else None
        arch_str = f" [{arch}]" if arch else ""
        ver_str = f"  ({ver})" if ver else ""
        lines.append(f"  {name:40s} : {format_timestamp(ts)}{arch_str}{ver_str}")
    lines.append("")
    lines.append("[Linux Main Binaries]")
    for name, ts in sorted(build_info.get('linux_main', {}).items()):
        arch = get_binary_arch(PROJECT_ROOT / "src" / "ncd" / name) if (PROJECT_ROOT / "src" / "ncd" / name).exists() else None
        ver = get_binary_version_info(PROJECT_ROOT / "src" / "ncd" / name) if (PROJECT_ROOT / "src" / "ncd" / name).exists() else None
        arch_str = f" [{arch}]" if arch else ""
        ver_str = f"  ({ver})" if ver else ""
        lines.append(f"  {name:40s} : {format_timestamp(ts)}{arch_str}{ver_str}")
    lines.append("")

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

        lines.append(f"{'Pass':>6}  {'Fail':>6}  {'Skip':>6}  {'Name':<36} {'Arch':<8} {'Mode':<8} {'Date':>12}  {'Time':>10}")
        lines.append(f"{'-'*6}  {'-'*6}  {'-'*6}  {'-'*36} {'-'*8} {'-'*8} {'-'*12}  {'-'*10}")

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

            # Find binary path for architecture detection
            binary_path = None
            if plat_label == "WINDOWS":
                candidate = PROJECT_ROOT / "test" / exe_name
                if candidate.exists():
                    binary_path = candidate
            else:
                candidate = PROJECT_ROOT / "test" / exe_name
                if candidate.exists():
                    binary_path = candidate

            arch = get_binary_arch(binary_path) if binary_path else None
            arch_str = arch if arch else "unknown"
            native = is_native_execution(arch, plat_label.lower()) if arch else None
            mode_str = "native" if native else ("emulated" if native is False else "unknown")

            lines.append(f"{passed:>6}  {failed:>6}  {skipped:>6}  {exe_name:<36} {arch_str:<8} {mode_str:<8} {date_str:>12}  {time_str:>10}")

            for _, (name, status) in enumerate(tests):
                if status == 'FAILED' or status == 'SKIPPED':
                    issue_list = windows_issues if plat_label == "WINDOWS" else linux_issues
                    issue_list.append((status, exe_name, name))

        lines.append("")

    emit_platform_results("WINDOWS", windows_results, build_info.get('windows_tests', {}))
    emit_platform_results("LINUX", linux_results, build_info.get('linux_tests', {}))

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
    lines.append("INTEGRATION TEST SUITES")
    lines.append("=" * 80)
    lines.append("")
    if integration_results:
        for r in integration_results:
            lines.append(f"  {r['name']:<50s} {r['status_str']}")
            summary = r.get("machine_summary")
            if summary and "latency_by_category" in summary:
                lines.append("  Latency (us):")
                for cat, stats in summary["latency_by_category"].items():
                    if stats.get("samples", 0) > 0:
                        lines.append(
                            f"    {cat:16s} n={stats['samples']:4d} "
                            f"min={stats['min_us']:10.2f} max={stats['max_us']:10.2f} "
                            f"avg={stats['avg_us']:10.2f} p50={stats['p50_us']:10.2f} "
                            f"p95={stats['p95_us']:10.2f} p99={stats['p99_us']:10.2f}"
                        )
    else:
        lines.append("  (Integration tests not run)")
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
    if win_int_run > 0:
        lines.append(f"Windows Int:  {win_int_run:4d} run, {win_int_pass:4d} passed, {win_int_fail:4d} failed, {win_int_skip:4d} skipped")
    lines.append(f"Linux Unit:   {lin_run:4d} run, {lin_pass:4d} passed, {lin_fail:4d} failed, {lin_skip:4d} skipped")
    if lin_int_run > 0:
        lines.append(f"Linux Int:    {lin_int_run:4d} run, {lin_int_pass:4d} passed, {lin_int_fail:4d} failed, {lin_int_skip:4d} skipped")
    total_run_all = total_run + win_int_run + lin_int_run
    total_pass_all = total_passed + win_int_pass + lin_int_pass
    total_fail_all = total_failed + win_int_fail + lin_int_fail
    total_skip_all = total_skipped + win_int_skip + lin_int_skip
    lines.append(f"Overall:      {total_run_all:4d} run, {total_pass_all:4d} passed, {total_fail_all:4d} failed, {total_skip_all:4d} skipped")
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


def check_environment():
    """Check if the environment is clean."""
    issues = []
    if os.environ.get('LOCALAPPDATA', '').lower().replace('\\', '/').replace('//', '/').find('temp/ncd') != -1:
        issues.append(f"LOCALAPPDATA points to test temp: {os.environ.get('LOCALAPPDATA')}")
    if os.environ.get('NCD_TEST_MODE'):
        issues.append(f"NCD_TEST_MODE is set: {os.environ.get('NCD_TEST_MODE')}")

    # Check for orphaned test VHDs (Windows only)
    if platform.system() == "Windows":
        try:
            result = subprocess.run(
                [
                    "powershell", "-NoProfile", "-Command",
                    "(Get-Disk | Where-Object { $_.Location -match 'ncd_.*\\.vhdx' }).Count"
                ],
                capture_output=True, text=True
            )
            vhd_count = int(result.stdout.strip()) if result.stdout.strip().isdigit() else 0
            if vhd_count > 0:
                issues.append(f"Found {vhd_count} orphaned test VHD(s)")
        except Exception:
            pass

    print("=" * 80)
    print("Environment Check")
    print("=" * 80)
    print(f"  LOCALAPPDATA:    {os.environ.get('LOCALAPPDATA', 'N/A')}")
    print(f"  NCD_TEST_MODE:   {os.environ.get('NCD_TEST_MODE', 'N/A')}")
    print(f"  Is clean:        {len(issues) == 0}")
    if issues:
        print("\nIssues found:")
        for issue in issues:
            print(f"  - {issue}")
        return False
    print("\nEnvironment is clean.")
    return True


def repair_environment():
    """Repair a corrupted environment."""
    print("=" * 80)
    print("Repairing Environment")
    print("=" * 80)
    if platform.system() == "Windows":
        fixed = os.path.expandvars(r"%USERPROFILE%\AppData\Local")
        if os.environ.get('LOCALAPPDATA', '').lower().replace('\\', '/').find('temp/ncd') != -1:
            print(f"Fixing LOCALAPPDATA: {os.environ.get('LOCALAPPDATA')} -> {fixed}")
            os.environ['LOCALAPPDATA'] = fixed
    if os.environ.get('NCD_TEST_MODE'):
        print(f"Clearing NCD_TEST_MODE (was: {os.environ.get('NCD_TEST_MODE')})")
        os.environ.pop('NCD_TEST_MODE', None)

    # Clean up orphaned test VHDs (Windows only)
    if platform.system() == "Windows":
        print("Checking for orphaned test VHDs...")
        try:
            subprocess.run(
                [
                    "powershell", "-NoProfile", "-Command",
                    "Get-Disk | Where-Object { $_.Location -match 'ncd_.*\\.vhdx' } | "
                    "ForEach-Object { Dismount-DiskImage -ImagePath $_.Location -ErrorAction SilentlyContinue; "
                    "Remove-Item $_.Location -ErrorAction SilentlyContinue }"
                ],
                capture_output=True
            )
            result = subprocess.run(
                ["powershell", "-NoProfile", "-Command",
                 "(Get-Disk | Where-Object { $_.Location -match 'ncd_.*\\.vhdx' }).Count"],
                capture_output=True, text=True
            )
            remaining = int(result.stdout.strip()) if result.stdout.strip().isdigit() else 0
            if remaining == 0:
                print("  No orphaned test VHDs found.")
            else:
                print(f"  Warning: {remaining} orphaned VHD(s) could not be removed.")
        except Exception as e:
            print(f"  VHD cleanup failed: {e}")

    print("Environment repair complete.")
    return True


def run_unit_tests(discovered, windows_only=False, wsl_only=False, quick=False):
    """Run unit tests and return (windows_results, linux_results)."""
    windows_results = {}
    linux_results = {}

    if 'windows' in discovered and not wsl_only:
        print("\n## Unit Tests - Windows")
        print()
        for exe_name in sorted(discovered['windows'].keys()):
            if quick and (exe_name.startswith("fuzz_") or exe_name.startswith("bench_")):
                continue
            path = discovered['windows'][exe_name]
            print(f"### Running {exe_name} ...")
            TestIsolation._stop_ncd_processes()
            output = run_test_binary(path, 'windows', timeout=120)
            if output.strip():
                try:
                    print(output)
                except UnicodeEncodeError:
                    sys.stdout.buffer.write(output.encode('utf-8', errors='replace'))
                    sys.stdout.buffer.write(b'\n')
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

    if 'linux' in discovered and not windows_only:
        run_linux = True
        if platform.system() == 'Windows' and not is_wsl_available():
            run_linux = False
            print("\n[WARN] WSL unavailable - skipping Linux unit tests")
        if run_linux:
            print("\n## Unit Tests - Linux")
            print()
            for exe_name in sorted(discovered['linux'].keys()):
                if quick and (exe_name.startswith("fuzz_") or exe_name.startswith("bench_")):
                    continue
                path = discovered['linux'][exe_name]
                print(f"### Running {exe_name} ...")
                TestIsolation._stop_ncd_processes()
                # Service lifecycle tests need more time in isolated environments
                test_timeout = 180 if "service" in exe_name.lower() else 60
                output = run_test_binary(path, 'linux', timeout=test_timeout)
                if output.strip():
                    try:
                        print(output)
                    except UnicodeEncodeError:
                        sys.stdout.buffer.write(output.encode('utf-8', errors='replace'))
                        sys.stdout.buffer.write(b'\n')
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

    return windows_results, linux_results


def run_integration_tests(suite, no_service=False, quick=False):
    """Run integration tests via the unified integration test runner."""
    return run_all_integration_suites(suite_filter=suite, no_service=no_service, quick=quick)


def main():
    parser = argparse.ArgumentParser(
        description="NCD Test Runner — unified cross-platform build & test",
        usage="python test/runner.py [suite] [options]"
    )
    parser.add_argument(
        "suite", nargs="?", default="all",
        choices=["all", "unit", "integration", "service", "ncd", "ncd-service",
                 "windows", "wsl"],
        help="Test suite to run (default: all)"
    )
    parser.add_argument("--skip-build", action="store_true", help="Skip build phase")
    parser.add_argument("--windows-only", action="store_true", help="Run only Windows tests")
    parser.add_argument("--wsl-only", action="store_true", help="Run only WSL tests")
    parser.add_argument("--no-service", action="store_true", help="Skip tests requiring service")
    parser.add_argument("--quick", action="store_true", help="Skip fuzz/benchmark tests")
    parser.add_argument("--check", action="store_true", help="Check environment only")
    parser.add_argument("--repair", action="store_true", help="Repair corrupted environment")
    parser.add_argument("--verbose", action="store_true", help="Verbose output")
    args = parser.parse_args()

    if args.check:
        ok = check_environment()
        sys.exit(0 if ok else 1)

    if args.repair:
        repair_environment()
        sys.exit(0)

    print("=" * 80)
    print("NCD Test Runner")
    print("=" * 80)
    print()
    print(f"Test Suite:     {args.suite}")
    print(f"Skip Build:     {args.skip_build}")
    print(f"Windows Only:   {args.windows_only}")
    print(f"WSL Only:       {args.wsl_only}")
    print(f"No Service:     {args.no_service}")
    print(f"Quick Mode:     {args.quick}")
    print()

    preflight_error = get_preflight_error(args)
    if preflight_error:
        write_results_file(
            {
                "wsl_available": is_wsl_available() if platform.system() == "Windows" else "N/A",
                "build_failed": False,
                "preflight_error": preflight_error,
            },
            {},
            {},
        )
        print(f"[FATAL] {preflight_error}")
        sys.exit(1)

    # Build phase
    if not build_all(
        skip_build=args.skip_build,
        windows_only=args.windows_only,
        wsl_only=args.wsl_only,
    ):
        write_results_file({"build_failed": True}, {}, {})
        print("\n[FATAL] Build failed. Aborting test run.")
        sys.exit(1)

    build_info = {
        "wsl_available": is_wsl_available() if platform.system() == "Windows" else "N/A",
        "build_failed": False,
        "windows_main": {},
        "linux_main": {},
        "windows_tests": {},
        "linux_tests": {},
    }
    for exe in WINDOWS_MAIN_BINARIES:
        p = PROJECT_ROOT / exe
        build_info["windows_main"][exe] = get_mtime(p)
    for binary in LINUX_MAIN_BINARIES:
        p = PROJECT_ROOT / binary
        build_info["linux_main"][binary] = get_mtime(p)
    # Alt service binaries (for version compatibility testing)
    for exe in WINDOWS_ALT_SERVICE_BINARIES:
        p = PROJECT_ROOT / "test" / exe
        build_info["windows_main"][exe] = get_mtime(p)
    for binary in LINUX_ALT_SERVICE_BINARIES:
        p = PROJECT_ROOT / "test" / binary
        build_info["linux_main"][binary] = get_mtime(p)

    discovered = discover_unit_tests()
    for name, path in discovered.get("windows", {}).items():
        build_info["windows_tests"][name] = get_mtime(path)
    for name, path in discovered.get("linux", {}).items():
        build_info["linux_tests"][name] = get_mtime(path)

    integration_status = None

    with TestIsolation() as iso:
        # Register for signal cleanup
        from ncd_testlib import env as _env_mod
        _env_mod.register_isolation(iso)

        try:
            # Unit tests
            if args.suite in ("all", "unit", "windows"):
                windows_results, linux_results = run_unit_tests(
                    discovered,
                    windows_only=args.windows_only,
                    wsl_only=args.wsl_only,
                    quick=args.quick,
                )
            else:
                windows_results = {}
                linux_results = {}

            # Integration tests
            if args.suite in ("all", "integration", "service", "ncd", "ncd-service", "windows", "wsl"):
                integration_status = run_integration_tests(
                    args.suite,
                    no_service=args.no_service,
                    quick=args.quick,
                )

            write_results_file(build_info, windows_results, linux_results, integration_status)

            total_failed = 0
            total_skipped = 0
            for tests in list(windows_results.values()) + list(linux_results.values()):
                for _, status in tests:
                    if status == 'FAILED':
                        total_failed += 1
                    elif status == 'SKIPPED':
                        total_skipped += 1

            # Include integration failures in summary counts
            int_failed = 0
            int_skipped = 0
            if integration_status:
                for r in integration_status:
                    int_failed += r.get('failed', 0)
                    int_skipped += r.get('skipped', 0)

            total_failed += int_failed
            total_skipped += int_skipped

            print()
            print("=" * 80)
            print("SUMMARY")
            print("=" * 80)
            print(f"Windows tests: {sum(len(t) for t in windows_results.values())}")
            print(f"Linux tests:   {sum(len(t) for t in linux_results.values())}")
            print(f"Failed:        {total_failed}")
            print(f"Skipped:       {total_skipped}")
            print()

            if total_failed > 0:
                print("[RESULT] FAIL - See test.results for details")
                sys.exit(1)
            elif total_skipped > 0:
                print(f"[RESULT] FAIL - {total_skipped} tests skipped (skipped = failure)")
                sys.exit(1)
            else:
                print("[RESULT] PASS - All tests passed")
                sys.exit(0)
        finally:
            _env_mod.unregister_isolation(iso)


if __name__ == '__main__':
    main()
