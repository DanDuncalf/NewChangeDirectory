#!/usr/bin/env bash
set -euo pipefail

CC=${CC:-gcc}
OUT=${OUT:-NewChangeDirectory}

echo "Building ${OUT} with ${CC}..."
"${CC}" -std=c11 -Wall -Wextra -O2 -DNDEBUG -D_GNU_SOURCE -Isrc src/main.c src/database.c src/scanner.c src/matcher.c src/ui.c src/platform.c -o "${OUT}" -lpthread
echo "Build successful: ${OUT}"
