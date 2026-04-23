"""Test execution and output parsing."""

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


def run_test_binary(path, platform_label, timeout=60):
    """Run a single test binary and return raw output."""
    if platform_label == "linux" and platform.system() == "Windows":
        from .build import wsl_path
        wsl_p = wsl_path(path)
        binary_name = Path(path).name
        wsl_dir = wsl_path(path.parent)
        rc, out, err = run_cmd(
            ["wsl", "bash", "-c",
             f'cd "{wsl_dir}" && NCD_TEST_MODE=1 ./{binary_name}'],
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
