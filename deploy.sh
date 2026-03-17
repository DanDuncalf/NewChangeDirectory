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

# Copy and set permissions for the binary
echo "  Installing: NewChangeDirectory -> ${DEST_DIR}/NewChangeDirectory"
${SUDO} cp "${SCRIPT_DIR}/NewChangeDirectory" "${DEST_DIR}/NewChangeDirectory"
${SUDO} chmod 755 "${DEST_DIR}/NewChangeDirectory"

echo ""
echo "Deployment complete!"
echo ""
echo "Add this function to your ~/.bashrc to use 'ncd' directly:"
echo "  ncd() { source ${DEST_DIR}/ncd \"\$@\"; }"
echo ""
echo "Or use: source ${DEST_DIR}/ncd <search>"
