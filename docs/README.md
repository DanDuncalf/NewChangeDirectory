# NCD Documentation

Documentation for NewChangeDirectory (NCD) - a cross-platform command-line directory navigation tool.

## Quick Links

- [Main README](../README.md) - User guide and quick start
- [AGENTS.md](../AGENTS.md) - Comprehensive agent/AI documentation
- [AGENT_RULES.md](../AGENT_RULES.md) - Agent workflow rules

## Documentation Structure

### `/architecture`
Technical architecture and design documentation:

- **[Code Quality Fixes](architecture/code_quality.md)** - Completed code quality improvements
- **[Linux Port](architecture/linux_port.md)** - Historical Linux porting documentation
- **[Test Strategy](architecture/test_strategy.md)** - Comprehensive test plan (218 test cases)

### `/history`
Project history and post-mortems:

- **[Implementation Summary](history/implementation_summary.md)** - Major feature implementation recap
- **[Final Summary](history/final_summary.md)** - Project completion summary
- **[Lessons Learned](history/lessons_learned.md)** - Development insights
- **[Performance Baseline](history/baseline.md)** - Performance benchmarks

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

See [test/README.md](../test/README.md) for comprehensive testing instructions.

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
