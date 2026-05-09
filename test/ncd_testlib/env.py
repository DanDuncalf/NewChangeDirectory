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


class WindowsTestDrive:
    """Provision an isolated Windows drive letter for feature tests.

    Uses PowerShell to find a truly free letter, then tries diskpart VHD
    (preferred) or falls back to SUBST.  Cleans up on teardown.
    """

    def __init__(self):
        self.letter = None
        self.vhd_path = None
        self.is_subst = False
        self._test_root = None

    def _find_free_letter(self):
        """Return a free drive letter (e.g. 'Z') or None."""
        try:
            result = subprocess.run(
                [
                    "powershell",
                    "-NoProfile",
                    "-Command",
                    "$used = (Get-Volume | Where-Object { $_.DriveLetter } | "
                    "ForEach-Object { $_.DriveLetter.ToString().ToUpper() }); "
                    "$all = 'Z','Y','X','W','V','U','T','S','R','Q','P','O','N',"
                    "'M','L','K','J','I','H','G','F','E','D','B','A'; "
                    "$free = $all | Where-Object { $_ -notin $used } | "
                    "Select-Object -First 1; "
                    "if ($free) { Write-Output $free } else { exit 1 }",
                ],
                capture_output=True,
                text=True,
                timeout=15,
            )
            if result.returncode == 0 and result.stdout.strip():
                return result.stdout.strip()
        except Exception as e:
            print(f"[TEST_DRIVE] Error finding free letter: {e}")
        return None

    def _force_cleanup_letter_and_vhd(self):
        """Aggressively clean up any existing VHD and drive letter assignment."""
        # 1. Remove drive letter assignment via PowerShell (if any disk has it)
        try:
            subprocess.run(
                ["powershell", "-NoProfile", "-Command",
                 f"Get-Partition -DriveLetter '{self.letter}' -ErrorAction SilentlyContinue | "
                 f"Remove-PartitionAccessPath -AccessPath '{self.letter}:\\' -ErrorAction SilentlyContinue"],
                capture_output=True, timeout=15
            )
        except Exception:
            pass
        # 2. Dismount any disk image at our VHD path
        if os.path.exists(self.vhd_path):
            try:
                subprocess.run(
                    ["powershell", "-NoProfile", "-Command",
                     f"Dismount-DiskImage -ImagePath '{self.vhd_path}' -ErrorAction SilentlyContinue"],
                    capture_output=True, timeout=15
                )
            except Exception:
                pass
            # 3. Remove the stale VHD file
            try:
                os.remove(self.vhd_path)
                print(f"[TEST_DRIVE] Removed stale VHD: {self.vhd_path}")
            except Exception as e:
                print(f"[TEST_DRIVE] Could not remove stale VHD: {e}")

    def setup(self):
        """Create an isolated drive letter. Returns True on success."""
        if platform.system() != "Windows":
            return True

        self.letter = self._find_free_letter()
        if not self.letter:
            print("[TEST_DRIVE] No free drive letter available")
            return False

        # Clean up any stale VHD at the target path and unmount the letter
        self.vhd_path = os.path.join(
            tempfile.gettempdir(), f"ncd_test_{self.letter}.vhdx"
        )
        self._force_cleanup_letter_and_vhd()

        # Try diskpart VHD first (most reliable isolation)
        dp_script = os.path.join(tempfile.gettempdir(), "ncd_diskpart_py.txt")
        try:
            with open(dp_script, "w") as f:
                f.write(f'create vdisk file="{self.vhd_path}" maximum=50 type=expandable\n')
                f.write(f'select vdisk file="{self.vhd_path}"\n')
                f.write("attach vdisk\n")
                f.write("create partition primary\n")
                f.write('format fs=ntfs quick label="NCDTest"\n')
                f.write(f"assign letter={self.letter}\n")

            result = subprocess.run(
                ["diskpart", "/s", dp_script],
                capture_output=True,
                text=True,
                timeout=30,
            )
            os.unlink(dp_script)
        except Exception as e:
            print(f"[TEST_DRIVE] diskpart error: {e}")
            if os.path.exists(dp_script):
                os.unlink(dp_script)
            result = subprocess.CompletedProcess(args=[], returncode=-1)

        if result.returncode == 0 and os.path.exists(f"{self.letter}:\\"):
            print(f"[TEST_DRIVE] VHD mounted at {self.letter}:\\")
            os.environ["NCD_TEST_DRIVE"] = self.letter
            os.environ["NCD_TEST_ROOT"] = f"{self.letter}:\\"
            return True

        # diskpart failed — try to diagnose
        print(f"[TEST_DRIVE] diskpart exit={result.returncode}")
        if result.stderr:
            print(f"[TEST_DRIVE] diskpart stderr: {result.stderr.strip()}")
        if result.stdout:
            # Print last few lines of diskpart output for diagnosis
            lines = result.stdout.strip().splitlines()
            for line in lines[-5:]:
                print(f"[TEST_DRIVE] diskpart: {line}")

        if result.returncode == 0 and os.path.exists(f"{self.letter}:\\"):
            print(f"[TEST_DRIVE] VHD mounted at {self.letter}:\\")
            os.environ["NCD_TEST_DRIVE"] = self.letter
            os.environ["NCD_TEST_ROOT"] = f"{self.letter}:\\"
            return True

        # Fall back to SUBST
        self._test_root = os.path.join(
            tempfile.gettempdir(), f"ncd_test_tree_{os.getpid()}"
        )
        os.makedirs(self._test_root, exist_ok=True)
        sub = subprocess.run(
            ["subst", f"{self.letter}:", self._test_root],
            capture_output=True,
            text=True,
        )
        if sub.returncode == 0:
            print(f"[TEST_DRIVE] SUBST {self.letter}: -> {self._test_root}")
            self.is_subst = True
            os.environ["NCD_TEST_DRIVE"] = self.letter
            os.environ["NCD_TEST_ROOT"] = self._test_root
            return True

        print(f"[TEST_DRIVE] Failed to provision drive {self.letter}")
        self.letter = None
        return False

    def teardown(self):
        """Remove the isolated drive letter."""
        if not self.letter:
            return
        if self.is_subst:
            subprocess.run(
                ["subst", f"{self.letter}:", "/D"],
                capture_output=True,
            )
            if self._test_root and os.path.exists(self._test_root):
                shutil.rmtree(self._test_root, ignore_errors=True)
        elif self.vhd_path and os.path.exists(self.vhd_path):
            subprocess.run(
                [
                    "powershell",
                    "-NoProfile",
                    "-Command",
                    f"Dismount-DiskImage -ImagePath '{self.vhd_path}' "
                    f"-ErrorAction SilentlyContinue; "
                    f"Remove-Item '{self.vhd_path}' -ErrorAction SilentlyContinue",
                ],
                capture_output=True,
            )
        for key in ("NCD_TEST_DRIVE", "NCD_TEST_ROOT"):
            os.environ.pop(key, None)
        print(f"[TEST_DRIVE] Cleaned up drive {self.letter}")


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
        self._test_drive: WindowsTestDrive | None = None

    def setup(self):
        if self._active:
            return
        print("[ISOLATION] Setting up isolated test environment...")
        for key in self._keys_to_save:
            self._original_env[key] = os.environ.get(key)
        self._stop_ncd_processes()
        self._cleanup_test_vhds()
        if platform.system() == "Windows":
            self._test_drive = WindowsTestDrive()
            self._test_drive.setup()
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
        self._cleanup_test_vhds()
        if self._test_drive:
            self._test_drive.teardown()
            self._test_drive = None
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
    def _cleanup_test_vhds():
        system = platform.system()
        if system != "Windows":
            return
        try:
            result = subprocess.run(
                [
                    "powershell",
                    "-NoProfile",
                    "-Command",
                    "Get-Disk | Where-Object { $_.Location -match 'ncd_.*\\.vhdx' } | "
                    "ForEach-Object { Dismount-DiskImage -ImagePath $_.Location -ErrorAction SilentlyContinue; "
                    "Remove-Item $_.Location -ErrorAction SilentlyContinue }",
                ],
                capture_output=True,
                text=True,
            )
            if result.stdout:
                for line in result.stdout.strip().splitlines():
                    print(f"[ISOLATION] {line}")
        except Exception as e:
            print(f"[ISOLATION] VHD cleanup warning: {e}")

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
