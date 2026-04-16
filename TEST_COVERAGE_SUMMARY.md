# NCD Test Coverage Summary

**Date:** 2026-04-12  
**Current Status:** ~85% coverage with 325+ unit tests  
**Planned Status:** 95%+ coverage with 500+ unit tests

---

## Executive Summary

| Metric | Current | After Plan | Improvement |
|--------|---------|------------|-------------|
| **Total Unit Tests** | 325+ | 500+ | +175 (+54%) |
| **Code Coverage** | ~85% | 95%+ | +10% |
| **Test Files** | 59 | 75 | +16 |
| **Lines of Test Code** | ~25,000 | ~40,000 | +15,000 (+60%) |
| **Edge Case Coverage** | ~60% | 90%+ | +30% |
| **Stress Tests** | 5 | 30 | +25 |
| **Fuzz Targets** | 2 | 6 | +4 |

---

## Coverage by Module

### 1. Core Modules

| Module | Lines | Current Tests | Current % | New Tests | Planned % | Gap Filled |
|--------|-------|---------------|-----------|-----------|-----------|------------|
| **main.c** | 4,633 | ~40 | 60% | +40 | 95% | Signal handling, result escaping, sorting |
| **database.c** | 3,302 | ~60 | 75% | +50 | 95% | Ref counting, atomic saves, migrations |
| **ui.c** | 3,047 | ~38 | 65% | +60 | 92% | Config editor, deep nav, edge cases |
| **scanner.c** | 1,098 | ~9 | 70% | +35 | 92% | Thread pool, timeouts, permissions |
| **matcher.c** | 818 | ~23 | 85% | +30 | 97% | DL edge cases, performance, fuzzing |
| **cli.c** | 767 | ~61 | 90% | +30 | 98% | Complex flags, edge cases |
| **result.c** | 257 | ~12 | 70% | +20 | 95% | Escaping, batch/shell generation |

### 2. Service Modules

| Module | Lines | Current Tests | Current % | New Tests | Planned % | Gap Filled |
|--------|-------|---------------|-----------|-----------|-----------|------------|
| **service_main.c** | 1,955 | ~25 | 75% | +25 | 95% | Startup, shutdown, crash recovery |
| **service_state.c** | 943 | ~20 | 80% | +15 | 95% | State machine, queue management |
| **service_publish.c** | 768 | ~15 | 75% | +20 | 95% | Snapshot generation, SHM publishing |
| **state_backend_service.c** | 786 | ~18 | 75% | +20 | 95% | Fallback, reconnection, errors |
| **state_backend_local.c** | 461 | ~10 | 80% | +10 | 95% | File locking, atomic operations |

### 3. IPC & Shared Memory

| Module | Lines | Current Tests | Current % | New Tests | Planned % | Gap Filled |
|--------|-------|---------------|-----------|-----------|-----------|------------|
| **control_ipc_win.c** | 952 | ~12 | 70% | +15 | 92% | Pipe edge cases, timeouts |
| **control_ipc_posix.c** | 868 | ~12 | 70% | +15 | 92% | Socket edge cases, permissions |
| **shm_platform_win.c** | 419 | ~10 | 75% | +12 | 95% | Large segments, encoding |
| **shm_platform_posix.c** | 343 | ~10 | 75% | +12 | 95% | Cleanup, crashes |
| **shared_state.c** | 183 | ~22 | 80% | +10 | 95% | Checksum, validation |

### 4. Platform & Utilities

| Module | Lines | Current Tests | Current % | New Tests | Planned % | Gap Filled |
|--------|-------|---------------|-----------|-----------|-----------|------------|
| **platform_ncd.c** | 458 | ~46 | 70% | +15 | 90% | Drive enumeration, paths |
| **strbuilder.c** (shared) | ~800 | ~54 | 80% | +10 | 95% | Edge cases, performance |
| **common.c** (shared) | ~400 | ~44 | 80% | +10 | 95% | Allocation edge cases |

---

## Test Categories: Before and After

### Unit Tests by Category

| Category | Current | After Plan | Delta |
|----------|---------|------------|-------|
| **Database Operations** | 60 | 110 | +50 |
| **Search/Matching** | 23 | 53 | +30 |
| **CLI Parsing** | 61 | 91 | +30 |
| **Agent Mode** | 35 | 50 | +15 |
| **UI/Selector** | 38 | 103 | +65 |
| **Scanner** | 9 | 44 | +35 |
| **Service Lifecycle** | 25 | 50 | +25 |
| **Service Database** | 35 | 70 | +35 |
| **IPC Communication** | 25 | 50 | +25 |
| **Shared Memory** | 22 | 42 | +20 |
| **Platform Abstraction** | 46 | 61 | +15 |
| **String Builder** | 54 | 64 | +10 |
| **Memory/Common** | 44 | 54 | +10 |
| **Result Output** | 12 | 32 | +20 |
| **Config/Metadata** | 35 | 50 | +15 |
| **History** | 14 | 24 | +10 |
| **Corruption Handling** | 16 | 26 | +10 |
| **Bug Regression** | 20 | 40 | +20 |
| **Integration** | 31 | 56 | +25 |
| **Edge Cases** | 10 | 50 | +40 |
| **Stress Tests** | 5 | 30 | +25 |
| **Fuzz Tests** | 2 | 12 | +10 |
| **TOTAL** | **325** | **500+** | **+175** |

---

## Coverage Deep Dive

### Currently Well Covered (90%+)

| Area | Coverage | Test Files |
|------|----------|------------|
| Binary database save/load | 95% | test_database.c, test_db_corruption.c |
| Basic CLI flag parsing | 92% | test_cli_parse.c, test_cli_parse_extended.c |
| Group database operations | 90% | test_database.c |
| Config save/load | 90% | test_config.c |
| Basic exclusion patterns | 90% | test_database.c |
| String builder operations | 90% | test_strbuilder.c, test_strbuilder_extended.c |
| Memory allocation wrappers | 90% | test_common.c, test_common_extended.c |

### Moderately Covered (70-89%)

| Area | Current | Planned | Priority |
|------|---------|---------|----------|
| Service IPC operations | 75% | 95% | **Critical** |
| UI selector rendering | 75% | 92% | **High** |
| Scanner thread management | 70% | 92% | **High** |
| Result output escaping | 70% | 95% | **High** |
| Shared memory management | 75% | 95% | **Medium** |
| Fuzzy matching edge cases | 85% | 97% | **Medium** |
| Platform drive enumeration | 70% | 90% | **Medium** |

### Poorly Covered (<70%)

| Area | Current | Planned | Priority |
|------|---------|---------|----------|
| Signal handling | 30% | 90% | **Critical** |
| Database reference counting | 50% | 95% | **Critical** |
| Atomic save failure recovery | 40% | 90% | **Critical** |
| Service crash recovery | 45% | 90% | **Critical** |
| Config editor UI | 55% | 90% | **High** |
| Navigator deep hierarchies | 60% | 90% | **High** |
| IPC timeout handling | 55% | 90% | **High** |
| Complex CLI flag combinations | 65% | 95% | **Medium** |
| Progress callback during scan | 50% | 85% | **Medium** |

---

## New Test Files Breakdown

### Phase 1: Critical Gaps (3 files, 105 tests)

| File | Tests | Coverage Target |
|------|-------|-----------------|
| test_database_extended.c | 50 | Reference counting, atomic saves, migrations |
| test_service_stress.c | 25 | State transitions, crash recovery, load |
| test_odd_cases.c | 30 | Time-related, filesystem oddities |

### Phase 2: High Impact (3 files, 105 tests)

| File | Tests | Coverage Target |
|------|-------|-----------------|
| test_main.c | 40 | Signal handling, result escaping, sorting |
| test_scanner_extended.c | 35 | Thread pool, timeouts, permissions |
| test_ipc_extended.c | 30 | Timeout handling, security, edge cases |

### Phase 3: Medium Impact (4 files, 125 tests)

| File | Tests | Coverage Target |
|------|-------|-----------------|
| test_ui_extended.c | 45 | Config editor, navigator, edge cases |
| test_matcher_extended.c | 30 | DL edge cases, performance, fuzzing |
| test_service_rescan.c | 20 | Service-side rescan scenarios |
| test_cli_edge_cases.c | 30 | Complex flags, combinations |

### Phase 4: Polish & Stress (6 files, 115 tests)

| File | Tests | Coverage Target |
|------|-------|-----------------|
| test_result_edge_cases.c | 20 | Escaping edge cases |
| test_shm_stress.c | 20 | Large segments, cleanup |
| test_ui_exclusions.c | 15 | Exclusion UI interactions |
| test_stress.c | 25 | Performance, memory, stability |
| fuzz_ipc.c | 10 | Message fuzzing |
| fuzz_database_load.c | 10 | Database fuzzing |

---

## Risk Areas Addressed

### Critical Risks (Before → After)

| Risk | Before Mitigation | After Mitigation |
|------|-------------------|------------------|
| **Data corruption on crash** | 40% tested | 90% tested |
| **Service deadlock** | 45% tested | 90% tested |
| **Memory leaks** | 50% tested | 90% tested |
| **Signal handling bugs** | 30% tested | 90% tested |
| **Race conditions** | 40% tested | 85% tested |

### High Risks (Before → After)

| Risk | Before Mitigation | After Mitigation |
|------|-------------------|------------------|
| **UI hangs** | 60% tested | 90% tested |
| **Timeout failures** | 55% tested | 90% tested |
| **Permission handling** | 50% tested | 90% tested |
| **Path escaping bugs** | 60% tested | 95% tested |

---

## Test Execution Time Estimate

| Test Suite | Current Time | After Plan | Increase |
|------------|--------------|------------|----------|
| Unit tests (fast) | ~30 sec | ~60 sec | +30 sec |
| Service tests | ~60 sec | ~90 sec | +30 sec |
| Integration tests | ~45 sec | ~75 sec | +30 sec |
| Stress tests | ~5 min | ~20 min | +15 min |
| Fuzz tests (60s each) | ~2 min | ~6 min | +4 min |
| **Total CI Time** | **~10 min** | **~30 min** | **+20 min** |

---

## Implementation Timeline

### Week 1-2: Foundation
- [ ] test_database_extended.c (50 tests)
- [ ] test_service_stress.c (25 tests)
- **Milestone:** Critical data integrity tests passing

### Week 3-4: Core Stability  
- [ ] test_main.c (40 tests)
- [ ] test_scanner_extended.c (35 tests)
- [ ] test_ipc_extended.c (30 tests)
- **Milestone:** All crash/timeout scenarios tested

### Week 5-6: UI & UX
- [ ] test_ui_extended.c (45 tests)
- [ ] test_matcher_extended.c (30 tests)
- [ ] test_service_rescan.c (20 tests)
- **Milestone:** UI reliability 90%+

### Week 7-8: Edge Cases & Polish
- [ ] test_odd_cases.c (30 tests)
- [ ] test_cli_edge_cases.c (30 tests)
- [ ] test_result_edge_cases.c (20 tests)
- [ ] test_shm_stress.c (20 tests)
- **Milestone:** Edge case coverage 90%+

### Week 9-10: Performance & Fuzzing
- [ ] test_stress.c (25 tests)
- [ ] fuzz_ipc.c (10 tests)
- [ ] fuzz_database_load.c (10 tests)
- [ ] test_ui_exclusions.c (15 tests)
- **Milestone:** Stress tests passing, fuzzing integrated

---

## Success Criteria

### Quantitative Metrics

| Criterion | Target | Measurement |
|-----------|--------|-------------|
| Line coverage | ≥95% | gcov/lcov report |
| Function coverage | ≥98% | gcov/lcov report |
| Branch coverage | ≥85% | gcov/lcov report |
| Test count | ≥500 | Count of TEST() macros |
| Test file count | 75 | Files in test/ directory |

### Qualitative Metrics

| Criterion | Target |
|-----------|--------|
| No critical paths untested | All error handling paths covered |
| All bug fixes have regression tests | test_bugs.c updated |
| All new features have tests | Tests written before/with code |
| CI passes reliably | <1% flaky test rate |
| Tests run in reasonable time | <30 minutes full suite |

---

## Maintenance Recommendations

After achieving 95% coverage:

1. **Coverage Gates:** Require 95% coverage for all new code
2. **Regression Testing:** Every bug fix must include a regression test
3. **Fuzzing Integration:** Run fuzz tests nightly
4. **Performance Baselines:** Track test execution time trends
5. **Coverage Reports:** Generate and review weekly
6. **Test Debt:** Address coverage drops immediately

---

## Appendix: Test Count by Source File

### Current Test Distribution

| Source File | Lines | Tests | Tests/100 Lines |
|-------------|-------|-------|-----------------|
| database.c | 3,302 | 60 | 1.8 |
| matcher.c | 818 | 23 | 2.8 |
| scanner.c | 1,098 | 9 | 0.8 |
| ui.c | 3,047 | 38 | 1.2 |
| main.c | 4,633 | 40 | 0.9 |
| service_main.c | 1,955 | 25 | 1.3 |
| service_state.c | 943 | 20 | 2.1 |
| cli.c | 767 | 61 | 8.0 |

### Planned Test Distribution

| Source File | Lines | Tests | Tests/100 Lines |
|-------------|-------|-------|-----------------|
| database.c | 3,302 | 110 | 3.3 |
| matcher.c | 818 | 53 | 6.5 |
| scanner.c | 1,098 | 44 | 4.0 |
| ui.c | 3,047 | 103 | 3.4 |
| main.c | 4,633 | 80 | 1.7 |
| service_main.c | 1,955 | 50 | 2.6 |
| service_state.c | 943 | 35 | 3.7 |
| cli.c | 767 | 91 | 11.9 |

---

*This summary provides a roadmap for improving NCD's test coverage from ~85% to 95%+, addressing critical gaps in error handling, edge cases, and stress scenarios.*
