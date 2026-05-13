# Implementation Plan: MCP Server for NCD

> **Status:** EXECUTED  
> **Note:** Directory renamed from `mcp/` to `mcp_server/` to avoid Python import shadowing with the PyPI `mcp` package.

---

## Goal
Create a Model Context Protocol (MCP) server that wraps NCD's Agent Mode (`--agent:*` commands), and integrate it into the cross-platform package build pipeline so it ships with every Windows ZIP and Linux tar.gz distribution.

---

## Phase 1: Create MCP Server (`mcp_server/`)

### 1.1 Create directory `mcp_server/`
Created at repo root. All MCP-related files live here.

### 1.2 Create `mcp_server/ncd_mcp_server.py`

Uses `mcp.server.fastmcp.FastMCP`.

- **Binary discovery** tries in order:
  1. `NCD_BINARY_PATH` env var
  2. `ncd` / `NewChangeDirectory` in `PATH`
  3. Windows: `%LOCALAPPDATA%\NCD\bin\NewChangeDirectory.exe`, `%ProgramFiles%\NCD\NewChangeDirectory.exe`
  4. Linux: `/usr/local/bin/NewChangeDirectory`, `/usr/bin/NewChangeDirectory`
  5. Script-relative: `../NewChangeDirectory.exe` / `../NewChangeDirectory`

- **14 FastMCP tools** exposed:
  - `ncd_query`, `ncd_ls`, `ncd_tree`, `ncd_check`, `ncd_complete`
  - `ncd_mkdir`, `ncd_mkdirs`, `ncd_rmdir`, `ncd_rmdirs`
  - `ncd_mv`, `ncd_ln`, `ncd_verify`, `ncd_chmod`, `ncd_help`

- Subprocess helper places `--json` **after** positional args to match NCD CLI parser expectations.

### 1.3 Create `mcp_server/pyproject.toml`

- Package: `ncd-mcp-server` v1.3.0
- Dependency: `mcp>=1.0.0`
- Entry point: `ncd-mcp-server = ncd_mcp_server:main`

### 1.4 Create `mcp_server/README.md`

Installation and Claude Desktop configuration guide.

### 1.5 Create `mcp_server/__init__.py` and `.gitignore`

Package init and git ignore rules for `__pycache__` / build artifacts.

---

## Phase 2: Integrate into Package Build Pipeline

### 2.1 Modify `Create-Packages.ps1`

Added `mcp_server/` copy (with `__pycache__` cleanup) in all 5 package phases:
- Windows x64 ZIP
- Windows ARM64 ZIP
- Linux x64 tar.gz
- Linux ARM64 tar.gz
- Linux RISC-V tar.gz

Also fixed a PS 5.1 `Join-Path` compatibility issue (3-arg form not supported).

### 2.2 Modify `packaging/linux/install.sh`

Added optional MCP server installation block after shell completions:
- Detects `python3` + `pip3`
- Prompts user to install
- Runs `pip3 install --user "${SCRIPT_DIR}/mcp_server"` or `sudo pip3 install`

### 2.3 Modify `packaging/windows/install.bat`

Added optional MCP server installation block after PATH check:
- Detects `python` + `pip`
- Prompts user to install
- Runs `pip install "%DEST_DIR%\mcp_server"`
- Escaped `}` characters in echo statements for batch parser safety

---

## Phase 3: Verification Results

| Check | Result |
|-------|--------|
| Server imports | PASS |
| Binary discovery | PASS |
| Package includes `mcp_server/` | PASS |
| No `__pycache__` in package | PASS |
| Tool count == 14 | PASS |
| `ncd_query` returns JSON | PASS |
| `ncd_ls` returns JSON | PASS |
| `ncd_check` returns JSON | PASS |
| `ncd_help` returns text | PASS |

---

## Files Created

- `mcp_server/ncd_mcp_server.py`
- `mcp_server/pyproject.toml`
- `mcp_server/README.md`
- `mcp_server/__init__.py`
- `mcp_server/.gitignore`

## Files Modified

- `Create-Packages.ps1`
- `packaging/linux/install.sh`
- `packaging/windows/install.bat`
