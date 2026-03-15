# Linux x64 Port Changelist (GCC)

This is the concrete change list to build NCD for Linux x64 while keeping Windows x64 support.

## 1) Platform contract and compile-time selection

- Add standard platform/arch macros in shared headers using compiler-provided symbols:
  - Windows: `_WIN32` / `_WIN64`
  - Linux: `__linux__`
  - x64: `_WIN64` or `__x86_64__` / `__amd64__`
- Use normalized project macros in code:
  - `NCD_PLATFORM_WINDOWS`
  - `NCD_PLATFORM_LINUX`
  - `NCD_ARCH_X64`
- Done in `src/ncd.h` (already added).

## 2) Build entrypoints

- Windows build stays in `build.bat` (MSVC).
- Linux build script is `build.sh` (GCC) (already added).
- Make `build.sh` executable on Linux:
  - `chmod +x build.sh`

## 3) Split Windows-only code behind platform abstractions

Current code is Win32-API heavy in these areas:

- Console I/O / key input / cursor positioning:
  - `src/ui.c`
  - `src/scanner.c` (live status repaint)
  - `src/main.c` (CONOUT$ writes)
- Filesystem scan and drive discovery:
  - `src/scanner.c` (`GetLogicalDrives`, `GetDriveTypeA`, `FindFirstFileExA`)
- Path/env helpers:
  - `src/database.c`, `src/main.c` (`GetEnvironmentVariableA`, `%LOCALAPPDATA%`, `%TEMP%`)

Required change:

- Introduce platform abstraction layer, for example:
  - `src/platform/platform.h`
  - `src/platform/platform_windows.c`
  - `src/platform/platform_linux.c`
- Move OS-specific APIs into those files and expose common functions.

## 4) Linux filesystem model

Windows drive letters do not exist on Linux.

Required change:

- Replace drive-letter model for Linux with mount-root entries:
  - Default roots: `/`, plus mounted filesystems from `/proc/mounts` (filter pseudo filesystems).
- Keep Windows drive logic unchanged under `#if NCD_PLATFORM_WINDOWS`.
- For Linux DB keys, use a mount ID/path string instead of `drive_letter`.

## 5) Linux directory scan implementation

Replace Win32 find APIs with POSIX/Linux:

- `opendir`, `readdir`, `closedir`
- `lstat`/`stat` for type checks
- Skip symlink loops (`S_IFLNK`) or track visited inode/device pairs.
- Hidden detection:
  - Unix hidden: name starts with `.`
- System detection:
  - Define Linux rule (for example: paths under `/proc`, `/sys`, `/dev`, `/run` flagged as system-like)

## 6) Linux TUI implementation

Current UI uses raw Win32 console handles.

Required change:

- Option A (recommended): `ncurses` backend for Linux:
  - Arrow keys, tab, backspace, enter, escape
  - Window-safe redraws
- Option B: ANSI + termios raw mode (more code/risk).

Keep selector/navigator behavior identical to Windows:

- Match selector
- Tab -> navigator
- Backspace -> return to selector
- Child cycling with Tab in navigator

## 7) Result handoff model on Linux

Current model writes `%TEMP%\ncd_result.bat` for `cmd.exe`.

Required change:

- Linux wrapper script (`ncd.sh`) should `source` a result file with shell-safe exports:
  - `NCD_STATUS`
  - `NCD_PATH`
  - `NCD_MESSAGE`
- Exe should emit a shell snippet (`ncd_result.sh`) when on Linux.

## 8) DB and history locations on Linux

Current storage uses `%LOCALAPPDATA%`.

Required change:

- Use XDG defaults:
  - Data: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd`
  - Cache/temp: `${XDG_RUNTIME_DIR:-/tmp}` for transient result handoff
- Keep per-root/per-drive DB split strategy.
- Keep `ncd.history` logic (same format is fine).

## 9) Command-line parity

Keep existing behavior on both platforms:

- `/r`, `/rBDE`, `/r-b,d`
- `/f`, `/fc`
- `/h`, `/?`
- `/v` prints version and continues when combined with work
- `.` and root navigation mode

Linux equivalent for `/rBDE` should be adapted or disabled with a clear message unless mapped to root IDs.

## 10) Test matrix

- Build:
  - Windows x64 (MSVC)
  - Linux x64 (GCC)
- Functional:
  - Search exact/prefix chain
  - Multi-match selector
  - Selector Tab -> navigator -> Enter/Backspace/Esc
  - Rescan all/include/exclude forms
  - Frequent history promotion and update
  - Missing-path handling triggers rescan behavior

## Suggested migration sequence

1. Add platform abstraction layer and keep Windows behavior identical.
2. Port scan + path/env + result handoff for Linux (no UI yet; non-interactive selection first).
3. Port Linux TUI backend.
4. Enable full CLI parity and run cross-platform tests.
