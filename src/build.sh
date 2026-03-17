#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SRCDIR="$ROOT/src"

if [ ! -d "$SRCDIR" ]; then
  echo "Error: src directory not found in $ROOT"
  exit 1
fi

CC=${CC:-gcc}
CFLAGS=${CFLAGS:-"-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter"}
LDFLAGS=""

UNAME=$(uname -s 2>/dev/null || echo Unknown)
case "$UNAME" in
  *Linux*|*GNU*)
    LDFLAGS="$LDFLAGS -pthread"
    ;;
  *Darwin*)
    # macOS: pthreads available by default
    ;;
  *CYGWIN*|*MINGW*|*MSYS*)
    # On Windows/MSYS prefer invoking MSVC via the Visual Studio developer
    # prompt / use the provided solution. Fall back to gcc/clang if available.
    ;;
  *) ;;
esac

echo "Building NewChangeDirectory with $CC..."

# Compile
$CC $CFLAGS -I"$SRCDIR" "$SRCDIR"/*.c -o ncd $LDFLAGS

if [ $? -eq 0 ]; then
  echo "Build successful: $ROOT/ncd"
else
  echo "Build failed"
  exit 1
fi
