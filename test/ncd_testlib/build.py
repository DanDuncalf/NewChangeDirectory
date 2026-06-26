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

WINDOWS_ALT_SERVICE_BINARIES = ["NCDService_v13.exe", "NCDService_v17.exe"]
LINUX_ALT_SERVICE_BINARIES = ["ncd_service_v13", "ncd_service_v17"]


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
        proc.kill()
        comm_thread.join()
        return -2, stdout[0].decode("utf-8", errors="replace"), stderr[0].decode("utf-8", errors="replace")

    if exc[0]:
        return -1, stdout[0].decode("utf-8", errors="replace"), str(exc[0])

    return (
        proc.returncode,
        stdout[0].decode("utf-8", errors="replace"),
        stderr[0].decode("utf-8", errors="replace"),
    )


def get_mtime(path):
    """Get modification time of a file as UTC datetime, or None if missing."""
    p = Path(path)
    if not p.exists():
        return None
    return datetime.fromtimestamp(p.stat().st_mtime, tz=timezone.utc)


def is_wsl_available():
    """Check if WSL (Windows Subsystem for Linux) is available."""
    try:
        rc, _, _ = run_cmd(["wsl", "--list", "--running"], timeout=10)
        return rc == 0
    except Exception:
        return False


def wsl_path(path):
    """Convert a Windows path to a WSL path."""
    p = Path(path).resolve()
    drive = p.drive[0].lower()
    rest = str(p)[2:].replace("\\", "/")
    return f"/mnt/{drive}{rest}"


def binary_needs_rebuild(binary_path, source_patterns):
    """Check if binary needs rebuild based on source modification times."""
    binary_mtime = get_mtime(binary_path)
    if binary_mtime is None:
        return True
    for pattern in source_patterns:
        for source in Path(PROJECT_ROOT).glob(pattern):
            source_mtime = get_mtime(source)
            if source_mtime and source_mtime > binary_mtime:
                return True
    return False


def build_windows_main():
    """Build main Windows binaries. Returns True on success."""
    print("[BUILD] Checking Windows main binaries...")
    needs_build = False
    for name in WINDOWS_MAIN_BINARIES:
        path = PROJECT_ROOT / name
        if binary_needs_rebuild(path, MAIN_SOURCE_PATTERNS):
            print(f"[BUILD] {name} needs rebuild: missing or outdated")
            needs_build = True
    if not needs_build:
        print("[BUILD] Windows main binaries are up-to-date.")
        return True
    print("[BUILD] Building Windows main binaries via build.bat...")
    rc, stdout, stderr = run_cmd("build.bat", cwd=str(PROJECT_ROOT), timeout=300, shell=True)
    if rc != 0:
        print(f"[BUILD] Windows main build FAILED (exit {rc})")
        if stderr:
            print(stderr)
        return False
    print("[BUILD] Windows main build succeeded.")
    return True


def build_windows_tests():
    """Build Windows test binaries. Returns True on success."""
    print("[BUILD] Checking Windows test binaries...")
    # Check if any test exe needs rebuild based on source changes
    test_dir = PROJECT_ROOT / "test"
    for pattern in MAIN_SOURCE_PATTERNS:
        for source in Path(PROJECT_ROOT).glob(pattern):
            source_mtime = get_mtime(source)
            if source_mtime is None:
                continue
            for exe in test_dir.glob("*.exe"):
                exe_mtime = get_mtime(exe)
                if exe_mtime is None or source_mtime > exe_mtime:
                    print("[BUILD] Test binaries need rebuild (source newer)")
                    rc, stdout, stderr = run_cmd(
                        "build-tests.bat", cwd=str(test_dir), timeout=300, shell=True
                    )
                    if rc != 0:
                        print(f"[BUILD] Windows test build FAILED (exit {rc})")
                        return False
                    # Also run build_new_tests.bat if it exists
                    new_tests_bat = test_dir / "build_new_tests.bat"
                    if new_tests_bat.exists():
                        rc2, _, _ = run_cmd(
                            "build_new_tests.bat", cwd=str(test_dir), timeout=120, shell=True
                        )
                        # build_new_tests.bat may return 1 for benign warnings, ignore
                    print("[BUILD] Windows test build succeeded.")
                    return True
    print("[BUILD] Windows test binaries are up-to-date.")
    return True


def build_linux_main(test_build=False):
    """Build main Linux binaries. Returns True on success."""
    print("[BUILD] Checking Linux main binaries...")
    if test_build:
        print("[BUILD] Test build requested - forcing rebuild for NCD_TEST_BUILD")
    else:
        needs_build = False
        for name in LINUX_MAIN_BINARIES:
            path = PROJECT_ROOT / name
            if binary_needs_rebuild(path, MAIN_SOURCE_PATTERNS):
                print(f"[BUILD] {name} needs rebuild: missing or outdated")
                needs_build = True
        if not needs_build:
            print("[BUILD] Linux main binaries are up-to-date.")
            return True

    if platform.system() == "Windows":
        if not is_wsl_available():
            print("[BUILD] ERROR: WSL not available, cannot build Linux binaries")
            return False
        wsl_root = wsl_path(PROJECT_ROOT)
        cmd = f'cd "{wsl_root}" && ./build.sh'
        rc, stdout, stderr = run_cmd(
            ["wsl", "bash", "-c", cmd],
            cwd=str(PROJECT_ROOT), timeout=300
        )
    else:
        rc, stdout, stderr = run_cmd(
            ["./build.sh"], cwd=str(PROJECT_ROOT), timeout=300, shell=True
        )

    if rc != 0:
        print(f"[BUILD] Linux main build FAILED (exit {rc})")
        if stderr:
            print(stderr)
        return False
    print("[BUILD] Linux main build succeeded.")
    return True


def build_linux_tests():
    """Build Linux test binaries. Returns True on success."""
    print("[BUILD] Checking Linux test binaries...")
    test_dir = PROJECT_ROOT / "test"

    # Check if any test needs rebuild
    needs_rebuild = False
    for pattern in MAIN_SOURCE_PATTERNS:
        for source in Path(PROJECT_ROOT).glob(pattern):
            source_mtime = get_mtime(source)
            if source_mtime is None:
                continue
            for elf in test_dir.glob("*"):
                if elf.is_file() and not elf.suffix:
                    elf_mtime = get_mtime(elf)
                    if elf_mtime is None:
                        needs_rebuild = True
                        break
                    if source_mtime > elf_mtime:
                        needs_rebuild = True
                        print(f"[BUILD] {elf.name} needs rebuild: outdated (source newer: {source_mtime.isoformat()})")
                if needs_rebuild:
                    break
            if needs_rebuild:
                break
        if needs_rebuild:
            break

    if not needs_rebuild:
        print("[BUILD] Linux test binaries are up-to-date.")
        return True

    print(f"[BUILD] Building Linux test binaries via WSL ({wsl_path(test_dir)})...")
    if platform.system() == "Windows":
        if not is_wsl_available():
            print("[BUILD] ERROR: WSL not available")
            return False
        wsl_test_dir = wsl_path(test_dir)
        rc, stdout, stderr = run_cmd(
            ["wsl", "bash", "-c", f'cd "{wsl_test_dir}" && make all'],
            cwd=str(PROJECT_ROOT), timeout=600
        )
    else:
        rc, stdout, stderr = run_cmd(
            ["make", "-C", str(test_dir), "all"],
            cwd=str(PROJECT_ROOT), timeout=600
        )

    if rc != 0:
        print(f"[BUILD] Linux test build FAILED (exit {rc})")
        if stderr:
            print(stderr)
        return False
    print("[BUILD] Linux test build succeeded.")
    return True


def ensure_ncd_service_for_tests():
    """Ensure ncd_service launcher scripts exist for Linux tests."""
    test_dir = PROJECT_ROOT / "test"
    scripts = ["test_ncd_wsl_standalone.sh", "test_ncd_wsl_with_service.sh",
               "test_service_wsl.sh"]
    for script in scripts:
        path = test_dir / script
        if not path.exists():
            return False
    return True


def build_alt_services():
    """Build alternative-version service binaries for version compatibility testing."""
    # Check if already built (idempotent - prevents double-build)
    windows_targets = [PROJECT_ROOT / "test" / "NCDService_v13.exe", PROJECT_ROOT / "test" / "NCDService_v17.exe"]
    linux_targets = [UNIT_TEST_DIR / "ncd_service_v13", UNIT_TEST_DIR / "ncd_service_v17"]
    targets = windows_targets if platform.system() == "Windows" else linux_targets
    if all(t.exists() for t in targets):
        print("[BUILD] Alt services already exist, skipping build")
        return True

    if platform.system() == "Windows":
        bat = PROJECT_ROOT / "test" / "build_alt_services.bat"
        rc, stdout, stderr = run_cmd(str(bat), cwd=str(PROJECT_ROOT), timeout=120, shell=True)
        if rc != 0:
            print(f"[BUILD] Alt service build FAILED (exit {rc})")
            if stderr:
                print(stderr)
            return False
    else:
        rc, stdout, stderr = run_cmd(
            ["make", "-C", str(UNIT_TEST_DIR), "alt_services"],
            cwd=str(PROJECT_ROOT), timeout=120
        )
        if rc != 0:
            print(f"[BUILD] Linux alt service build FAILED (exit {rc})")
            return False
    print("[BUILD] Alt services built (v1.3, v1.7)")
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
            if not build_alt_services():
                ok = False
        if is_wsl_available() and not windows_only:
            if not build_linux_main(test_build=True):
                ok = False
            if not build_linux_tests():
                ok = False
            if not ensure_ncd_service_for_tests():
                print("[BUILD] ERROR: Linux service launcher scripts are missing")
                ok = False
            if not build_alt_services():
                ok = False
    else:
        if not build_linux_main(test_build=True):
            ok = False
        if not build_linux_tests():
            ok = False
        if not build_alt_services():
            ok = False

    if not ok:
        build_info["build_failed"] = True
    return ok
