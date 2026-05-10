"""Test execution and output parsing."""

import os
import platform
import re
from pathlib import Path

from .build import run_cmd


def _cleanup_linux_service_state(path):
    """Best-effort cleanup between Linux test binaries when running under WSL."""
    from .build import wsl_path

    wsl_dir = wsl_path(path.parent)
    cleanup_cmd = (
        f'cd "{wsl_dir}" && '
        'pkill -9 -x NCDService 2>/dev/null; '
        'killall -9 NCDService 2>/dev/null; '
        'rm -f "${XDG_RUNTIME_DIR:-/tmp}/ncd_service.pid" 2>/dev/null; '
        'rm -f "${XDG_RUNTIME_DIR:-/tmp}"/ncd_*_control.sock 2>/dev/null'
    )
    run_cmd(["wsl", "bash", "-lc", cleanup_cmd], timeout=15)


def run_test_binary(path, platform_label, timeout=60):
    """Run a single test binary and return raw output."""
    if platform_label == "linux" and platform.system() == "Windows":
        from .build import wsl_path
        wsl_p = wsl_path(path)
        binary_name = Path(path).name
        wsl_dir = wsl_path(path.parent)
        _cleanup_linux_service_state(path)

        # WSL inherits Windows environment variables, but XDG_DATA_HOME
        # set to a Windows path (C:\...) is invalid inside Linux binaries.
        # Translate isolation paths to WSL /mnt/ paths so the service
        # can actually write its metadata and database files.
        env_setup = ""
        xdg = os.environ.get("XDG_DATA_HOME", "")
        if xdg:
            wsl_xdg = wsl_path(xdg)
            env_setup += f"export XDG_DATA_HOME='{wsl_xdg}'; "
        localappdata = os.environ.get("LOCALAPPDATA", "")
        if localappdata:
            wsl_local = wsl_path(localappdata)
            env_setup += f"export LOCALAPPDATA='{wsl_local}'; "
        temp = os.environ.get("TEMP", "")
        if temp:
            wsl_temp = wsl_path(temp)
            env_setup += f"export TEMP='{wsl_temp}'; export TMP='{wsl_temp}'; "

        rc, out, err = run_cmd(
            ["wsl", "bash", "-c",
             f'{env_setup}cd "{wsl_dir}" && NCD_TEST_MODE=1 ./{binary_name}'],
            timeout=timeout
        )
        _cleanup_linux_service_state(path)
        return out + err
    else:
        env = dict(os.environ)
        env.setdefault("NCD_TEST_MODE", "1")
        rc, out, err = run_cmd([str(path)], timeout=timeout, env=env)
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
                status = _classify_block(current_block)
                tests.append((current_test, status))
            current_test = m.group(1)
            current_block = [line]
            continue
        if current_test:
            current_block.append(line)
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
            elif re.search(r'^\s+SKIPPED\s*$', line):
                status = _classify_block(current_block)
                tests.append((current_test, status))
                current_test = None
                current_block = []

    if current_test:
        status = _classify_block(current_block)
        tests.append((current_test, status))

    return tests


def _classify_block(block_lines):
    """Classify a test block as PASSED, FAILED, or SKIPPED.

    Detection order (first match wins):
    1. Structured skip marker: === SKIP <reason> ===  (from SKIP_TEST macro)
    2. Legacy skip pattern: SKIP: or SKIPPED in block text
    3. Assertion failed or FAILED markers
    4. PASSED marker
    5. Fallback: FAILED
    """
    block_text = '\n'.join(block_lines)

    # Structured skip marker (Phase 1 SKIP_TEST macro)
    if re.search(r'^=== SKIP .+ ===$', block_text, re.MULTILINE):
        return 'SKIPPED'

    # Legacy skip patterns (pre-Phase 1 ad-hoc skips)
    if re.search(r'(?m)^\s*(SKIP:|SKIPPED\b)', block_text):
        return 'SKIPPED'

    # Failure detection
    if 'Assertion failed' in block_text:
        return 'FAILED'
    if 'FAIL:' in block_text or 'FAILED' in block_text:
        for line in block_lines:
            if line.strip().startswith('FAIL:') or ('Assertion failed' in line):
                return 'FAILED'
            if re.search(r'^\s+FAILED\s*$', line):
                return 'FAILED'

    # Pass detection
    if 'PASSED' in block_text:
        return 'PASSED'

    return 'FAILED'
