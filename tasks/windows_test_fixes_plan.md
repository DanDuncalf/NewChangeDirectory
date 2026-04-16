# Windows Integration Test Fixes — Plan

## Remaining Failures (as of 2026-04-15)

| Test File | ID | Description | Root Cause |
|-----------|----|-------------|------------|
| `test\Win\test_features.bat` | I10a | Search excludes Deep | PowerShell `Start-Process -ArgumentList` mangles batch-expanded `%CONF_OVERRIDE%` (starts with `-conf`), causing NCD to run without the isolated config path. It loads the wrong metadata and the exclusion is ignored. |
| `test\Win\test_features.bat` | P1 | Spaces in dir name | Same PowerShell argument mangling; NCD receives garbled args, times out or fails to create result file. |
| `test\Win\test_features.bat` | V5 | Agent tree --depth limits depth | `TESTROOT` is a drive root (`T:\`). Batch `"%TESTROOT%"` expands to `"T:\"`, which escapes the closing quote. NCD receives a corrupted path argument. |
| `test\Win\test_agent_commands.bat` | W7 | ls --dirs-only works | Test looks for JSON key `"n"` in `agent_mode_ls` output, but `ls` outputs `"name"`. Only `tree` uses compact `"n"`. |
| `test\Win\test_agent_commands.bat` | W12 | tree --depth limits results | Same `"T:\"` batch quoting escape as V5. |

## Proposed Fixes

### 1. Source: `src/main.c` — Windows path normalization for agent commands
Add a small normalization step in `agent_mode_tree`, `agent_mode_ls`, and `agent_mode_check` to strip a trailing `.` that follows a backslash (e.g., `T:\.` → `T:\`). This works around the batch `"T:\"` escaping issue without changing any Linux behavior (wrapped in `#if NCD_PLATFORM_WINDOWS`).

### 2. Tests: `test\Win\test_features.bat`
- **I10a**: Replace the `:test_ncd_no_match` helper call with a direct batch invocation for this specific test. `L10` is unique in the DB, so NCD returns immediately; no TUI timeout risk.
- **P1**: Replace the `:test_ncd_finds` helper call with a direct batch invocation that checks for the result file. `"dir with spaces"` is unique in the DB.
- **V5**: Append `.` to `TESTROOT` in the `agent tree` invocations (`"%TESTROOT%."`), relying on the new source normalization.

### 3. Tests: `test\Win\test_agent_commands.bat`
- **W7**: Change `findstr /C:"\"n\""` to `findstr /I "name"` to match the actual JSON key emitted by `agent_mode_ls`.
- **W12**: Append `.` to `TESTROOT` in the `agent tree` invocations (`"%TESTROOT%."`).

### 4. Documentation updates
- **`AGENT_TESTING_GUIDE.md`**: Add a "Known Windows Batch Pitfalls" section covering:
  - The `"T:\"` quote-escape issue and the `T:\.` workaround.
  - The PowerShell `Start-Process -ArgumentList` hazard when the argument string starts with `-` (like `-conf`), which causes batch variable expansion to break quoting.
- **`test/README.md`**: Document the JSON field differences between agent subcommands (`tree` uses `"n"`/`"d"`; `ls` uses `"name"`/`"is_dir"`).

## Files to Modify
- `src/main.c`
- `test/Win/test_features.bat`
- `test/Win/test_agent_commands.bat`
- `AGENT_TESTING_GUIDE.md`
- `test/README.md`
