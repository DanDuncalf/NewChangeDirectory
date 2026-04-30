#!/bin/bash
set -e

# build.sh -- Build NewChangeDirectory for Linux
# Supports x64 (amd64), ARM64, and RISC-V architectures
#
# Usage:
#   ./build.sh              # Build for host architecture
#   ./build.sh x64          # Build for x64 only
#   ./build.sh arm64        # Build for ARM64 only
#   ./build.sh riscv64      # Build for RISC-V only
#   ./build.sh all          # Build for all supported architectures
#
# Environment variables:
#   CC                      # Compiler to use (default: gcc)
#   CC_ARM64                # ARM64 cross-compiler (default: aarch64-linux-gnu-gcc)
#   CC_RISCV                # RISC-V cross-compiler (default: riscv64-linux-gnu-gcc)

# Detect host architecture
HOST_ARCH=$(uname -m)
case "$HOST_ARCH" in
    x86_64|amd64)
        HOST_ARCH="x64"
        ;;
    aarch64|arm64)
        HOST_ARCH="arm64"
        ;;
    riscv64)
        HOST_ARCH="riscv64"
        ;;
    *)
        HOST_ARCH="unknown"
        ;;
esac

# Parse arguments
BUILD_X64=0
BUILD_ARM64=0
BUILD_RISCV=0
BUILD_DEBUG=0
BUILD_TEST=0
TARGET_ARCH="${1:-host}"

# Check for "test" argument (can be first or second)
if [ "$TARGET_ARCH" = "test" ] || [ "${2:-}" = "test" ]; then
    BUILD_TEST=1
    # When "test" is first arg, default to host arch
    if [ "$TARGET_ARCH" = "test" ]; then
        TARGET_ARCH="host"
    fi
fi

case "$TARGET_ARCH" in
    x64|amd64|x86_64)
        BUILD_X64=1
        ;;
    arm64|aarch64)
        BUILD_ARM64=1
        ;;
    riscv64)
        BUILD_RISCV=1
        ;;
    all|both)
        BUILD_X64=1
        BUILD_ARM64=1
        BUILD_RISCV=1
        ;;
    debug)
        BUILD_DEBUG=1
        if [ "$HOST_ARCH" = "arm64" ]; then
            BUILD_ARM64=1
        elif [ "$HOST_ARCH" = "riscv64" ]; then
            BUILD_RISCV=1
        else
            BUILD_X64=1
        fi
        ;;
    host|*)
        if [ "$HOST_ARCH" = "arm64" ]; then
            BUILD_ARM64=1
        elif [ "$HOST_ARCH" = "riscv64" ]; then
            BUILD_RISCV=1
        else
            BUILD_X64=1
        fi
        ;;
esac

# Check for "debug" as second argument
if [ "${2:-}" = "debug" ] || [ "${3:-}" = "debug" ]; then
    BUILD_DEBUG=1
fi

# Compilers
CC_X64="${CC:-gcc}"
CC_ARM64="${CC_ARM64:-aarch64-linux-gnu-gcc}"
CC_RISCV="${CC_RISCV:-riscv64-linux-gnu-gcc}"

# Output names
OUT_X64="${OUT:-NewChangeDirectory}"
OUT_ARM64="${OUT_ARM64:-NewChangeDirectory_arm64}"
OUT_RISCV="${OUT_RISCV:-NewChangeDirectory_riscv64}"
SERVICE_OUT_X64="${SERVICE_OUT:-NCDService}"
SERVICE_OUT_ARM64="${SERVICE_OUT_ARM64:-NCDService_arm64}"
SERVICE_OUT_RISCV="${SERVICE_OUT_RISCV:-NCDService_riscv64}"

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="${SCRIPT_DIR}/src"
SHARED_DIR="${SCRIPT_DIR}/../shared"

# Common source files (shared between main executable and service)
COMMON_SOURCES=(
    "${SRC_DIR}/database.c"
    "${SRC_DIR}/scanner.c"
    "${SRC_DIR}/matcher.c"
    "${SRC_DIR}/platform_ncd.c"
    "${SRC_DIR}/cli.c"
    "${SRC_DIR}/result.c"
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

# Base compiler flags
# -Wno-format-truncation: snprintf to fixed buffers is intentional design
# -Wno-stringop-truncation: strncpy with proper null-termination is safe
if [ $BUILD_DEBUG -eq 1 ]; then
    BASE_CFLAGS="-std=c11 -Wall -Wextra -Wno-format-truncation -Wno-stringop-truncation -O0 -g3 -DDEBUG -D_GNU_SOURCE"
elif [ $BUILD_TEST -eq 1 ]; then
    BASE_CFLAGS="-std=c11 -Wall -Wextra -Wno-format-truncation -Wno-stringop-truncation -O2 -DNDEBUG -DNCD_TEST_BUILD -D_GNU_SOURCE"
else
    BASE_CFLAGS="-std=c11 -Wall -Wextra -Wno-format-truncation -Wno-stringop-truncation -O2 -DNDEBUG -D_GNU_SOURCE"
fi

# Architecture-specific flags
CFLAGS_X64="${BASE_CFLAGS} -march=x86-64-v2"
CFLAGS_ARM64="${BASE_CFLAGS} -march=armv8-a"
CFLAGS_RISCV="${BASE_CFLAGS} -march=rv64gc"

echo "========================================"
echo "Build Configuration"
echo "========================================"
echo "Host architecture: $HOST_ARCH"
echo "Build x64: $BUILD_X64"
echo "Build ARM64: $BUILD_ARM64"
echo "Build RISC-V: $BUILD_RISCV"
echo ""

# Function to check if cross-compiler exists
check_compiler() {
    local compiler="$1"
    local arch="$2"
    
    if ! command -v "$compiler" &> /dev/null; then
        echo "ERROR: Compiler '$compiler' not found for $arch build"
        echo "Install it with:"
        echo "  sudo apt-get install gcc-aarch64-linux-gnu (for ARM64)"
        echo "  sudo apt-get install gcc-riscv64-linux-gnu (for RISC-V)"
        return 1
    fi
    return 0
}

# Function to build for a specific architecture
build_arch() {
    local arch="$1"
    local cc="$2"
    local cflags="$3"
    local out="$4"
    local service_out="$5"
    
    echo ""
    echo "========================================"
    echo "Building $arch version..."
    echo "========================================"
    echo "Compiler: $cc"
    echo "Output: $out"
    
    # Check compiler exists
    if ! check_compiler "$cc" "$arch"; then
        return 1
    fi
    
    # Build main executable
    echo "Building ${out}..."
    "$cc" $cflags -I"${SRC_DIR}" -I"${SHARED_DIR}" \
        "${SRC_DIR}/main.c" \
        "${SRC_DIR}/ui.c" \
        "${COMMON_SOURCES[@]}" \
        -o "${out}" -lpthread
    echo "Build successful: ${out}"
    
    # Build service executable
    echo ""
    echo "Building ${service_out}..."
    "$cc" $cflags -I"${SRC_DIR}" -I"${SHARED_DIR}" \
        "${SRC_DIR}/service_main.c" \
        "${SRC_DIR}/ui.c" \
        "${COMMON_SOURCES[@]}" \
        -o "${service_out}" -lpthread
    echo "Build successful: ${service_out}"
}

# Build x64 version
if [ $BUILD_X64 -eq 1 ]; then
    build_arch "x64" "$CC_X64" "$CFLAGS_X64" "$OUT_X64" "$SERVICE_OUT_X64"
    if [ $? -ne 0 ]; then
        echo "ERROR: x64 build failed"
        exit 1
    fi
fi

# Build ARM64 version
if [ $BUILD_ARM64 -eq 1 ]; then
    build_arch "ARM64" "$CC_ARM64" "$CFLAGS_ARM64" "$OUT_ARM64" "$SERVICE_OUT_ARM64"
    if [ $? -ne 0 ]; then
        echo "WARNING: ARM64 build failed (cross-compiler may not be installed)"
        # Don't fail if ARM64 cross-compile fails on x64 host
        if [ "$HOST_ARCH" = "arm64" ]; then
            exit 1
        fi
    fi
fi

# Build RISC-V version
if [ $BUILD_RISCV -eq 1 ]; then
    build_arch "RISC-V" "$CC_RISCV" "$CFLAGS_RISCV" "$OUT_RISCV" "$SERVICE_OUT_RISCV"
    if [ $? -ne 0 ]; then
        echo "WARNING: RISC-V build failed (cross-compiler may not be installed)"
        # Don't fail if RISC-V cross-compile fails on non-RISC-V host
        if [ "$HOST_ARCH" = "riscv64" ]; then
            exit 1
        fi
    fi
fi

echo ""
echo "========================================"
echo "Build Summary"
echo "========================================"
if [ $BUILD_X64 -eq 1 ]; then
    echo "x64: $OUT_X64, $SERVICE_OUT_X64"
fi
if [ $BUILD_ARM64 -eq 1 ]; then
    echo "ARM64: $OUT_ARM64, $SERVICE_OUT_ARM64"
fi
if [ $BUILD_RISCV -eq 1 ]; then
    echo "RISC-V: $OUT_RISCV, $SERVICE_OUT_RISCV"
fi
echo "========================================"
