#!/usr/bin/env bash
set -euo pipefail

CC=${CC:-gcc}
OUT=${OUT:-NewChangeDirectory}

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="${SCRIPT_DIR}/src"
SHARED_DIR="${SCRIPT_DIR}/../shared"

echo "Building ${OUT} with ${CC}..."
"${CC}" -std=c11 -Wall -Wextra -O2 -DNDEBUG -D_GNU_SOURCE -I"${SRC_DIR}" -I"${SHARED_DIR}" \
    "${SRC_DIR}/main.c" \
    "${SRC_DIR}/database.c" \
    "${SRC_DIR}/scanner.c" \
    "${SRC_DIR}/matcher.c" \
    "${SRC_DIR}/ui.c" \
    "${SRC_DIR}/platform.c" \
    "${SHARED_DIR}/platform.c" \
    "${SHARED_DIR}/strbuilder.c" \
    "${SHARED_DIR}/common.c" \
    -o "${OUT}" -lpthread
echo "Build successful: ${OUT}"
