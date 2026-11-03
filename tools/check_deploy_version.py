#!/usr/bin/env python3
"""Check that the NCD version is in sync across:

  1. src/control_ipc.h        (NCD_APP_VERSION - single source of truth)
  2. Built binaries           (repo root NewChangeDirectory.exe / NCDService.exe)
  3. Deployed binaries        (C:\\Program Files\\NCD\\bin\\)

Exit code 0 when everything matches, 1 on any drift.
Used by the .githooks/post-commit auto-deploy hook; also runnable manually:

    python tools/check_deploy_version.py
"""

import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEPLOY_DIR = Path(r"C:\Program Files\NCD\bin")


def repo_version():
    text = (REPO / "src" / "control_ipc.h").read_text(encoding="utf-8")
    m = re.search(r'#define\s+NCD_APP_VERSION\s+"([^"]+)"', text)
    return m.group(1) if m else None


def exe_version(path: Path):
    """Return the 'X.Y' version reported by `exe -v`, or None."""
    if not path.exists():
        return None
    try:
        out = subprocess.run([str(path), "-v"], capture_output=True,
                             text=True, timeout=30)
        m = re.search(r"v(\d+\.\d+)", out.stdout + out.stderr)
        return m.group(1) if m else None
    except Exception:
        return None


def main():
    want = repo_version()
    rows = [
        ("repo header (control_ipc.h)", want),
        ("built client   (repo exe)", exe_version(REPO / "NewChangeDirectory.exe")),
        ("built service  (repo exe)", exe_version(REPO / "NCDService.exe")),
        ("deployed client", exe_version(DEPLOY_DIR / "NewChangeDirectory.exe")),
        ("deployed service", exe_version(DEPLOY_DIR / "NCDService.exe")),
    ]

    drift = False
    for label, ver in rows:
        status = "OK" if ver == want else "DRIFT"
        if ver != want:
            drift = True
        print(f"  [{status:5}] {label:32} {ver or '(missing)'}")

    if drift:
        print("\nVersion drift detected.")
        print("  - If built binaries are stale: run build.bat, then deploy.bat")
        print("  - If deployed binaries are stale: run deploy.bat")
        return 1

    print(f"\nAll versions in sync at {want}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
