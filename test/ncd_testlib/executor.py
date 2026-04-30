"""Test execution and output parsing."""

import os
import platform
import re
import subprocess
from pathlib import Path


def run_cmd(cmd, cwd=None, timeout=300, shell=False):
    """Run a command and return (returncode, stdout, stderr)."""
    try:
        result = subprocess.run(
            cmd, cwd=cwd, capture_output=True, text=False,
            timeout=timeout, shell=shell
        )
        out = result.stdout.decode("utf-8", errors="replace") if result.stdout else ""
        err = result.stderr.decode("utf-8", errors="replace") if result.stderr else ""
        return result.returncode, out, err
    except subprocess.TimeoutExpired as e:
        out = e.stdout.decode("utf-8", errors="replace") if e.stdout else ""
        err = e.stderr.decode("utf-8", errors="replace") if e.stderr else ""
        return -1, out, err + f"\nCommand timed out after {timeout}s"
    except FileNotFoundError as e:
        return -1, "", str(e)


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
