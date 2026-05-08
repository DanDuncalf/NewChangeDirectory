# NCD Documentation

Documentation for NewChangeDirectory (NCD) - a cross-platform command-line directory navigation tool.

## Quick Links

- [Main README](../README.md) - User guide and quick start
- [AGENTS.md](../AGENTS.md) - Comprehensive agent/AI documentation
- [AGENT_RULES.md](../AGENT_RULES.md) - Agent workflow rules
- [Agent Mode Reference](agent_mode.md) - Implemented `--agent:*` commands and flags
- [Testing Guide](../AGENT_TESTING_GUIDE.md) - Complete testing workflow and isolation rules

## Documentation Structure

### Root Docs

- **[agent_mode.md](agent_mode.md)** - Implemented agent mode behavior and flags
- **[AGENT_TESTING_GUIDE.md](../AGENT_TESTING_GUIDE.md)** - Complete testing workflow and isolation rules

### `/architecture`
Current architecture documentation:

- **[OVERVIEW.md](architecture/OVERVIEW.md)** - System architecture overview
- **[shared_memory_pointers_explained.md](architecture/shared_memory_pointers_explained.md)** - Offset vs pointer model

### `/history`
Historical notes and post-mortems:

- **[Lessons Learned](history/lessons_learned.md)** - Development insights

## For Developers

### Building

**Windows:**
```batch
build.bat
```

**Linux:**
```bash
./build.sh
```

### Testing

Prefer the Python harness:

```powershell
python test\generate_report.py
python test\runner.py unit
python test\runner.py integration
```

See [test/README.md](../test/README.md) and [AGENT_TESTING_GUIDE.md](../AGENT_TESTING_GUIDE.md) for the full workflow.

### Key Source Files

| File | Purpose |
|------|---------|
| `src/ncd.h` | Core types, constants, platform detection |
| `src/main.c` | Entry point, CLI parsing, orchestration |
| `src/database.c` | Binary DB load/save, groups, config |
| `src/scanner.c` | Multi-threaded directory scanning |
| `src/matcher.c` | Search matching algorithm |
| `src/ui.c` | Interactive TUI implementation |

## Contributing

When contributing to NCD:

1. Follow the [Agent Rules](../AGENT_RULES.md) for workflow
2. Run the test suite before submitting changes
3. Update relevant documentation if you change behavior
4. Keep changes minimal and focused

## License

MIT License - See project root for details.
