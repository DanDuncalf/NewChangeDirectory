"""Environment isolation and guaranteed cleanup for NCD tests."""

import atexit
import os
import platform
import random
import shutil
import signal
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path


class TestIsolation:
    """
    Isolates NCD test execution from the host environment.

    Guarantees cleanup via atexit + signal handlers (replaces PowerShell try/finally).
    Usage:
        with TestIsolation():
            # run tests
    """

    _keys_to_save = [
        "LOCALAPPDATA",
        "NCD_TEST_MODE",
        "TEMP",
        "TMP",
        "PATH",
        "XDG_DATA_HOME",
        "NCD_UI_KEYS",
        "NCD_UI_KEY_TIMEOUT_MS",
    ]

    def __init__(self):
        self._original_env: dict[str, str | None] = {}
        self.test_temp_dir: Path | None = None
        self._active = False

    def setup(self):
        if self._active:
            return
        print("[ISOLATION] Setting up isolated test environment...")
        for key in self._keys_to_save:
            self._original_env[key] = os.environ.get(key)
        self._stop_ncd_processes()
        os.environ["NCD_TEST_MODE"] = "1"
        os.environ["NCD_UI_KEYS"] = "ENTER"
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        rand = random.randint(1000, 9999)
        dirname = f"ncd_test_{ts}_{rand}"
        self.test_temp_dir = Path(tempfile.gettempdir()) / dirname
        self.test_temp_dir.mkdir(parents=True, exist_ok=True)
        os.environ["LOCALAPPDATA"] = str(self.test_temp_dir)
        os.environ["XDG_DATA_HOME"] = str(self.test_temp_dir)
        # Also set TEMP/TMP inside isolation so temp files go there
        os.environ["TEMP"] = str(self.test_temp_dir)
        os.environ["TMP"] = str(self.test_temp_dir)
        self._active = True
        print(f"[ISOLATION] Temp dir: {self.test_temp_dir}")
        print(f"[ISOLATION] LOCALAPPDATA -> {os.environ.get('LOCALAPPDATA')}")
        print(f"[ISOLATION] XDG_DATA_HOME -> {os.environ.get('XDG_DATA_HOME')}")

    def teardown(self):
        if not self._active:
            return
        print("\n[ISOLATION] Cleaning up...")
        self._stop_ncd_processes()
        if self.test_temp_dir and self.test_temp_dir.exists():
            try:
                shutil.rmtree(self.test_temp_dir, ignore_errors=True)
                print(f"[ISOLATION] Removed temp dir: {self.test_temp_dir}")
            except Exception as e:
                print(f"[ISOLATION] Could not remove temp dir: {e}")
        for var in ("NCD_UI_KEYS", "NCD_UI_KEY_TIMEOUT_MS"):
            os.environ.pop(var, None)
        for key, value in self._original_env.items():
            if value is not None:
                os.environ[key] = value
            else:
                os.environ.pop(key, None)
        self._active = False
        print("[ISOLATION] Environment restored. Safe to continue using NCD.")

    @staticmethod
    def _stop_ncd_processes():
        system = platform.system()
        if system == "Windows":
            for name in ("NCDService", "NewChangeDirectory"):
                try:
                    subprocess.run(
                        ["taskkill", "/F", "/IM", f"{name}.exe"],
                        capture_output=True,
                    )
                except Exception:
                    pass
            try:
                subprocess.run(
                    [
                        "wsl",
                        "bash",
                        "-lc",
                        'pkill -9 -x NCDService 2>/dev/null; '
                        'pkill -9 -f NewChangeDirectory 2>/dev/null; '
                        'killall -9 NCDService 2>/dev/null; '
                        'rm -f "${XDG_RUNTIME_DIR:-/tmp}/ncd_service.pid" 2>/dev/null',
                    ],
                    capture_output=True,
                )
            except Exception:
                pass
        else:
            for name in ("ncd_service", "NCDService", "NewChangeDirectory"):
                try:
                    subprocess.run(
                        ["pkill", "-9", "-f", name],
                        capture_output=True,
                    )
                except Exception:
                    pass

    def __enter__(self):
        self.setup()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.teardown()
        return False


# Module-level singleton for signal handling
_isolation_stack: list[TestIsolation] = []


def _signal_handler(signum, frame):
    print(f"\n[ISOLATION] Interrupted (signal {signum})! Running cleanup...")
    for iso in reversed(_isolation_stack):
        iso.teardown()
    sys.exit(128 + signum)


def register_isolation(iso: TestIsolation):
    """Register an isolation instance for signal/atexit cleanup."""
    _isolation_stack.append(iso)


def unregister_isolation(iso: TestIsolation):
    """Unregister an isolation instance."""
    if iso in _isolation_stack:
        _isolation_stack.remove(iso)


# Register handlers once at import time
signal.signal(signal.SIGINT, _signal_handler)
if hasattr(signal, "SIGBREAK"):
    signal.signal(signal.SIGBREAK, _signal_handler)
if hasattr(signal, "SIGTERM"):
    signal.signal(signal.SIGTERM, _signal_handler)


def _atexit_cleanup():
    for iso in reversed(_isolation_stack):
        iso.teardown()


atexit.register(_atexit_cleanup)
