# GitHub Copilot Instructions for NewChangeDirectory

## Project Purpose
NewChangeDirectory (NCD) is a lightweight command-line directory switching utility for Windows and Linux. It maintains a path database and supports fuzzy path matching + heuristics to accelerate `cd` workflows.  It also has agentic support through the /agent option for AI-assisted path management.

## Recommended agent workflow
- Use repository-level rules in `coding_agent_rules.md` / `developer_agent_rules.md` (Plan, Subagent, Verify, Elegance).
- Keep fixes minimal, with one semantic change per PR.
- When adding features, add appropriate user facing tests, and unit tests investigate how to do this by reading .md files in the test folder.
- Run the automated tests which are found in the tests/ directory after each change.  Fix any issues before submitting.

## Build and run
### Windows
- `build.bat` (requires VS Native Tools prompt + Visual Studio 2019/2022).
- Produces `NewChangeDirectory.exe`.

### Linux
- `build.sh` (x86_64 Linux, gcc/clang).
- `CC=clang ./build.sh` optional.
- Produces `NewChangeDirectory`.

### Manual validation
- `ncd.bat` wrapper available for Windows usage.
- CLI semantics in `src/main.c` header comment.

## Key code areas
- `src/main.c`: entry point, command-line parsing, result file output via `%TEMP%/ncd_result.bat`.
- `src/database.c`, `src/scanner.c`, `src/matcher.c`: core DB build and path matching logic.
- `src/ui.c`: user-facing output and formatting.
- `src/platform.c`: platform compatibility macros/abstractions.
- `src/ncd.h`: shared API and constants (e.g., `NCD_MAX_PATH`).

## Coding conventions
- C language: C11 on Windows (`/std:c11`) and Linux (`-std=c11`).
- Use `bool`, explicit checks for null pointers, and stable string-handling helpers in `platform.c` (e.g., `platform_strncpy_s`).
- Follow existing comment style: top-of-file license/usage block and section markers (`/* ============================================================= ... */`).

## Common pitfalls in this filter
- Maintain cross-platform path separators and case sensitivity branches (Windows vs POSIX) in `platform.c` and `scanner.c`.
- Preserve memory limits, static buffers, and no dynamic allocation assumptions for embedded-like logic.
- Keep heuristics file handling (`ncd.history`) in sync with platform newline conventions.

## Style pointers for AI suggestions
- Prefer API-preserving, localized fixes in `src/*`.
- If change requires algorithmic redesign, create a short architecture note comment.
- Do not convert to a large C++ rewrite; keep C11 idiom.

