#!/bin/bash
#
# deploy_system.sh - Deploy NCD to /usr/local/bin
#

set -e

SCRIPT_DIR="/mnt/e/llama/NewChangeDirectory"
DEST_DIR="/usr/local/bin"

echo "=== NCD System Deployment ==="
echo ""

# Stop old service if running
if pgrep -x "NCDService" > /dev/null 2>&1; then
    echo "Stopping old NCD service..."
    "${SCRIPT_DIR}/NewChangeDirectory" --agent:quit 2>/dev/null || true
    sleep 2
    pkill -9 -x "NCDService" 2>/dev/null || true
fi

# Copy binaries to /usr/local/bin
echo "Deploying to ${DEST_DIR}..."
sudo cp "${SCRIPT_DIR}/NewChangeDirectory" "${DEST_DIR}/"
sudo cp "${SCRIPT_DIR}/NCDService" "${DEST_DIR}/"
sudo cp "${SCRIPT_DIR}/ncd" "${DEST_DIR}/"
sudo chmod 755 "${DEST_DIR}/NewChangeDirectory" "${DEST_DIR}/NCDService" "${DEST_DIR}/ncd"

# Verify
echo ""
echo "=== Verification ==="
"${DEST_DIR}/NewChangeDirectory" -v

echo ""
echo "=== Deployment Complete ==="
echo "Binaries installed to: ${DEST_DIR}"
echo ""
echo "To use: ncd -?"
