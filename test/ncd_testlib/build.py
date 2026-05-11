"""Build detection and execution for Windows and Linux test binaries."""

import os
import platform
import subprocess
import threading
from datetime import datetime, timezone
from pathlib import Path

PROJECT_ROOT = Path(__file__).parent.parent.parent.resolve()
UNIT_TEST_DIR = PROJECT_ROOT / "test"

MAIN_SOURCE_PATTERNS = [
    "src/*.c",
    "src/*.h",
    "../shared/*.c",
    "../shared/*.h",
]

WINDOWS_MAIN_BINARIES = ["NewChangeDirectory.exe", "NCDService.exe"]
LINUX_MAIN_BINARIES = ["NewChangeDirectory", "NCDService"]


def run_cmd(cmd, cwd=None, timeout=300, shell=False, stdin=None, env=None):
    """Run a command and return (returncode, stdout, stderr).

    Return codes:
        >=0  - Normal process exit code.
        -1   - Launch error (e.g. FileNotFoundError).
        -2   - Timed out (process was still running when the timeout expired).
    """
    try:
        proc = subprocess.Popen(
            cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            shell=shell, stdin=stdin, env=env
        )
    except FileNotFoundError as e:
        return -1, "", str(e)

    stdout = [b""]
    stderr = [b""]
    exc = [None]

    def _communicate():
        try:
            stdout[0], stderr[0] = proc.communicate()
        except Exception as e:
            exc[0] = e

    comm_thread = threading.Thread(target=_communicate)
    comm_thread.start()
    comm_thread.join(timeout)

    if comm_thread.is_alive():
        # The process is still running after the timeout.
        proc.kill()
        comm_thread.join(timeout=10)
        out = stdout[0].decode("utf-8", errors="replace") if stdout[0] else ""
        err = stderr[0].decode("utf-8", errors="replace") if stderr[0] else ""
        return -2, out, err + f"\nCommand timed out after {timeout}s"

    if exc[0] is not None:
        raise exc[0]

    out = stdout[0].decode("utf-8", errors="replace") if stdout[0] else ""
    err = stderr[0].decode("utf-8", errors="replace") if stderr[0] else ""
    return proc.returncode, out, err


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


def is_wsl_available():
    """Check if WSL is installed and responsive."""
    if platform.system() != "Windows":
        return False
    rc, _, _ = run_cmd(["wsl", "echo", "wsl_ok"], timeout=10)
    return rc == 0


def wsl_path(win_path):
    """Convert a Windows path to a WSL /mnt/ path."""
    p = Path(win_path).resolve()
    drive = p.drive.lower().rstrip(":")
    rest = str(p)[2:].replace("\\", "/")
    return f"/mnt/{drive}{rest}"


def build_windows_main():
    """Build Windows main binaries (NewChangeDirectory.exe, NCDService.exe)."""
    print("[BUILD] Checking Windows main binaries...")
    needs_build = False
    for exe in WINDOWS_MAIN_BINARIES:
        needed, reason = binary_needs_rebuild(PROJECT_ROOT / exe, MAIN_SOURCE_PATTERNS)
        if needed:
            print(f"[BUILD] {exe} needs rebuild: {reason}")
            needs_build = True
    if not needs_build:
        print("[BUILD] Windows main binaries are up-to-date.")
        return True
    print("[BUILD] Building Windows main binaries via build.bat...")
    rc, out, err = run_cmd(["cmd", "/c", "build.bat"], cwd=PROJECT_ROOT, timeout=300)
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
    test_source_patterns = [str(p.relative_to(PROJECT_ROOT)) for p in UNIT_TEST_DIR.glob("*.c")]
    all_dep_patterns = MAIN_SOURCE_PATTERNS + test_source_patterns
    needs_build = False
    for candidate in sorted(UNIT_TEST_DIR.glob("test_*.c")):
        exe = candidate.with_suffix(".exe")
        if not exe.exists():
            continue
        needed, reason = binary_needs_rebuild(exe, all_dep_patterns)
        if needed:
            print(f"[BUILD] {exe.name} needs rebuild: {reason}")
            needs_build = True
    for candidate in sorted(UNIT_TEST_DIR.glob("ipc_*.c")):
        exe = candidate.with_suffix(".exe")
        if not exe.exists():
            continue
        needed, reason = binary_needs_rebuild(exe, all_dep_patterns)
        if needed:
            print(f"[BUILD] {exe.name} needs rebuild: {reason}")
            needs_build = True
    for candidate in sorted(UNIT_TEST_DIR.glob("fuzz_*.c")):
        exe = candidate.with_suffix(".exe")
        if not exe.exists():
            continue
        needed, reason = binary_needs_rebuild(exe, all_dep_patterns)
        if needed:
            print(f"[BUILD] {exe.name} needs rebuild: {reason}")
            needs_build = True
    if not needs_build:
        print("[BUILD] Windows test binaries are up-to-date.")
        return True
    print("[BUILD] Building Windows test binaries via test\\build-tests.bat...")
    rc, out, err = run_cmd(["cmd", "/c", "build-tests.bat"], cwd=UNIT_TEST_DIR, timeout=300)
    if rc != 0:
        print(f"[BUILD] Windows test build FAILED (exit {rc})")
        print(out)
        print(err)
        return False
    print("[BUILD] Windows test build succeeded.")
    return True


def build_linux_main(test_build=False):
    """Build Linux main binaries via WSL or natively.

    Args:
        test_build: When True, build with -DNCD_TEST_BUILD so the stdio TUI
                    test backend is available for headless feature tests.
    """
    print("[BUILD] Checking Linux main binaries...")
    needs_build = False
    
    # When test_build=True, always rebuild to ensure -DNCD_TEST_BUILD is active.
    # A release build could otherwise be cached and used, breaking TUI tests.
    if test_build:
        needs_build = True
        print("[BUILD] Test build requested - forcing rebuild for NCD_TEST_BUILD")
    else:
        for binary in LINUX_MAIN_BINARIES:
            needed, reason = binary_needs_rebuild(PROJECT_ROOT / binary, MAIN_SOURCE_PATTERNS)
            if needed:
                print(f"[BUILD] {binary} needs rebuild: {reason}")
                needs_build = True
    if not needs_build:
        print("[BUILD] Linux main binaries are up-to-date.")
        return True

    build_cmd = "./build.sh test" if test_build else "./build.sh"
    if platform.system() == "Windows":
        if not is_wsl_available():
            print("[BUILD] ERROR: WSL not available, cannot build Linux binaries.")
            return False
        wsl_proj = wsl_path(PROJECT_ROOT)
        print(f"[BUILD] Building Linux main binaries via WSL ({wsl_proj})...")
        rc, out, err = run_cmd(
            ["wsl", "bash", "-c", f'cd "{wsl_proj}" && {build_cmd}'],
            timeout=300
        )
    else:
        print(f"[BUILD] Building Linux main binaries natively ({build_cmd})...")
        rc, out, err = run_cmd([build_cmd], cwd=PROJECT_ROOT, timeout=300)

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
    test_source_patterns = [str(p.relative_to(PROJECT_ROOT)) for p in UNIT_TEST_DIR.glob("*.c")]
    all_dep_patterns = MAIN_SOURCE_PATTERNS + test_source_patterns
    needs_build = False
    for pattern in ("test_*.c", "fuzz_*.c", "bench_*.c", "ipc_*.c"):
        for candidate in sorted(UNIT_TEST_DIR.glob(pattern)):
            binary = candidate.with_suffix("")
            if not binary.exists():
                continue
            needed, reason = binary_needs_rebuild(binary, all_dep_patterns)
            if needed:
                print(f"[BUILD] {binary.name} needs rebuild: {reason}")
                needs_build = True
    if not needs_build:
        print("[BUILD] Linux test binaries are up-to-date.")
        return True

    if platform.system() == "Windows":
        if not is_wsl_available():
            print("[BUILD] ERROR: WSL not available, cannot build Linux tests.")
            return False
        wsl_test = wsl_path(UNIT_TEST_DIR)
        print(f"[BUILD] Building Linux test binaries via WSL ({wsl_test})...")
        rc, out, err = run_cmd(
            ["wsl", "bash", "-c", f'cd "{wsl_test}" && make all'],
            timeout=300
        )
    else:
        print("[BUILD] Building Linux test binaries natively...")
        rc, out, err = run_cmd(["make", "all"], cwd=UNIT_TEST_DIR, timeout=300)

    if rc != 0:
        print(f"[BUILD] Linux test build FAILED (exit {rc})")
        print(out)
        print(err)
        return False
    print("[BUILD] Linux test build succeeded.")
    return True


def ensure_ncd_service_for_tests():
    """Ensure Linux service launcher scripts exist where tests expect them."""
    required_paths = [
        PROJECT_ROOT / "ncd_service",
        UNIT_TEST_DIR / "ncd_service",
    ]
    missing = [str(path) for path in required_paths if not path.exists()]
    if missing:
        print("[BUILD] Missing Linux service launcher(s):")
        for path in missing:
            print(f"[BUILD]   {path}")
        return False
    return True


def build_all(skip_build=False, windows_only=False, wsl_only=False):
    """Build all required binaries. Returns True if all succeeded."""
    if skip_build:
        return True
    build_info = {
        "build_failed": False,
        "wsl_available": is_wsl_available() if platform.system() == "Windows" else "N/A",
    }
    ok = True
    if platform.system() == "Windows":
        if not wsl_only:
            if not build_windows_main():
                ok = False
            if not build_windows_tests():
                ok = False
        if is_wsl_available() and not windows_only:
            if not build_linux_main(test_build=True):
                ok = False
            if not build_linux_tests():
                ok = False
            if not ensure_ncd_service_for_tests():
                print("[BUILD] ERROR: Linux service launcher scripts are missing")
                ok = False
    else:
        if not build_linux_main(test_build=True):
            ok = False
        if not build_linux_tests():
            ok = False

    if not ok:
        build_info["build_failed"] = True
    return ok
