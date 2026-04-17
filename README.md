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

## Installation

### Windows

Download the latest release or build from source:

```batch
build.bat
```

Copy `NewChangeDirectory.exe` and `ncd.bat` to a directory in your PATH.

### Linux/WSL

```bash
chmod +x build.sh
./build.sh
sudo ./deploy.sh  # Installs to /usr/local/bin
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

### Quick Start - Run All Tests

**Windows (complete test suite):**
```batch
:: Build everything first
build.bat

:: Run all tests (unit + integration)
Run-All-Tests-Complete.bat --windows-only

:: Or run just unit tests
cd test
Run-All-Unit-Tests.bat
```

**Linux/WSL (complete test suite):**
```bash
# Build everything first
./build.sh

# Run all tests
cd test
make test

# Additional test targets
make fuzz       # Fuzz testing
make bench      # Performance benchmarks
make corruption # Corruption handling tests
```

### Test Runners Reference

| Script | Purpose | Platform |
|--------|---------|----------|
| `Run-All-Tests-Complete.bat` | Master runner - builds and runs ALL tests | Windows |
| `test\Run-All-Unit-Tests.bat` | Runs all unit test executables | Windows |
| `test\Test-Service-Windows.bat` | Service tests (isolated) | Windows |
| `test\Test-NCD-Windows-Standalone.bat` | NCD without service | Windows |
| `test\Test-NCD-Windows-With-Service.bat` | NCD with service | Windows |

See `AGENTS.md` for complete testing documentation.

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
```

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

## Database Storage

**Windows:**
- Per-drive databases: `%LOCALAPPDATA%\NCD\ncd_X.database`
- Metadata: `%LOCALAPPDATA%\NCD\ncd.metadata`

**Linux:**
- Per-mount databases: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd_XX.database`
- Metadata: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd.metadata`

## License

MIT License - See LICENSE file for details.

## Documentation

Additional documentation is available in the `docs/` directory:

- **[docs/architecture/](docs/architecture/)** - Technical architecture docs
- **[docs/history/](docs/history/)** - Project history and lessons learned
- **[AGENTS.md](AGENTS.md)** - Comprehensive technical documentation for developers

## Credits

Inspired by the classic Norton Commander (Norton Utilities) CD command.
