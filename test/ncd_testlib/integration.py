"""Integration test discovery, execution, and parsing for NCD."""

import json
import os
import platform
import re
import subprocess
from pathlib import Path

from .build import is_wsl_available, run_cmd, wsl_path

PROJECT_ROOT = Path(__file__).parent.parent.parent.resolve()
TEST_DIR = PROJECT_ROOT / "test"

# ---------------------------------------------------------------------------
# Integration suite definitions: (display_name, script_path, timeout_seconds)
# ---------------------------------------------------------------------------

WINDOWS_SUITES = [
    ("Service Tests (Isolated)", TEST_DIR / "Test-Service-Windows.bat", 60),
    ("NCD Standalone", TEST_DIR / "Test-NCD-Windows-Standalone.bat", 60),
    ("NCD with Service", TEST_DIR / "Test-NCD-Windows-With-Service.bat", 60),
    ("Windows Feature Tests", TEST_DIR / "Win" / "test_features.bat", 300),
    ("Windows Agent Command Tests", TEST_DIR / "Win" / "test_agent_commands.bat", 120),
    ("Service Race Tester", TEST_DIR / "Win" / "test_service_race.bat", 60),
]

WSL_SUITES = [
    ("WSL Service Tests", TEST_DIR / "test_service_wsl.sh", 60),
    ("WSL NCD Standalone", TEST_DIR / "test_ncd_wsl_standalone.sh", 60),
    ("WSL NCD with Service", TEST_DIR / "test_ncd_wsl_with_service.sh", 90),
    ("WSL Feature Tests", TEST_DIR / "Wsl" / "test_features.sh", 120),
    ("WSL Agent Command Tests", TEST_DIR / "Wsl" / "test_agent_commands.sh", 180),
    ("WSL Service Race Tester", TEST_DIR / "Wsl" / "test_service_race.sh", 180),
]


# ---------------------------------------------------------------------------
# Output parsing
# ---------------------------------------------------------------------------

def _extract_machine_summary(output: str):
    """Extract the JSON machine-parseable summary block if present."""
    m = re.search(
        r"=== MACHINE PARSEABLE SUMMARY ===\r?\n(.*?)\r?\n=== END MACHINE PARSEABLE SUMMARY ===",
        output,
        re.DOTALL,
    )
    if not m:
        return None
    try:
        return json.loads(m.group(1))
    except Exception:
        return None


def _last_search(pattern, text):
    """Return the last match of pattern in text, or None."""
    matches = list(re.finditer(pattern, text))
    return matches[-1] if matches else None


def parse_integration_output(output: str):
    """Extract pass/fail/skip/total counts from script output."""
    # Strip ANSI escape sequences so colour codes don't break regexes
    cleaned = re.sub(r"\x1b\[[0-9;]*m", "", output)

    passed = 0
    failed = 0
    skipped = 0
    total = 0

    # Use last match because wrapper scripts emit their summary at the end
    # of the output, after any nested tool output (e.g. service_race_tester).
    total_m = _last_search(r"(?mi)^\s*Total:\s*(\d+)", cleaned)
    passed_m = _last_search(r"(?mi)^\s*Passed:\s*(\d+)", cleaned)
    failed_m = _last_search(r"(?mi)^\s*Failed:\s*(\d+)", cleaned)
    skipped_m = _last_search(r"(?mi)^\s*Skipped:\s*(\d+)", cleaned)

    if total_m:
        total = int(total_m.group(1))
    if passed_m:
        passed = int(passed_m.group(1))
    if failed_m:
        failed = int(failed_m.group(1))
    if skipped_m:
        skipped = int(skipped_m.group(1))

    # Fallback: count individual pass/fail/skip markers
    if total == 0:
        passed = len(re.findall(r"^\s*\[PASS\]", cleaned, re.MULTILINE))
        failed = len(re.findall(r"^\s*\[FAIL\]", cleaned, re.MULTILINE))
        skipped = len(re.findall(r"^\s*\[SKIP\]", cleaned, re.MULTILINE))
        # Batch files use "PASS  ID  desc" and "FAIL  ID  desc"
        passed += len(re.findall(r"^\s*PASS\s+\S+", cleaned, re.MULTILINE))
        failed += len(re.findall(r"^\s*FAIL\s+\S+", cleaned, re.MULTILINE))
        skipped += len(re.findall(r"^\s*SKIP\s+\S+", cleaned, re.MULTILINE))
        total = passed + failed + skipped

    return passed, failed, skipped, total


def _format_status(passed, failed, skipped, total, raw_status):
    """Format a human-readable status string for test.results."""
    if raw_status == "MISSING":
        return "MISSING (script not found)"
    if raw_status == "SKIP":
        return "SKIP (WSL not available)"
    if raw_status == "TIMEOUT":
        parts = [f"{passed}/{total} passed"]
        return f"TIMEOUT ({', '.join(parts)})"

    parts = [f"{passed}/{total} passed"]
    if failed:
        parts.append(f"{failed} failed")
    if skipped:
        parts.append(f"{skipped} skipped")

    label = "PASS" if failed == 0 and raw_status != "FAIL" else "FAIL"
    return f"{label} ({', '.join(parts)})"


# ---------------------------------------------------------------------------
# Per-platform execution
# ---------------------------------------------------------------------------

def run_windows_suite(script_path: Path, timeout: int = 60):
    """Run a Windows integration test script and return a result dict."""
    # Best-effort cleanup of orphaned service processes before each suite
    try:
        subprocess.run(
            ["taskkill", "/F", "/IM", "NCDService.exe"],
            capture_output=True, timeout=10
        )
    except Exception:
        pass
    if not script_path.exists():
        return {
            "name": script_path.name,
            "passed": 0,
            "failed": 0,
            "skipped": 0,
            "total": 0,
            "status": "MISSING",
            "status_str": _format_status(0, 0, 0, 0, "MISSING"),
        }

    rc, out, err = run_cmd(
        ["cmd", "/c", str(script_path)], cwd=PROJECT_ROOT, timeout=timeout
    )
    output = out + err
    print(output)

    passed, failed, skipped, total = parse_integration_output(output)

    has_fail = re.search(r"RESULT:\s*FAILED", output, re.IGNORECASE) is not None
    has_pass = re.search(r"RESULT:\s*PASSED", output, re.IGNORECASE) is not None
    timed_out = (rc == -2)

    if timed_out:
        raw = "TIMEOUT"
        if failed == 0:
            failed = 1  # Timeout counts as a failure
    elif has_fail or failed > 0:
        raw = "FAIL"
    elif has_pass or (passed > 0 and total > 0):
        raw = "PASS"
    else:
        raw = "FAIL" if rc != 0 else "PASS"

    result = {
        "name": script_path.name,
        "passed": passed,
        "failed": failed,
        "skipped": skipped,
        "total": total,
        "status": raw,
        "status_str": _format_status(passed, failed, skipped, total, raw),
    }
    summary = _extract_machine_summary(output)
    if summary:
        result["machine_summary"] = summary
    return result


def run_wsl_suite(script_path: Path, timeout: int = 60):
    """Run a WSL integration test script and return a result dict."""
    # Best-effort cleanup of orphaned service processes before each suite
    try:
        subprocess.run(
            ["wsl", "bash", "-lc",
             "pkill -9 -f NCDService 2>/dev/null; pkill -9 -f ncd_service 2>/dev/null; rm -f /tmp/ncd_*_control.sock 2>/dev/null"],
            capture_output=True, timeout=10
        )
    except Exception:
        pass
    if not script_path.exists():
        return {
            "name": script_path.name,
            "passed": 0,
            "failed": 0,
            "skipped": 0,
            "total": 0,
            "status": "MISSING",
            "status_str": _format_status(0, 0, 0, 0, "MISSING"),
        }

    if not is_wsl_available():
        return {
            "name": script_path.name,
            "passed": 0,
            "failed": 0,
            "skipped": 0,
            "total": 0,
            "status": "SKIP",
            "status_str": _format_status(0, 0, 0, 0, "SKIP"),
        }

    wsl_script = wsl_path(script_path)
    wsl_proj = wsl_path(PROJECT_ROOT)

    # Translate isolation env vars to WSL paths so NCD writes into the
    # isolated temp directory instead of the user's real data location.
    env_setup = ""
    xdg = os.environ.get("XDG_DATA_HOME", "")
    if xdg:
        env_setup += f"export XDG_DATA_HOME='{wsl_path(xdg)}'; "
    localappdata = os.environ.get("LOCALAPPDATA", "")
    if localappdata:
        env_setup += f"export LOCALAPPDATA='{wsl_path(localappdata)}'; "
    temp = os.environ.get("TEMP", "")
    if temp:
        wsl_temp = wsl_path(temp)
        env_setup += f"export TEMP='{wsl_temp}'; export TMP='{wsl_temp}'; "

    ncd_test_mode = os.environ.get("NCD_TEST_MODE", "1")
    env_setup += f"export NCD_TEST_MODE='{ncd_test_mode}'; "
    ncd_ui_keys = os.environ.get("NCD_UI_KEYS", "")
    if ncd_ui_keys:
        env_setup += f"export NCD_UI_KEYS='{ncd_ui_keys}'; "
    ncd_ui_keys_strict = os.environ.get("NCD_UI_KEYS_STRICT", "")
    if ncd_ui_keys_strict:
        env_setup += f"export NCD_UI_KEYS_STRICT='{ncd_ui_keys_strict}'; "

    cmd = f'{env_setup}cd "{wsl_proj}" && bash "{wsl_script}"'
    rc, out, err = run_cmd(["wsl", "bash", "-c", cmd], timeout=timeout, stdin=subprocess.DEVNULL)
    output = out + err
    print(output)

    passed, failed, skipped, total = parse_integration_output(output)

    has_fail = re.search(r"RESULT:\s*FAILED", output, re.IGNORECASE) is not None
    has_pass = re.search(r"RESULT:\s*PASSED", output, re.IGNORECASE) is not None
    timed_out = (rc == -2)

    if timed_out:
        raw = "TIMEOUT"
        if failed == 0:
            failed = 1  # Timeout counts as a failure
    elif has_fail or failed > 0:
        raw = "FAIL"
    elif has_pass or (passed > 0 and total > 0):
        raw = "PASS"
    else:
        raw = "FAIL" if rc != 0 else "PASS"

    result = {
        "name": script_path.name,
        "passed": passed,
        "failed": failed,
        "skipped": skipped,
        "total": total,
        "status": raw,
        "status_str": _format_status(passed, failed, skipped, total, raw),
    }
    summary = _extract_machine_summary(output)
    if summary:
        result["machine_summary"] = summary
    return result


# ---------------------------------------------------------------------------
# Main entry point used by runner.py
# ---------------------------------------------------------------------------

def run_all_integration_suites(suite_filter=None, no_service=False, quick=False):
    """
    Run integration tests and return a list of result dicts.

    Each dict has keys:
        name, passed, failed, skipped, total, status, status_str
    """
    results = []
    system = platform.system()

    run_windows = system == "Windows" and suite_filter not in ("wsl",)
    run_wsl = (system == "Windows" and is_wsl_available()) or system != "Windows"
    if suite_filter == "windows":
        run_wsl = False
    if suite_filter in ("service", "ncd", "ncd-service"):
        run_wsl = False

    windows_to_run = list(WINDOWS_SUITES)
    wsl_to_run = list(WSL_SUITES)

    if no_service:
        windows_to_run = [s for s in windows_to_run if "Service" not in s[0]]
        wsl_to_run = [s for s in wsl_to_run if "Service" not in s[0]]

    if suite_filter == "service":
        windows_to_run = [s for s in windows_to_run if "Service" in s[0]]
        wsl_to_run = [s for s in wsl_to_run if "Service" in s[0]]
    elif suite_filter == "ncd":
        windows_to_run = [s for s in windows_to_run if "Standalone" in s[0]]
        wsl_to_run = [s for s in wsl_to_run if "Standalone" in s[0]]
    elif suite_filter == "ncd-service":
        windows_to_run = [s for s in windows_to_run if "with Service" in s[0]]
        wsl_to_run = [s for s in wsl_to_run if "with Service" in s[0]]

    if run_windows:
        print("\n## Integration Tests - Windows")
        print()
        for name, script, timeout in windows_to_run:
            print(f"### Running {name} ...")
            result = run_windows_suite(script, timeout=timeout)
            result["name"] = name
            results.append(result)
            print(f"  -> {name}: {result['status_str']}")

    if run_wsl:
        print("\n## Integration Tests - WSL/Linux")
        print()
        for name, script, timeout in wsl_to_run:
            print(f"### Running {name} ...")
            result = run_wsl_suite(script, timeout=timeout)
            result["name"] = name
            results.append(result)
            print(f"  -> {name}: {result['status_str']}")

    return results
