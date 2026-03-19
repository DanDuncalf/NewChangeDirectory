# NewChangeDirectory (NCD)

A cross-platform command-line directory navigation tool inspired by the classic Norton Commander CD command. NCD maintains a database of directory paths and provides fast, fuzzy search-based navigation with an interactive TUI.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platforms](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-blue.svg)

## Features

- **Fast Directory Search** - Search across your entire filesystem using a pre-built binary database
- **Interactive TUI** - Selector mode for choosing from multiple matches, Navigator mode for browsing
- **Cross-Platform** - Works on Windows (x64) and Linux (x64, including WSL)
- **Multi-threaded Scanning** - Fast directory enumeration using parallel workers
- **Fuzzy Matching** - Damerau-Levenshtein distance for typo-tolerant searches
- **Smart Ranking** - Learns from your choices to prioritize frequently used directories
- **Groups/Bookmarks** - Tag frequently used directories for quick access
- **Agent Mode** - LLM-friendly API for integration with AI tools

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
ncd /r                    # Rescan all drives
ncd /rC                   # Rescan drive C only
ncd /i <search>           # Include hidden directories
ncd /s                    # Include system directories
ncd /a                    # Include all (hidden + system)
ncd /z <search>           # Fuzzy matching
ncd .                     # Navigator mode (browse from current dir)
ncd @home                 # Jump to bookmarked group
ncd /g @home              # Create group for current directory
ncd /gl                   # List all groups
ncd /f                    # Show frequent searches
ncd /agent query <term>   # Agent mode (JSON output)
ncd /?                    # Help
```

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

### Linux/WSL

```bash
cd test
make test
make fuzz       # Fuzz testing
make bench      # Performance benchmarks
make corruption # Corruption handling tests
```

### Windows

```batch
cd test
build-tests.bat
```

## Architecture

NCD is written in C11 and organized into modules:

| Module | Purpose |
|--------|---------|
| `main.c` | CLI parsing, orchestration, heuristics |
| `database.c` | Binary DB load/save, groups, config |
| `scanner.c` | Multi-threaded directory scanning |
| `matcher.c` | Search matching with fuzzy support |
| `ui.c` | Interactive TUI (selector + navigator) |
| `platform.c` | Platform abstraction (delegates to shared lib) |

The project uses a shared platform abstraction library located at `../shared/` which provides cross-platform utilities for filesystem, console I/O, and threading.

## Agent Mode

NCD includes an agent mode for LLM integration:

```bash
ncd /agent query downloads --json --limit 10
ncd /agent ls /home/user --depth 2 --json
ncd /agent tree /home/user --depth 3 --json
ncd /agent check /home/user/projects
ncd /agent check --db-age
ncd /agent check --stats
```

Exit codes:
- `0` - Success / Found
- `1` - Not found / Error

## Database Storage

**Windows:**
- Per-drive databases: `%LOCALAPPDATA%\NCD\ncd_X.database`
- Config: `%LOCALAPPDATA%\NCD\ncd.config`
- History: `%LOCALAPPDATA%\ncd.history`

**Linux:**
- Per-mount databases: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd_XX.database`
- Config: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd.config`
- History: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd.history`

## License

MIT License - See LICENSE file for details.

## Credits

Inspired by the classic Norton Commander (Norton Utilities) CD command.
