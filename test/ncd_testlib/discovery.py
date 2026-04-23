"""Discover unit test binaries for Windows and Linux."""

import platform
from pathlib import Path

UNIT_TEST_DIR = Path(__file__).parent.parent.resolve()


def discover_unit_tests():
    """Discover unit test binaries for all supported platforms."""
    results = {}
    system = platform.system()

    # Windows tests
    if system == "Windows":
        for candidate in sorted(UNIT_TEST_DIR.glob("test_*")):
            if candidate.suffix in (".c", ".obj", ".h", ".sh", ".bat", ".ps1"):
                continue
            if candidate.suffix != ".exe":
                continue
            if not candidate.with_suffix(".c").exists():
                continue
            results.setdefault("windows", {})[candidate.name] = candidate

        for candidate in sorted(UNIT_TEST_DIR.glob("fuzz_*")):
            if candidate.suffix == ".exe" and candidate.with_suffix(".c").exists():
                if candidate.stem == "fuzz_database":
                    continue
                results.setdefault("windows", {})[candidate.name] = candidate

    # Linux tests - always discover; run natively or via WSL
    for candidate in sorted(UNIT_TEST_DIR.glob("test_*")):
        if candidate.suffix in (".c", ".obj", ".h", ".sh", ".bat", ".ps1", ".exe"):
            continue
        if candidate.is_dir():
            continue
        if not candidate.with_suffix(".c").exists():
            continue
        try:
            with open(candidate, "rb") as f:
                magic = f.read(4)
                if magic == b"\x7fELF":
                    results.setdefault("linux", {})[candidate.name] = candidate
        except Exception:
            pass

    for candidate in sorted(UNIT_TEST_DIR.glob("fuzz_*")):
        if candidate.suffix in (".c", ".exe"):
            continue
        if candidate.is_dir():
            continue
        if candidate.stem == "fuzz_database":
            continue
        try:
            with open(candidate, "rb") as f:
                magic = f.read(4)
                if magic == b"\x7fELF":
                    results.setdefault("linux", {})[candidate.name] = candidate
        except Exception:
            pass

    return results
