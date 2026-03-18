#!/usr/bin/env bash
#
# deploy.sh  --  Deploy NCD to /usr/local/bin
#
# This script copies both the ncd wrapper script and the NewChangeDirectory
# binary to /usr/local/bin and sets appropriate permissions.
#
# Usage:
#   ./deploy.sh
#   sudo ./deploy.sh
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST_DIR="/usr/local/bin"

echo "Deploying NCD to ${DEST_DIR}..."

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

echo ""
echo "Deployment complete!"
echo ""
echo "Usage: ncd <search>"
