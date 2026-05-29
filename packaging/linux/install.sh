#!/usr/bin/env bash
#
# NCD Linux Installer
#
# Usage: ./install.sh [destination_path]
# Default: /usr/local/bin
#
# This script installs NCD binaries, wrapper scripts, and shell completions.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST_DIR="${1:-/usr/local/bin}"

echo "========================================"
echo "NCD Installer for Linux"
echo "========================================"
echo ""
echo "Install directory: ${DEST_DIR}"
echo ""

# Detect host architecture
HOST_ARCH="$(uname -m)"
case "$HOST_ARCH" in
    x86_64|amd64) HOST_ARCH="x64" ;;
    aarch64|arm64) HOST_ARCH="arm64" ;;
    riscv64) HOST_ARCH="riscv64" ;;
    *) HOST_ARCH="unknown" ;;
esac

# Detect package architecture from included binaries
PKG_ARCH="x64"
if [[ -f "${SCRIPT_DIR}/NewChangeDirectory_arm64" ]]; then
    PKG_ARCH="arm64"
elif [[ -f "${SCRIPT_DIR}/NewChangeDirectory_riscv64" ]]; then
    PKG_ARCH="riscv64"
fi

echo "Host architecture: ${HOST_ARCH}"
echo "Package architecture: ${PKG_ARCH}"
echo ""

# Warn if architectures don't match
if [[ "$HOST_ARCH" != "$PKG_ARCH" ]]; then
    echo "WARNING: Package architecture (${PKG_ARCH}) does not match host (${HOST_ARCH})."
    echo "The binaries will be installed but may not run on this machine."
    echo ""
    read -rp "Continue anyway? [y/N] " confirm
    if [[ ! "$confirm" =~ ^[Yy]$ ]]; then
        echo "Installation cancelled."
        exit 1
    fi
    echo ""
fi

# Check if we need sudo for the destination
SUDO=""
if [[ ! -w "$DEST_DIR" ]]; then
    if command -v sudo &> /dev/null; then
        SUDO="sudo"
    else
        echo "ERROR: Cannot write to ${DEST_DIR} and sudo is not available."
        echo "Run as root or specify a writable directory:"
        echo "  ./install.sh ~/.local/bin"
        exit 1
    fi
fi

# Create destination directory
$SUDO mkdir -p "$DEST_DIR"

# Stop any running service before overwriting
if pgrep -x "NCDService" > /dev/null 2>&1; then
    echo "Stopping NCD service..."
    $SUDO pkill -x "NCDService" 2>/dev/null || true
    sleep 1
fi

echo "Installing binaries..."

# Install the correct architecture binary
if [[ "$PKG_ARCH" == "arm64" ]]; then
    $SUDO cp -f "${SCRIPT_DIR}/NewChangeDirectory_arm64" "${DEST_DIR}/NewChangeDirectory"
    $SUDO cp -f "${SCRIPT_DIR}/NCDService_arm64" "${DEST_DIR}/NCDService"
elif [[ "$PKG_ARCH" == "riscv64" ]]; then
    $SUDO cp -f "${SCRIPT_DIR}/NewChangeDirectory_riscv64" "${DEST_DIR}/NewChangeDirectory"
    $SUDO cp -f "${SCRIPT_DIR}/NCDService_riscv64" "${DEST_DIR}/NCDService"
else
    $SUDO cp -f "${SCRIPT_DIR}/NewChangeDirectory" "${DEST_DIR}/NewChangeDirectory"
    $SUDO cp -f "${SCRIPT_DIR}/NCDService" "${DEST_DIR}/NCDService"
fi

$SUDO chmod 755 "${DEST_DIR}/NewChangeDirectory"
$SUDO chmod 755 "${DEST_DIR}/NCDService"

# Install wrapper scripts
$SUDO cp -f "${SCRIPT_DIR}/ncd" "${DEST_DIR}/ncd"
$SUDO chmod 755 "${DEST_DIR}/ncd"

$SUDO cp -f "${SCRIPT_DIR}/ncd_service" "${DEST_DIR}/ncd_service"
$SUDO chmod 755 "${DEST_DIR}/ncd_service"

echo "  [OK] Binaries installed"
echo ""

# Install shell completions
echo "Installing shell completions..."

BASH_COMP_DIR="/usr/local/share/bash-completion/completions"
ZSH_COMP_DIR="/usr/local/share/zsh/site-functions"

if [[ -d "${SCRIPT_DIR}/completions" ]]; then
    if $SUDO mkdir -p "$BASH_COMP_DIR" 2>/dev/null; then
        if [[ -f "${SCRIPT_DIR}/completions/ncd.bash" ]]; then
            $SUDO cp -f "${SCRIPT_DIR}/completions/ncd.bash" "${BASH_COMP_DIR}/ncd"
            echo "  [OK] Bash completions installed"
        fi
    fi

    if $SUDO mkdir -p "$ZSH_COMP_DIR" 2>/dev/null; then
        if [[ -f "${SCRIPT_DIR}/completions/_ncd" ]]; then
            $SUDO cp -f "${SCRIPT_DIR}/completions/_ncd" "${ZSH_COMP_DIR}/_ncd"
            echo "  [OK] Zsh completions installed"
        fi
    fi
fi

echo ""

# Add ncd() function to ~/.bashrc if not present
BASHRC="${HOME}/.bashrc"
NCD_FUNC="ncd() { source ${DEST_DIR}/ncd \"\$@\"; }"

if [[ -f "$BASHRC" ]]; then
    if grep -qF "ncd() { source ${DEST_DIR}/ncd" "$BASHRC" 2>/dev/null; then
        echo "ncd() function already in ${BASHRC}"
    else
        echo "Adding ncd() function to ${BASHRC}..."
        {
            echo ""
            echo "# NCD - Norton Change Directory"
            echo "$NCD_FUNC"
        } >> "$BASHRC"
        echo "  [OK] Added to ${BASHRC}"
        echo ""
        echo "Run 'source ${BASHRC}' or open a new terminal to use ncd."
    fi
else
    echo "Note: ${BASHRC} not found. Add this function manually:"
    echo "  ${NCD_FUNC}"
fi

echo ""

# Optional: Install MCP server
if command -v python3 &> /dev/null && command -v pip3 &> /dev/null; then
    echo "Python detected. Install MCP server?"
    echo "  This enables LLM integration (Claude, etc.) with NCD."
    read -rp "Install ncd-mcp-server? [y/N] " install_mcp
    if [[ "$install_mcp" =~ ^[Yy]$ ]]; then
        pip3 install --user "${SCRIPT_DIR}/mcp_server" 2>/dev/null || sudo pip3 install "${SCRIPT_DIR}/mcp_server"
        echo "  [OK] MCP server installed. Run 'ncd-mcp-server' to verify."
        echo "  Add this to your MCP client config (e.g., Claude Desktop):"
        echo '    { "mcpServers": { "ncd": { "command": "ncd-mcp-server" } } }'
    fi
else
    echo ""
    echo "Note: Python 3 + pip not found. Skipping MCP server installation."
    echo "  To install later: pip3 install /path/to/this/mcp_server"
fi

# Optional: Install systemd service
if [[ -d "${SCRIPT_DIR}" && -f "${SCRIPT_DIR}/ncd.service" ]]; then
    if command -v systemctl &> /dev/null; then
        echo ""
        read -rp "Install NCD as a systemd system service? [y/N] " install_systemd
        if [[ "$install_systemd" =~ ^[Yy]$ ]]; then
            $SUDO cp -f "${SCRIPT_DIR}/ncd.service" /etc/systemd/system/ncd.service
            $SUDO sed -i "s|/usr/local/bin|${DEST_DIR}|g" /etc/systemd/system/ncd.service
            $SUDO systemctl daemon-reload
            $SUDO systemctl enable ncd.service
            $SUDO systemctl start ncd.service
            echo "  [OK] systemd service installed and started"
            echo "  Check status: sudo systemctl status ncd"
        fi
    else
        echo ""
        echo "Note: systemd not found. Skipping service installation."
        echo "  To start the service manually: ncd_service start"
    fi
fi

echo ""
echo "========================================"
echo "Installation Complete"
echo "========================================"
echo ""
echo "Installed to: ${DEST_DIR}"
echo ""
echo "Usage:"
echo "  ncd <search>         - Navigate to a directory"
echo "  ncd_service start    - Start the resident service"
echo "  ncd -?               - Show help"
echo ""
echo "To uninstall:"
echo "  sudo rm -f ${DEST_DIR}/ncd ${DEST_DIR}/NewChangeDirectory ${DEST_DIR}/NCDService ${DEST_DIR}/ncd_service"
if command -v systemctl &> /dev/null && [[ -f /etc/systemd/system/ncd.service ]]; then
    echo "  sudo systemctl stop ncd"
    echo "  sudo systemctl disable ncd"
    echo "  sudo rm -f /etc/systemd/system/ncd.service"
fi
echo ""
