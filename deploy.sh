#!/usr/bin/env bash
#
# deploy.sh  --  Deploy NCD to /usr/local/bin
#
# This script copies both the ncd wrapper script and the NewChangeDirectory
# binary to /usr/local/bin and sets appropriate permissions.
# It also handles service management and version checking.
#
# Usage:
#   ./deploy.sh
#   sudo ./deploy.sh
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST_DIR="/usr/local/bin"

echo "================================================"
echo "NCD Deployment"
echo "================================================"
echo ""

# Check for required files
if [[ ! -f "${SCRIPT_DIR}/ncd" ]]; then
    echo "ERROR: ncd script not found in ${SCRIPT_DIR}" >&2
    exit 1
fi

if [[ ! -f "${SCRIPT_DIR}/NewChangeDirectory" ]]; then
    echo "ERROR: NewChangeDirectory binary not found in ${SCRIPT_DIR}" >&2
    echo "Please run ./build.sh first to build the binary." >&2
    exit 1
fi

# Check for service binary
if [[ -f "${SCRIPT_DIR}/NCDService" ]]; then
    HAS_SERVICE=1
else
    HAS_SERVICE=0
    echo "NOTE: NCDService binary not found, service will not be deployed"
fi

# Get version info
NEW_VERSION=$("${SCRIPT_DIR}/NewChangeDirectory" /v 2>&1 || echo "unknown")
echo "New version: ${NEW_VERSION}"
echo ""

# Check for running service and stop it gracefully
if [[ ${HAS_SERVICE} -eq 1 ]]; then
    if pgrep -x "NCDService" > /dev/null 2>&1; then
        echo "Service is currently running."
        echo "Requesting graceful shutdown..."
        
        # Try graceful shutdown via IPC first
        if "${SCRIPT_DIR}/NewChangeDirectory" /agent quit 2>/dev/null; then
            # Wait up to 10 seconds for service to stop
            for i in {1..10}; do
                if ! pgrep -x "NCDService" > /dev/null 2>&1; then
                    echo "Service stopped gracefully."
                    break
                fi
                sleep 1
            done
        fi
        
        # Force kill if still running
        if pgrep -x "NCDService" > /dev/null 2>&1; then
            echo "Warning: Service did not stop gracefully, forcing termination..."
            pkill -9 -x "NCDService" 2>/dev/null || true
            sleep 1
        fi
        echo ""
    fi
fi

# Check if we need sudo
if [[ ! -w "${DEST_DIR}" ]]; then
    echo "NOTE: Need write permission to ${DEST_DIR}, using sudo..." >&2
    SUDO="sudo"
else
    SUDO=""
fi

# Create dest directory if it doesn't exist
if [[ ! -d "${DEST_DIR}" ]]; then
    ${SUDO} mkdir -p "${DEST_DIR}"
fi

# Copy and set permissions for the wrapper script
echo "  Installing: ncd -> ${DEST_DIR}/ncd"
${SUDO} cp "${SCRIPT_DIR}/ncd" "${DEST_DIR}/ncd"
${SUDO} chmod 755 "${DEST_DIR}/ncd"

# Kill any running NewChangeDirectory processes to avoid "Text file busy"
if pgrep -x "NewChangeDirectory" > /dev/null 2>&1; then
    echo "  Stopping running NewChangeDirectory processes..."
    ${SUDO} pkill -x "NewChangeDirectory" 2>/dev/null || true
    sleep 1
fi

# Copy and set permissions for the binary
echo "  Installing: NewChangeDirectory -> ${DEST_DIR}/NewChangeDirectory"
${SUDO} rm -f "${DEST_DIR}/NewChangeDirectory" 2>/dev/null || true
${SUDO} cp "${SCRIPT_DIR}/NewChangeDirectory" "${DEST_DIR}/NewChangeDirectory"
${SUDO} chmod 755 "${DEST_DIR}/NewChangeDirectory"

# -----------------------------------------------------------------------
# 5.  Add ncd() function to ~/.bashrc if not already present
# -----------------------------------------------------------------------
BASHRC="${HOME}/.bashrc"
NCD_FUNC="ncd() { source ${DEST_DIR}/ncd \"\$@\"; }"

if [[ -f "${BASHRC}" ]]; then
    if grep -q "^ncd() { source ${DEST_DIR}/ncd" "${BASHRC}" 2>/dev/null; then
        echo ""
        echo "ncd() function already exists in ${BASHRC}"
    else
        echo ""
        echo "Adding ncd() function to ${BASHRC}..."
        echo "" >> "${BASHRC}"
        echo "# NCD - Norton Change Directory" >> "${BASHRC}"
        echo "${NCD_FUNC}" >> "${BASHRC}"
        echo ""
        echo "=============================================="
        echo " ncd() function added to ${BASHRC}"
        echo ""
        echo " IMPORTANT: Run this command NOW:"
        echo "   source ${BASHRC}"
        echo ""
        echo " Or open a new terminal."
        echo "=============================================="
    fi
else
    echo ""
    echo "Note: ${BASHRC} not found. Add this function manually:"
    echo "  ${NCD_FUNC}"
fi

# -----------------------------------------------------------------------
# 6.  Install shell completion files
# -----------------------------------------------------------------------
echo ""
echo "Installing shell completion files..."

# Bash completions
BASH_COMPLETION_DIR="/usr/local/share/bash-completion/completions"
if [[ -d "${BASH_COMPLETION_DIR}" ]] || ${SUDO} mkdir -p "${BASH_COMPLETION_DIR}" 2>/dev/null; then
    if [[ -f "${SCRIPT_DIR}/completions/ncd.bash" ]]; then
        ${SUDO} cp "${SCRIPT_DIR}/completions/ncd.bash" "${BASH_COMPLETION_DIR}/ncd"
        echo "  Bash completions installed to ${BASH_COMPLETION_DIR}/ncd"
    fi
else
    echo "  Note: Could not install Bash completions (no permission)"
fi

# Zsh completions
ZSH_COMPLETION_DIR="/usr/local/share/zsh/site-functions"
if [[ -d "${ZSH_COMPLETION_DIR}" ]] || ${SUDO} mkdir -p "${ZSH_COMPLETION_DIR}" 2>/dev/null; then
    if [[ -f "${SCRIPT_DIR}/completions/_ncd" ]]; then
        ${SUDO} cp "${SCRIPT_DIR}/completions/_ncd" "${ZSH_COMPLETION_DIR}/_ncd"
        echo "  Zsh completions installed to ${ZSH_COMPLETION_DIR}/_ncd"
    fi
else
    echo "  Note: Could not install Zsh completions (no permission)"
fi

echo ""
echo "================================================"
echo "Deployment Summary"
echo "================================================"
echo "Destination: ${DEST_DIR}"
echo "  [OK] NewChangeDirectory"
if [[ ${HAS_SERVICE} -eq 1 ]]; then
    echo "  [OK] NCDService"
fi
echo "  [OK] ncd"

# Start the service
if [[ ${HAS_SERVICE} -eq 1 ]]; then
    echo ""
    echo "Starting NCD Service..."
    nohup "${DEST_DIR}/NCDService" > /dev/null 2>&1 &
    sleep 2
    
    if pgrep -x "NCDService" > /dev/null 2>&1; then
        echo "  [OK] Service started successfully"
    else
        echo "  [WARNING] Service may not have started. Check manually."
    fi
fi

echo ""
echo "================================================"
echo "Deployment complete!"
echo "================================================"
echo ""
echo "Usage: ncd <search>"
echo ""
echo "Tab completion:"
echo "  Bash: Restart your shell or run: source ${BASH_COMPLETION_DIR}/ncd"
echo "  Zsh:  Add ${ZSH_COMPLETION_DIR} to your fpath and run: compinit"
