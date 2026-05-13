# NCD MCP Server

A [Model Context Protocol (MCP)](https://modelcontextprotocol.io) server for [NewChangeDirectory (NCD)](https://github.com/NewChangeDirectory/NCD). It exposes NCD's Agent Mode as MCP tools, enabling LLM clients such as Claude Desktop to query, browse, and manipulate directories through NCD.

## Installation

The MCP server is bundled with NCD releases. After extracting the NCD archive:

```bash
# From the extracted archive directory
pip install ./mcp_server
```

Or, if you prefer a user-local install:

```bash
pip install --user ./mcp_server
```

## Client Configuration

### Claude Desktop

Add the server to your Claude Desktop configuration file:

- **macOS:** `~/Library/Application Support/Claude/claude_desktop_config.json`
- **Windows:** `%APPDATA%\Claude\claude_desktop_config.json`
- **Linux:** `~/.config/Claude/claude_desktop_config.json`

```json
{
  "mcpServers": {
    "ncd": {
      "command": "ncd-mcp-server",
      "env": {
        "NCD_BINARY_PATH": "/usr/local/bin/NewChangeDirectory"
      }
    }
  }
}
```

> **Tip:** Set `NCD_BINARY_PATH` if `NewChangeDirectory` is not on your `PATH`.

## Available Tools

| Tool | Description |
|------|-------------|
| `ncd_query` | Search the indexed database |
| `ncd_ls` | List live filesystem contents |
| `ncd_tree` | Show directory structure from the database |
| `ncd_check` | Check path existence, DB age, stats, or service status |
| `ncd_complete` | Return completion candidates |
| `ncd_mkdir` | Create a single directory |
| `ncd_mkdirs` | Create a directory tree from flat text or JSON |
| `ncd_rmdir` | Remove a single empty directory |
| `ncd_rmdirs` | Recursively remove a directory tree |
| `ncd_mv` | Move or rename a directory |
| `ncd_ln` | Create a symbolic link |
| `ncd_verify` | Verify directory properties |
| `ncd_chmod` | Change directory permissions (Linux only) |
| `ncd_help` | Show built-in agent-mode help |

## Development

```bash
cd mcp
pip install -e .
ncd-mcp-server
```

## Troubleshooting

- **"NCD binary not found"** — Set the `NCD_BINARY_PATH` environment variable to the full path of the `NewChangeDirectory` (or `NewChangeDirectory.exe`) binary.
- **"command not found: ncd-mcp-server"** — Ensure the Python scripts directory (e.g., `~/.local/bin` or `%LOCALAPPDATA%\Programs\Python\Python311\Scripts`) is on your `PATH`.
