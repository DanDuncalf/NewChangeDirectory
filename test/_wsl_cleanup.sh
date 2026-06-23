#!/bin/bash
pkill -f ncd_service 2>/dev/null
pkill -f ncd 2>/dev/null
sleep 1
echo "WSL ncd processes:"
pgrep -af ncd 2>/dev/null || echo "(none)"
