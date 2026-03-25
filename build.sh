#!/usr/bin/env bash
set -euo pipefail

CC=${CC:-gcc}
OUT=${OUT:-NewChangeDirectory}
SERVICE_OUT=${SERVICE_OUT:-NCDService}

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="${SCRIPT_DIR}/src"
SHARED_DIR="${SCRIPT_DIR}/../shared"

# Common source files (shared between main executable and service)
COMMON_SOURCES=(
    "${SRC_DIR}/database.c"
    "${SRC_DIR}/scanner.c"
    "${SRC_DIR}/matcher.c"
    "${SRC_DIR}/platform.c"
    "${SRC_DIR}/state_backend_local.c"
    "${SRC_DIR}/state_backend_service.c"
    "${SRC_DIR}/shared_state.c"
    "${SRC_DIR}/shm_platform_posix.c"
    "${SRC_DIR}/control_ipc_posix.c"
    "${SRC_DIR}/service_state.c"
    "${SRC_DIR}/service_publish.c"
    "${SHARED_DIR}/platform.c"
    "${SHARED_DIR}/strbuilder.c"
    "${SHARED_DIR}/common.c"
)

echo "Building ${OUT} with ${CC}..."
"${CC}" -std=c11 -Wall -Wextra -O2 -DNDEBUG -D_GNU_SOURCE -I"${SRC_DIR}" -I"${SHARED_DIR}" \
    "${SRC_DIR}/main.c" \
    "${SRC_DIR}/ui.c" \
    "${COMMON_SOURCES[@]}" \
    -o "${OUT}" -lpthread
echo "Build successful: ${OUT}"

echo ""
echo "Building ${SERVICE_OUT} with ${CC}..."
"${CC}" -std=c11 -Wall -Wextra -O2 -DNDEBUG -D_GNU_SOURCE -I"${SRC_DIR}" -I"${SHARED_DIR}" \
    "${SRC_DIR}/service_main.c" \
    "${COMMON_SOURCES[@]}" \
    -o "${SERVICE_OUT}" -lpthread
echo "Build successful: ${SERVICE_OUT}"
