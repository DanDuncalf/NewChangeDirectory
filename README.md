# NewChangeDirectory (NCD)

A cross-platform command-line directory navigation tool inspired by the classic Norton Commander CD command. NCD maintains a database of directory paths and provides fast, fuzzy search-based navigation with an interactive TUI.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platforms](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-blue.svg)

## Features

- **Fast Directory Search** - Search across your entire filesystem using a pre-built binary database
- **Interactive TUI** - Selector mode with live filtering, Navigator mode for browsing
- **Cross-Platform** - Works on Windows (x64) and Linux (x64, including WSL)
- **Multi-threaded Scanning** - Fast directory enumeration using parallel workers
- **Fuzzy Matching** - Damerau-Levenshtein distance for typo-tolerant searches
- **Smart Ranking** - Learns from your choices to prioritize frequently used directories
- **Groups/Bookmarks** - Tag frequently used directories for quick access (supports multiple dirs per group)
- **Directory History** - Interactive history browser with delete support
- **Agent Mode** - LLM-friendly API for integration with AI tools
- **Shell Tab Completion** - Bash, Zsh, and PowerShell completion support
- **Optional Service** - Resident state service for faster startup (shared memory)

## Download & Install (No Build Required)

Pre-built packages for all platforms are available on the [GitHub Releases](https://github.com/DanDuncalf/NewChangeDirectory/releases) page. Each package includes the NCD binary, service binary, wrapper scripts, install scripts, shell completions, and the optional MCP server.

### Windows

1. Download `NCD-<version>-windows-x64.zip` from the latest release
2. Extract the ZIP to a folder of your choice (e.g. `C:\NCD`)
3. Run `install.bat` to add NCD to your PATH (or [install manually](#manual-installation))

```batch
# Extract and install
C:\NCD> install.bat
```

Or install manually:
```batch
set DEST=C:\NCD\bin
mkdir %DEST%
copy NewChangeDirectory.exe %DEST%
copy NCDService.exe %DEST%
copy ncd.bat %DEST%
copy ncd_service.bat %DEST%
:: Add %DEST% to your PATH environment variable
```

### Windows ARM64

Same steps, download `NCD-<version>-windows-arm64.zip` instead.

### Linux (x64, ARM64, RISC-V)

1. Download the appropriate archive for your architecture:
   - `NCD-<version>-linux-x64.tar.gz` — Intel/AMD 64-bit
   - `NCD-<version>-linux-arm64.tar.gz` — ARM 64-bit (e.g. Raspberry Pi)
   - `NCD-<version>-linux-riscv64.tar.gz` — RISC-V 64-bit
2. Extract and run the install script:

```bash
tar -xzf NCD-<version>-linux-x64.tar.gz
cd NCD-<version>-linux-x64
sudo ./install.sh      # Installs to /usr/local/bin
```

Or install to a custom location:
```bash
./install.sh ~/.local/bin
```

After installation, add the ncd shell function to your `~/.bashrc`:
```bash
echo 'ncd() { source /usr/local/bin/ncd "$@"; }' >> ~/.bashrc
source ~/.bashrc
```

### Verify It Works

```bash
ncd --version
ncd --help
```

### What's in a Package?

| File | Purpose |
|------|---------|
| `NewChangeDirectory` / `.exe` | The main NCD binary |
| `NCDService` / `.exe` | Optional resident service (shared memory cache) |
| `ncd` / `ncd.bat` | Shell wrapper (needed for `cd` to work from your shell) |
| `ncd_service` / `ncd_service.bat` | Service launcher script |
| `install.bat` / `install.sh` | One-command installer (PATH setup, startup, etc.) |
| `completions/` | Tab-completion scripts for Bash, Zsh, PowerShell |
| `mcp_server/` | Optional MCP server for LLM integration (Claude, etc.) |
| `README.md` | This file |

## Building from Source

If you prefer to build from source instead of downloading a pre-built package:

### Windows (MSVC)

```batch
build.bat
```

Requires Visual Studio 2019/2022 with C11 support.

### Windows (MinGW)

```batch
make
```

### Linux (GCC/Clang)

```bash
./build.sh
# Or with Clang:
CC=clang ./build.sh
```

## Usage

**Important:** NCD requires a wrapper script because a child process cannot change the working directory of its parent shell.

### Windows
Use `ncd.bat` (not the `.exe` directly):
```batch
ncd downloads
ncd scott\downloads
```

### Linux
Source the wrapper or use a shell function:
```bash
source ncd
cd downloads
```

Or add to your `.bashrc`:
```bash
ncd() { source /path/to/ncd "$@"; }
```

### Commands

```bash
ncd <search>              # Search for a directory
ncd -r                    # Rescan all drives
ncd -r:c                   # Rescan drive C only
ncd -r <path>              # Rescan a specific directory
ncd -r .                   # Rescan from current directory
ncd -r <path>              # Rescan a specific directory
ncd -r .                   # Rescan from current directory
ncd -i <search>           # Include hidden directories
ncd -s                    # Include system directories
ncd -a                    # Include all (hidden + system)
ncd -z <search>           # Fuzzy matching
ncd .                     # Navigator mode (browse from current dir)
ncd @home                 # Jump to bookmarked group
ncd -g:@home              # Add current directory to group
ncd -G:@home             # Remove current directory from group
ncd -g:l                   # List all groups
ncd -0                    # Ping-pong between last two directories
ncd -h                    # Browse history interactively (Del to remove)
ncd -h:l                   # List directory history
ncd -h:c                   # Clear all history
ncd -h:c3                  # Remove history entry #3
ncd -f                    # Show frequent searches
ncd -f:c                   # Clear frequent searches
ncd -x:<pattern>          # Add exclusion pattern
ncd -X:<pattern>         # Remove exclusion pattern
ncd -x:l                   # List exclusion patterns
ncd --agent:query <term>   # Agent mode (JSON output)
ncd -?                    # Help
```

### Interactive Selection

When multiple directories match, NCD shows an interactive list:

- **Arrow keys** - Navigate up/down
- **Page Up/Down** - Scroll a page at a time
- **Home/End** - Jump to first/last item
- **Type characters** - Live filter the list (e.g., type "src" to narrow results)
- **Backspace** - Remove last filter character
- **Escape** - Clear filter (first press) or cancel (second press)
- **Tab** - Enter navigator mode on selected item
- **Enter** - Select highlighted directory

### History Browser

The interactive history browser (`ncd -h`) shows recently visited directories:

- **Arrow keys** - Navigate
- **Delete** - Remove entry from history
- **Enter** - Navigate to selected directory
- **Escape** - Cancel

## Building from Source

### Windows (MSVC)

```batch
build.bat
```

Requires Visual Studio 2019/2022 with C11 support.

### Windows (MinGW)

```batch
make
```

### Linux (GCC/Clang)

```bash
./build.sh
# Or with Clang:
CC=clang ./build.sh
```

## Running Tests

### Preferred Entry Points

Use the Python harness directly:

```powershell
:: Full cross-platform report with test.results
python test\generate_report.py

:: Direct runner entry points
python test\runner.py unit
python test\runner.py integration
python test\runner.py --repair
```

`test/generate_report.py` is a thin compatibility wrapper over `test/runner.py`, which is the current build-and-test engine.

**What the Python harness does:**
- Detects outdated or missing binaries and rebuilds them automatically
- Runs Windows unit tests (`.exe`) and Linux unit tests (ELF via WSL)
- Detects build failures and aborts with clear diagnostics
- Writes `test.results` with build timestamps and per-test statistics
- **Exits with code 1** if any test fails or any test is skipped

**Requirements:**
- Windows: Visual Studio 2019/2022 (for auto-build)
- WSL: Ubuntu/Debian distro with `gcc` and `make`

### CI / GitHub Actions

Pushes to `master` are protected by the CI workflow (`.github/workflows/ci.yml`).  
**No push to master is allowed unless:**
- All Windows tests pass with zero failures and zero skips
- All Linux tests pass with zero failures and zero skips
- `test.results` is present and shows `OVERALL STATUS: PASS`

### Quick Start - Run All Tests

**Windows (complete test suite) — use the Python runner:**
```bash
python test/runner.py          # All tests
python test/runner.py unit     # Unit tests only
python test/runner.py --quick  # Skip fuzz/benchmarks
```

**Linux/WSL (complete test suite):**
```bash
# Build everything first
./build.sh

# Run all tests
python test/runner.py

# Or use the Makefile
cd test && make test

# Additional test targets
make fuzz       # Fuzz testing
make bench      # Performance benchmarks
make corruption # Corruption handling tests
```

### test.results File

`python test/generate_report.py` produces a `test.results` file in the project root.  
**This file must be checked in with every commit.** It contains:
- Build timestamps for all Windows and Linux binaries
- Per-executable pass/fail/skip counts
- Grand totals and overall PASS/FAIL status

### Test Runners Reference

| Command | Purpose |
|---------|---------|
| `python test/runner.py` | Run all tests (builds if needed) |
| `python test/runner.py unit` | Run only unit tests |
| `python test/runner.py integration` | Run only integration tests |
| `python test/runner.py --quick` | Skip fuzz/benchmarks |
| `python test/runner.py --repair` | Fix corrupted test environment |
| `python test/generate_report.py` | Alias for `python test/runner.py` |

See `AGENTS.md` and `AGENT_TESTING_GUIDE.md` for complete testing documentation.

## Architecture

NCD is written in C11 and organized into modules:

| Module | Purpose |
|--------|---------|
| `main.c` | CLI parsing, orchestration, heuristics |
| `database.c` | Binary DB load/save, groups, config, history |
| `scanner.c` | Multi-threaded directory scanning |
| `matcher.c` | Search matching with fuzzy support |
| `ui.c` | Interactive TUI (selector + navigator + history) |
| `platform.c` | Platform abstraction (delegates to shared lib) |

The project uses a shared platform abstraction library located at `../shared/` which provides cross-platform utilities for filesystem, console I/O, and threading.

## Agent Mode

NCD includes an agent mode for LLM integration:

```bash
ncd --agent:query downloads --json --limit 10
ncd --agent:ls /home/user --depth 2 --json
ncd --agent:tree /home/user --depth 3 --json
ncd --agent:check /home/user/projects
ncd --agent:check --db-age
ncd --agent:check --stats
ncd --agent:check --service-status
ncd --agent:complete dow              # Shell tab-completion candidates
ncd --agent:mkdir <path>              # Create directory and parents if needed
ncd --agent:mkdirs <content>          # Create directory tree from JSON/flat format
ncd --agent:rmdir <path> [--force]    # Remove empty directory
ncd --agent:rmdirs <path> --force     # Remove directory tree
ncd --agent:mv <src> <dst> [--force]  # Move/rename directory
ncd --agent:ln <target> <link>        # Create symbolic link
ncd --agent:verify <path> [--empty]   # Verify directory properties
ncd --agent:chmod <path> --mode 0755  # Change permissions (Linux only)
ncd --agent:help                      # Show all agent commands
```

See [docs/agent_mode.md](docs/agent_mode.md) for the current command/flag matrix, including the implemented filesystem-mutation flags such as `--dry-run`, `--atomic`, `--parents-required`, `--stop-on-error`, `--tree`, and `--recursive`.

Exit codes:
- `0` - Success / Found
- `1` - Not found / Error

## Tab Completion

NCD supports shell tab-completion for directory names:

### Bash
```bash
source completions/ncd.bash
# or if installed via deploy.sh, completions are auto-loaded
ncd dow<Tab>  # Completes to "downloads", "documents", etc.
ncd @h<Tab>   # Completes to "@home" group
```

### Zsh
```zsh
# Add completions directory to fpath, then run compinit
fpath+=(/path/to/completions)
autoload -U compinit && compinit
```

### PowerShell
```powershell
. /path/to/completions/ncd.ps1
ncd dow<Tab>  # Shows matching directory names
```

## Optional Service Mode

NCD can run with an optional resident service (`NCDService.exe` on Windows, `ncd_service` on Linux) that keeps state hot in memory using shared memory. This provides faster startup times for repeated use.

### Starting the Service

```bash
# Windows
ncd_service.bat start

# Linux
ncd_service start
```

When the service is running, NCD automatically connects to it. If the service is not available, NCD falls back to standalone mode (loading from disk).

### Initializing the Database on First Start

If no metadata file exists (first-time use), the service can initialize the database by scanning your drives before becoming available:

```bash
# Initialize by scanning all drives on first start
ncd_service start -init

# Initialize by scanning specific drives only
ncd_service start -init C,D,E
```

The `-init` option blocks startup until the scan is complete, preventing clients from attempting to use NCD before the database is ready. If a metadata file already exists, `-init` performs a forced rescan of the specified drives (or all drives) on startup.

## Database Storage

### Per-User Mode (default)

**Windows:**
- Per-drive databases: `%LOCALAPPDATA%\NCD\ncd_X.database`
- Metadata: `%LOCALAPPDATA%\NCD\ncd.metadata`
- Service log: `%LOCALAPPDATA%\NCD\ncd_service.log`

**Linux:**
- Per-mount databases: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd_XX.database`
- Metadata: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd.metadata`
- Service log: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd_service.log`

### User Mode (`--user-mode`)

**System-wide mode is now the default.** The service uses shared locations
accessible to all users on the machine:

**Windows:**
- Per-drive databases: `C:\ProgramData\NCD\ncd_X.database`
- Metadata: `C:\ProgramData\NCD\ncd.metadata`
- Service log: `C:\ProgramData\NCD\ncd_service.log`

**Linux:**
- Per-mount databases: `/var/lib/ncd/ncd_XX.database`
- Metadata: `/var/lib/ncd/ncd.metadata`
- Service log: `/var/lib/ncd/ncd_service.log`

When started with `--user-mode`, the service stores data in per-user local
directories instead. System mode uses fixed IPC and shared memory names (without
user SID/UID) with relaxed permissions so all local users can connect to a
single service instance. The service must run with administrator/root privileges
to write to shared directories. Clients connect with `ncd --user-mode <search>`.

## License

MIT License - See LICENSE file for details.

## Documentation

Additional documentation is available in the `docs/` directory:

- **[docs/agent_mode.md](docs/agent_mode.md)** - Agent mode command and flag reference
- **[docs/architecture/](docs/architecture/)** - Technical architecture docs
- **[docs/history/](docs/history/)** - Project history and lessons learned
- **[AGENTS.md](AGENTS.md)** - Comprehensive technical documentation for developers

## Credits

Inspired by the classic Norton Commander (Norton Utilities) CD command.
