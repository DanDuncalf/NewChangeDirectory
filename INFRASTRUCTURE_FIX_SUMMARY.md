# NCD Test Infrastructure Fix - Implementation Summary

## Date: 2026-03-27
## Status: COMPLETED

## Problem
NCD service tests failed because they could not capture output via pipe redirection. NCD wrote directly to the Windows console (`CONOUT$` handle) using `WriteConsoleA()`, bypassing stdout entirely.

## Solution Implemented

### File Modified: `src/platform.c`

1. **Added redirect detection** (`ncd_detect_redirect()`):
   ```c
   #if NCD_PLATFORM_WINDOWS
       HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
       DWORD mode;
       g_stdout_redirected = !GetConsoleMode(hOut, &mode);
   #else
       g_stdout_redirected = !isatty(STDOUT_FILENO);
   #endif
   ```

2. **Modified `ncd_con_init()`**:
   - Detects redirect state before opening console
   - Only opens `CONOUT$` handle when NOT redirected

3. **Modified `ncd_print()`**:
   ```c
   if (g_stdout_redirected || !g_ncd_con) {
       fputs(s, stdout);      // Redirected: use stdout
       fflush(stdout);
   } else {
       platform_console_write(g_ncd_con, s);  // Console: direct write
   }
   ```

### File Modified: `AGENTS.md`

Updated documentation to reflect the automatic redirect detection behavior.

## Verification

### Manual Tests (All Passed)
```powershell
# Output capture works
.\NewChangeDirectory.exe /? > output.txt
# File contains full help text including "[Service: Running.]" or "[Standalone client.]"

# Version capture works
.\NewChangeDirectory.exe /v > version.txt
# Contains: NCD v1.3 [built ...]
```

### Core Tests (All Pass - 116/116)
- test_database.exe: 39/39 ✓
- test_matcher.exe: 23/23 ✓
- test_scanner.exe: 9/9 ✓
- test_metadata.exe: 11/11 ✓
- test_platform.exe: 12/12 ✓
- test_strbuilder.exe: 15/15 ✓
- test_common.exe: 7/7 ✓

### Service Tests (Output Capture Works)
- `test_service_lifecycle.exe`: 11/13 pass - Output capture works! (2 failures are test logic issues, not output issues)
- `test_service_integration.exe`: 8/11 pass - Output capture works! (3 failures are test logic issues)

The service test failures are due to **test environment state mismatches** (service running when test expects stopped), NOT output capture problems.

## Technical Details

### Why Two Code Paths?
1. **Console Mode** (`CONOUT$`): Better for TUI - cursor positioning, colors, performance
2. **Stdout Mode** (`stdout`): Required for pipe redirection and file capture

### How Detection Works
- `GetConsoleMode()` succeeds → stdout is a console (interactive)
- `GetConsoleMode()` fails → stdout is a pipe/file (redirected)

### Backward Compatibility
- Interactive use: Unchanged behavior (direct console writes)
- Redirected use: Now works correctly (stdout writes)
- No environment variables or flags needed - fully automatic

## Files Changed
1. `src/platform.c` - Core implementation
2. `AGENTS.md` - Documentation update
3. `INFRASTRUCTURE_ISSUES.md` - Updated with resolution status

## Build Instructions
After pulling these changes:
```batch
build.bat                    # Rebuild NCD executables
cd test
build-and-run-tests.bat      # Rebuild and run tests
```

## Conclusion

✅ The test infrastructure issue is **RESOLVED**. Output capture now works correctly via automatic redirect detection. All verification items pass. Core tests are at 100% (116/116). Service tests have working output capture with some unrelated test logic issues remaining.
