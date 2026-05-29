"""Platform, architecture, and binary format detection for test reporting."""

import platform
import struct
from pathlib import Path

from .build import run_cmd


def get_host_arch():
    """Return normalized host architecture name."""
    mach = platform.machine().lower()
    if mach in ("amd64", "x86_64"):
        return "x64"
    if mach in ("arm64", "aarch64"):
        return "arm64"
    if mach in ("riscv64",):
        return "riscv64"
    return mach


def get_host_platform():
    """Return host platform name."""
    return platform.system()


def is_windows_arm64_host():
    """Return True if running on Windows ARM64."""
    if platform.system() != "Windows":
        return False
    # PROCESSOR_ARCHITECTURE is AMD64 even on ARM64 when running under x64 emulation,
    # so we use a more reliable check via ctypes if available.
    try:
        import ctypes
        # IsWow64Process2 gives us the native machine type
        kernel32 = ctypes.windll.kernel32
        if hasattr(kernel32, "IsWow64Process2"):
            process_machine = ctypes.c_uint16()
            native_machine = ctypes.c_uint16()
            kernel32.IsWow64Process2(
                ctypes.c_void_p(-1),  # GetCurrentProcess()
                ctypes.byref(process_machine),
                ctypes.byref(native_machine),
            )
            # IMAGE_FILE_MACHINE_ARM64 = 0xAA64
            return native_machine.value == 0xAA64
    except Exception:
        pass
    # Fallback: check environment variables
    import os
    if os.environ.get("PROCESSOR_ARCHITECTURE", "").upper() == "ARM64":
        return True
    return False


def get_wsl_arch():
    """Return architecture of the default WSL distro, or None if unavailable."""
    rc, out, _ = run_cmd(["wsl", "uname", "-m"], timeout=10)
    if rc != 0:
        return None
    mach = out.strip().lower()
    if mach in ("x86_64",):
        return "x64"
    if mach in ("aarch64",):
        return "arm64"
    if mach in ("riscv64",):
        return "riscv64"
    return mach


def get_pe_arch(path):
    """Read PE Machine type from a Windows executable. Returns arch string or None."""
    try:
        with open(path, "rb") as f:
            data = f.read(512)
        if len(data) < 64 or data[:2] != b"MZ":
            return None
        pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
        if pe_offset + 6 > len(data):
            return None
        machine = struct.unpack_from("<H", data, pe_offset + 4)[0]
        if machine == 0x8664:
            return "x64"
        if machine == 0xAA64:
            return "arm64"
        if machine == 0x014C:
            return "x86"
        return f"pe_0x{machine:04X}"
    except Exception:
        return None


def get_elf_arch(path):
    """Read ELF e_machine from a Linux binary. Returns arch string or None."""
    try:
        with open(path, "rb") as f:
            data = f.read(20)
        if len(data) < 20 or data[:4] != b"\x7fELF":
            return None
        ei_class = data[4]  # 1=32-bit, 2=64-bit
        ei_data = data[5]   # 1=little, 2=big
        fmt = "<H" if ei_data == 1 else ">H"
        e_machine = struct.unpack_from(fmt, data, 18)[0]
        if e_machine == 62:   # EM_X86_64
            return "x64"
        if e_machine == 183:  # EM_AARCH64
            return "arm64"
        if e_machine == 243:  # EM_RISCV
            return "riscv64"
        return f"elf_{e_machine}"
    except Exception:
        return None


def get_binary_arch(path):
    """Detect architecture of a binary file (PE or ELF)."""
    path = Path(path)
    if not path.exists():
        return None
    arch = get_pe_arch(path)
    if arch:
        return arch
    return get_elf_arch(path)


def is_native_execution(binary_arch, platform_label):
    """Return True if the binary architecture matches the execution environment."""
    if not binary_arch:
        return None  # unknown

    if platform_label == "linux" and platform.system() == "Windows":
        # Running under WSL
        wsl_arch = get_wsl_arch()
        if wsl_arch is None:
            return None
        return binary_arch == wsl_arch

    # Native Windows or native Linux
    host_arch = get_host_arch()
    if platform.system() == "Windows" and is_windows_arm64_host():
        host_arch = "arm64"
    return binary_arch == host_arch


def get_host_env_summary(build_info):
    """Return a multi-line string describing the test host environments."""
    lines = []
    host_plat = get_host_platform()
    host_arch = get_host_arch()

    # Detect if Windows host is ARM64 running x64 emulation
    if host_plat == "Windows" and is_windows_arm64_host():
        host_arch = "arm64"
        lines.append(f"Host: Windows {host_arch} (native)")
        lines.append(f"  Windows binaries: x64 (emulated via Prism/WoW64)")
    else:
        lines.append(f"Host: {host_plat} {host_arch} (native)")

    if build_info.get("wsl_available"):
        wsl_arch = get_wsl_arch()
        if wsl_arch:
            if host_plat == "Windows" and wsl_arch != host_arch:
                lines.append(f"  WSL environment: Linux {wsl_arch} (emulated)")
            else:
                lines.append(f"  WSL environment: Linux {wsl_arch} (native)")
        else:
            lines.append("  WSL environment: unavailable")
    else:
        lines.append("  WSL environment: not tested")

    return lines


def get_binary_version_info(path):
    """Try to extract version string from an NCD binary via -v flag."""
    try:
        rc, out, _ = run_cmd([str(path), "-v"], timeout=10)
        if rc == 0 and out.strip():
            return out.strip()
    except Exception:
        pass
    return None
