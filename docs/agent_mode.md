# NCD Agent Mode Reference

This document reflects the agent-mode behavior implemented in `src/cli.c` and `src/main.c`.

## Commands

| Command | Purpose |
|---------|---------|
| `--agent:query <search>` | Search the indexed database |
| `--agent:ls <path>` | List live filesystem contents |
| `--agent:tree <path>` | Show directory structure from the database |
| `--agent:check <path>` | Check path existence, DB age, stats, or service status |
| `--agent:complete <partial>` | Return completion candidates |
| `--agent:mkdir <path>` | Create one directory |
| `--agent:mkdirs <content>` | Create a directory tree from flat text, JSON, `--file`, or stdin |
| `--agent:rmdir <path>` | Remove one directory |
| `--agent:rmdirs <path>` | Remove a directory tree |
| `--agent:mv <src> <dst>` | Move or rename a directory |
| `--agent:ln <target> <link>` | Create a symbolic link |
| `--agent:verify <path>` | Verify directory properties |
| `--agent:chmod <path>` | Change directory permissions |
| `--agent:quit` | Request graceful service shutdown |
| `--agent:help` | Show built-in help |

## Mutation Flags

### `mkdir`

- `--json`
- `--dry-run`
- `--parents-required`
- `--force`
- `--mode <octal>`
- `--verify`

### `mkdirs`

- `--file <path>`
- `--json`
- `--dry-run`
- `--atomic`
- `--force`
- `--verify`
- `--stop-on-error`
- `--parents-required`
- `--mode <octal>`

If neither `--file` nor a positional tree spec is provided, `mkdirs` reads from stdin.

### `rmdir` and `rmdirs`

- `rmdir` supports `--force` and `--json`.
- `rmdirs` supports `--dry-run`, `--force`, `--preserve-root`, and `--json`.
- `rmdirs` refuses to delete a root directory and requires `--force` unless you are previewing with `--dry-run`.

### `mv`

- `--force`
- `--json`

`--force` only overwrites an empty destination directory.

### `ln`

- `--json`

On Windows, symbolic links require developer mode or sufficient privilege; unsupported cases return `error_unsupported`.

### `verify`

- `--empty`
- `--mode <octal>`
- `--tree <spec>`
- `--json`

### `chmod`

- `--mode <octal>`
- `--recursive`
- `--json`

On Windows, `chmod` is implemented as `error_unsupported`.

## Output Notes

- `tree --json` emits compact entries using `"n"` for name and `"d"` for depth.
- `ls --json` emits `"name"` and `"is_dir"`.
- `mkdirs --json` buffers its output so atomic failures still produce one complete JSON document.

## Windows Notes

- Agent path handling normalizes `T:\.` to `T:\` for batch-file compatibility.
- The Windows integration tests cover the quoting workaround in `test/Win/test_agent_commands.bat` and `test/Win/test_features.bat`.

## Test Coverage

Implemented agent filesystem operations are covered by:

- `test/test_agent_mkdir_extended.c`
- `test/test_agent_rmdir.c`
- `test/test_agent_mv.c`
- `test/test_agent_verify.c`
