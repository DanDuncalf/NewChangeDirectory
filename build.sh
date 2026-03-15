#!/usr/bin/env bash
set -euo pipefail

# Linux x64 build entrypoint (GCC)
# Usage:
#   ./build.sh
#   CC=clang ./build.sh

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "This build script is intended for Linux." >&2
  exit 1
fi

if [[ "$(uname -m)" != "x86_64" ]]; then
  echo "Expected x86_64 Linux (got: $(uname -m))." >&2
  exit 1
fi

CC=${CC:-gcc}
OUT=${OUT:-NewChangeDirectory}

SRC=(
  src/main.c
  src/database.c
  src/scanner.c
  src/matcher.c
  src/ui.c
  src/platform.c
)

CFLAGS=(
  -std=c11
  -Wall
  -Wextra
  -O2
  -DNDEBUG
  -D_GNU_SOURCE
  -Isrc
)

LDFLAGS=(
  -lpthread
)

echo "Building ${OUT} with ${CC}..."
"${CC}" "${CFLAGS[@]}" "${SRC[@]}" -o "${OUT}" "${LDFLAGS[@]}"
echo "Build successful: ${OUT}"
