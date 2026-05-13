#!/usr/bin/env python3
"""
NCD MCP Server

A Model Context Protocol (MCP) server that wraps NCD's Agent Mode.
Requires: pip install mcp
Usage:    ncd-mcp-server
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

from mcp.server.fastmcp import FastMCP
from mcp.types import TextContent

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

SERVER_NAME = "ncd-mcp-server"

# ---------------------------------------------------------------------------
# Binary discovery
# ---------------------------------------------------------------------------

def _find_ncd() -> str:
    """Locate the NCD binary using multiple fallback strategies."""
    # 1. Environment variable override
    env_path = os.environ.get("NCD_BINARY_PATH")
    if env_path and Path(env_path).is_file():
        return env_path

    # 2. PATH lookup
    for name in ("NewChangeDirectory", "ncd"):
        found = shutil.which(name)
        if found and Path(found).is_file():
            return found

    # 3. Known install locations – Windows
    if sys.platform == "win32":
        for base in (
            os.environ.get("LOCALAPPDATA", ""),
            os.environ.get("PROGRAMFILES", ""),
            os.environ.get("PROGRAMFILES(X86)", ""),
        ):
            if not base:
                continue
            candidate = Path(base) / "NCD" / "bin" / "NewChangeDirectory.exe"
            if candidate.is_file():
                return str(candidate)
            candidate = Path(base) / "NCD" / "NewChangeDirectory.exe"
            if candidate.is_file():
                return str(candidate)

    # 4. Known install locations – Linux / macOS
    for candidate in (
        "/usr/local/bin/NewChangeDirectory",
        "/usr/bin/NewChangeDirectory",
        "/usr/local/bin/ncd",
        "/usr/bin/ncd",
    ):
        if Path(candidate).is_file():
            return candidate

    # 5. Relative to this script (development layout)
    script_dir = Path(__file__).resolve().parent
    for rel in ("../NewChangeDirectory.exe", "../NewChangeDirectory", "../ncd"):
        candidate = (script_dir / rel).resolve()
        if candidate.is_file():
            return str(candidate)

    raise RuntimeError(
        "NCD binary not found. Install NCD or set NCD_BINARY_PATH environment variable."
    )


# Cache the binary path so we fail fast on startup
_NCD_BINARY: str | None = None


def _get_ncd_binary() -> str:
    global _NCD_BINARY
    if _NCD_BINARY is None:
        _NCD_BINARY = _find_ncd()
    return _NCD_BINARY


# ---------------------------------------------------------------------------
# Subprocess helper
# ---------------------------------------------------------------------------

def _run_ncd_agent(
    agent_cmd: str,
    *extra_args: str,
    use_json: bool = True,
) -> dict | str:
    """Run an NCD agent-mode command and return the parsed output."""
    binary = _get_ncd_binary()
    cmd: list[str] = [binary, f"--agent:{agent_cmd}"]
    cmd.extend(extra_args)
    if use_json:
        cmd.append("--json")

    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )

    stdout = result.stdout.strip()
    stderr = result.stderr.strip()

    if result.returncode != 0:
        err_msg = f"NCD exited with code {result.returncode}"
        if stderr:
            err_msg += f"\n{stderr}"
        if stdout:
            err_msg += f"\n{stdout}"
        raise RuntimeError(err_msg)

    if use_json and stdout:
        try:
            return json.loads(stdout)
        except json.JSONDecodeError:
            # Fallback to raw text if JSON parse fails
            return stdout

    return stdout


# ---------------------------------------------------------------------------
# FastMCP server
# ---------------------------------------------------------------------------

mcp = FastMCP(SERVER_NAME)


@mcp.tool()
def ncd_query(search: str) -> list[TextContent]:
    """Search the NCD indexed database for directories matching the search term."""
    result = _run_ncd_agent("query", search)
    return [TextContent(type="text", text=json.dumps(result, indent=2))]


@mcp.tool()
def ncd_ls(path: str, depth: int = 1) -> list[TextContent]:
    """List live filesystem contents at the given path."""
    result = _run_ncd_agent("ls", path, "--depth", str(depth))
    return [TextContent(type="text", text=json.dumps(result, indent=2))]


@mcp.tool()
def ncd_tree(path: str, depth: int = 3, flat: bool = False) -> list[TextContent]:
    """Show directory structure from the NCD database."""
    args = [path, "--depth", str(depth)]
    if flat:
        args.append("--flat")
    result = _run_ncd_agent("tree", *args)
    return [TextContent(type="text", text=json.dumps(result, indent=2))]


@mcp.tool()
def ncd_check(
    path: str = "",
    db_age: bool = False,
    stats: bool = False,
    service_status: bool = False,
) -> list[TextContent]:
    """Check path existence, database age, stats, or service status."""
    if service_status:
        args = ["--service-status"]
    elif db_age:
        args = ["--db-age"]
    elif stats:
        args = ["--stats"]
    elif path:
        args = [path]
    else:
        args = []
    result = _run_ncd_agent("check", *args)
    return [TextContent(type="text", text=json.dumps(result, indent=2))]


@mcp.tool()
def ncd_complete(partial: str) -> list[TextContent]:
    """Return completion candidates for a partial directory name."""
    result = _run_ncd_agent("complete", partial)
    return [TextContent(type="text", text=json.dumps(result, indent=2))]


@mcp.tool()
def ncd_mkdir(
    path: str,
    force: bool = False,
    mode: str = "",
    verify: bool = False,
) -> list[TextContent]:
    """Create a single directory."""
    args = [path]
    if force:
        args.append("--force")
    if mode:
        args.extend(["--mode", mode])
    if verify:
        args.append("--verify")
    result = _run_ncd_agent("mkdir", *args)
    return [TextContent(type="text", text=json.dumps(result, indent=2))]


@mcp.tool()
def ncd_mkdirs(
    content: str,
    atomic: bool = False,
    verify: bool = False,
) -> list[TextContent]:
    """Create a directory tree from a flat text or JSON specification."""
    args = [content]
    if atomic:
        args.append("--atomic")
    if verify:
        args.append("--verify")
    result = _run_ncd_agent("mkdirs", *args)
    return [TextContent(type="text", text=json.dumps(result, indent=2))]


@mcp.tool()
def ncd_rmdir(path: str, force: bool = False) -> list[TextContent]:
    """Remove a single empty directory."""
    args = [path]
    if force:
        args.append("--force")
    result = _run_ncd_agent("rmdir", *args)
    return [TextContent(type="text", text=json.dumps(result, indent=2))]


@mcp.tool()
def ncd_rmdirs(path: str, force: bool = False) -> list[TextContent]:
    """Recursively remove a directory tree."""
    args = [path]
    if force:
        args.append("--force")
    result = _run_ncd_agent("rmdirs", *args)
    return [TextContent(type="text", text=json.dumps(result, indent=2))]


@mcp.tool()
def ncd_mv(src: str, dst: str, force: bool = False) -> list[TextContent]:
    """Move or rename a directory."""
    args = [src, dst]
    if force:
        args.append("--force")
    result = _run_ncd_agent("mv", *args)
    return [TextContent(type="text", text=json.dumps(result, indent=2))]


@mcp.tool()
def ncd_ln(target: str, link: str) -> list[TextContent]:
    """Create a symbolic link."""
    result = _run_ncd_agent("ln", target, link)
    return [TextContent(type="text", text=json.dumps(result, indent=2))]


@mcp.tool()
def ncd_verify(
    path: str,
    empty: bool = False,
    mode: str = "",
    tree_spec: str = "",
) -> list[TextContent]:
    """Verify directory properties."""
    args = [path]
    if empty:
        args.append("--empty")
    if mode:
        args.extend(["--mode", mode])
    if tree_spec:
        args.extend(["--tree", tree_spec])
    result = _run_ncd_agent("verify", *args)
    return [TextContent(type="text", text=json.dumps(result, indent=2))]


@mcp.tool()
def ncd_chmod(
    path: str,
    mode: str,
    recursive: bool = False,
) -> list[TextContent]:
    """Change directory permissions (Linux only)."""
    args = [path, "--mode", mode]
    if recursive:
        args.append("--recursive")
    result = _run_ncd_agent("chmod", *args)
    return [TextContent(type="text", text=json.dumps(result, indent=2))]


@mcp.tool()
def ncd_help() -> list[TextContent]:
    """Show the NCD agent-mode help text."""
    result = _run_ncd_agent("help", use_json=False)
    return [TextContent(type="text", text=str(result))]


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
