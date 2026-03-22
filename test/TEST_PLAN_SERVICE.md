# NCD Shared Memory Service - Comprehensive Test Plan

## Overview

This test plan provides a comprehensive validation framework for the NCD Shared Memory Service implementation. All existing tests are executed in both standalone mode (without service) and service-backed mode (with service), ensuring parity between the two operational modes.

## Test Objectives

1. **Service Lifecycle Management** - Properly start, detect, and stop the service
2. **Standalone Mode Verification** - Ensure NCD works without service (backward compatibility)
3. **Service Mode Verification** - Ensure NCD works with service active
4. **Mode Parity** - Confirm identical behavior between standalone and service modes
5. **State Restoration** - Return system to original state after testing

## Pre-Test Conditions

### Environment Setup
- Windows: Visual Studio build environment or MinGW
- Linux: GCC/Clang with POSIX shared memory support
- Clean build of both `NewChangeDirectory.exe` and `NCDService.exe`
- Test data directory with sample database files
- Administrative/script execution privileges (for service control)

### Test Data Preparation
1. Create test database with known structure:
   - Multiple drives (if applicable)
   - Mix of hidden/system/normal directories
   - Nested directory structures (at least 5 levels deep)
   - Total: ~1000 directories for performance baseline

2. Create metadata with:
   - 5-10 saved groups
   - 20+ heuristics entries
   - 5-10 exclusion patterns
   - 9 directory history entries

## Test Harness Architecture

### Service State Management

```
TEST HARNESS FLOW
=================

1. RECORD INITIAL STATE
   └── Check if service is currently running
   └── Record PID if running
   └── Record generation numbers if accessible

2. EXECUTE STANDALONE TEST SUITE
   └── Ensure service is NOT running
   └── Run all existing tests (database, matcher, etc.)
   └── Record results

3. PREPARE SERVICE MODE
   └── Start NCDService.exe
   └── Wait for initialization (max 5 seconds)
   └── Verify service is responsive (IPC ping)
   └── Publish initial snapshots

4. EXECUTE SERVICE-BACKED TEST SUITE
   └── Run all existing tests
   └── Record results
   └── Compare with standalone results

5. CLEANUP & RESTORATION
   └── Stop service (if started by harness)
   └── Restart service (if was running before tests)
   └── Verify restoration

6. GENERATE REPORT
   └── Summary of all test results
   └── Performance comparison
   └── Parity analysis
```

## Detailed Test Cases

### Phase 1: Initial State Recording (TEST-INIT)

| Test ID | Description | Steps | Expected Result |
|---------|-------------|-------|-----------------|
| TEST-INIT-01 | Record service state | 1. Check service process existence<br>2. Query service statistics if running<br>3. Log initial state | Initial state documented |
| TEST-INIT-02 | Verify test environment | 1. Verify NCD executable exists<br>2. Verify service executable exists<br>3. Verify test data accessible | All components present |
| TEST-INIT-03 | Capture baseline metrics | 1. Record current database size<br>2. Record metadata state<br>3. Record disk space available | Baseline metrics logged |

### Phase 2: Standalone Mode Tests (TEST-STANDALONE-*)

Execute all existing tests WITHOUT service running:

| Test ID | Test Suite | Description | Validation Criteria |
|---------|------------|-------------|---------------------|
| TEST-STANDALONE-01 | Database | Create and free database | No memory leaks, no crashes |
| TEST-STANDALONE-02 | Database | Add drive to database | Drive added correctly |
| TEST-STANDALONE-03 | Database | Add directory entries | Parent-child relationships correct |
| TEST-STANDALONE-04 | Database | Full path reconstruction | Paths reconstructed accurately |
| TEST-STANDALONE-05 | Database | Binary save/load roundtrip | Data integrity maintained |
| TEST-STANDALONE-06 | Database | Corruption detection | Invalid data rejected |
| TEST-STANDALONE-07 | Database | Truncation handling | Graceful failure on truncated files |
| TEST-STANDALONE-08 | Matcher | Single component matching | Correct matches returned |
| TEST-STANDALONE-09 | Matcher | Multi-component matching | Path traversal works correctly |
| TEST-STANDALONE-10 | Matcher | Case insensitivity | Case-insensitive matching works |
| TEST-STANDALONE-11 | Matcher | Hidden filter | Hidden directories properly filtered |
| TEST-STANDALONE-12 | Matcher | Empty/no results | Graceful handling of empty results |
| TEST-STANDALONE-13 | Matcher | Fuzzy matching | Damerau-Levenshtein distance working |
| TEST-STANDALONE-14 | Matcher | Glob patterns | Wildcard matching (* and ?) works |
| TEST-STANDALONE-15 | State Backend | Local backend open/close | Resource management correct |
| TEST-STANDALONE-16 | State Backend | Metadata retrieval | Metadata loaded from disk |
| TEST-STANDALONE-17 | State Backend | Database retrieval | Database loaded from disk |
| TEST-STANDALONE-18 | State Backend | Mutation persistence | Changes saved to disk |
| TEST-STANDALONE-19 | Snapshot | Header validation | Proper rejection of invalid headers |
| TEST-STANDALONE-20 | Snapshot | CRC64 validation | Checksum verification works |
| TEST-STANDALONE-21 | Snapshot | Section lookup | Section finding works correctly |

### Phase 3: Service Lifecycle Tests (TEST-LIFECYCLE-*)

| Test ID | Description | Steps | Expected Result |
|---------|-------------|-------|-----------------|
| TEST-LIFECYCLE-01 | Service start | 1. Ensure service not running<br>2. Start service<br>3. Verify process exists | Service starts successfully |
| TEST-LIFECYCLE-02 | Service initialization | 1. Wait for init<br>2. Query state info<br>3. Verify generations | Generations start at 1 |
| TEST-LIFECYCLE-03 | Service ping | Send IPC ping request | Response received within 100ms |
| TEST-LIFECYCLE-04 | Service status | Query service statistics | Stats returned correctly |
| TEST-LIFECYCLE-05 | Shared memory creation | 1. Start service<br>2. Check SHM objects exist | SHM objects created |
| TEST-LIFECYCLE-06 | Service stop | 1. Stop service<br>2. Verify process terminated | Clean shutdown |
| TEST-LIFECYCLE-07 | Shared memory cleanup | 1. Stop service<br>2. Verify SHM unlinked | Resources released |
| TEST-LIFECYCLE-08 | Rapid restart | 1. Start/stop 5 times<br>2. Check for resource leaks | No leaks, consistent behavior |
| TEST-LIFECYCLE-09 | Multiple instances | Attempt to start second instance | Second start fails gracefully |
| TEST-LIFECYCLE-10 | Signal handling | Send interrupt signal | Graceful shutdown |

### Phase 4: Service Mode Tests (TEST-SERVICE-*)

Execute all existing tests WITH service running:

| Test ID | Test Suite | Description | Validation Criteria |
|---------|------------|-------------|---------------------|
| TEST-SERVICE-01 | Database | Create and free database | No memory leaks, no crashes |
| TEST-SERVICE-02 | Database | Add drive to database | Drive added correctly |
| TEST-SERVICE-03 | Database | Add directory entries | Parent-child relationships correct |
| TEST-SERVICE-04 | Database | Full path reconstruction | Paths reconstructed accurately |
| TEST-SERVICE-05 | Database | Binary operations | Data integrity maintained |
| TEST-SERVICE-06 | Matcher | All matching operations | Identical to standalone |
| TEST-SERVICE-07 | Matcher | Fuzzy matching | Identical to standalone |
| TEST-SERVICE-08 | Matcher | Glob patterns | Identical to standalone |
| TEST-SERVICE-09 | State Backend | Service detection | Service detected automatically |
| TEST-SERVICE-10 | State Backend | Shared memory mapping | Maps successfully |
| TEST-SERVICE-11 | State Backend | Metadata from snapshot | Metadata loaded from SHM |
| TEST-SERVICE-12 | State Backend | Database from snapshot | Database loaded from SHM |
| TEST-SERVICE-13 | State Backend | Generation tracking | Correct generation numbers |
| TEST-SERVICE-14 | Snapshot | Header validation on SHM | Proper validation |
| TEST-SERVICE-15 | Snapshot | Checksum validation | CRC64 verified |
| TEST-SERVICE-16 | IPC | Heuristic submission | Updates received by service |
| TEST-SERVICE-17 | IPC | Metadata submission | Updates received by service |
| TEST-SERVICE-18 | IPC | Rescan request | Rescan queued/processed |
| TEST-SERVICE-19 | IPC | Flush request | Flush performed |
| TEST-SERVICE-20 | IPC | Concurrent requests | Multiple clients handled |

### Phase 5: Mutation and Persistence Tests (TEST-MUTATION-*)

| Test ID | Description | Steps | Expected Result |
|---------|-------------|-------|-----------------|
| TEST-MUTATION-01 | Heuristic update | 1. Submit heuristic<br>2. Verify generation bump<br>3. Verify new snapshot | Generation incremented |
| TEST-MUTATION-02 | Group add | 1. Add group via IPC<br>2. Query metadata<br>3. Verify presence | Group added correctly |
| TEST-MUTATION-03 | Group remove | 1. Remove group<br>2. Query metadata | Group removed |
| TEST-MUTATION-04 | Exclusion add | 1. Add exclusion<br>2. Check persistence | Exclusion persisted |
| TEST-MUTATION-05 | Exclusion remove | 1. Remove exclusion<br>2. Check persistence | Exclusion removed |
| TEST-MUTATION-06 | Config update | 1. Update config<br>2. Verify snapshot update | Config updated |
| TEST-MUTATION-07 | History clear | 1. Clear history<br>2. Verify empty | History cleared |
| TEST-MUTATION-08 | Dir history add | 1. Add history entry<br>2. Verify | Entry added |
| TEST-MUTATION-09 | Lazy flush | 1. Make changes<br>2. Wait for flush interval<br>3. Verify disk | Changes persisted |
| TEST-MUTATION-10 | Forced flush | 1. Request flush<br>2. Verify immediate persistence | Immediate flush |
| TEST-MUTATION-11 | Service restart persistence | 1. Make changes<br>2. Stop service<br>3. Start service<br>4. Verify state | State recovered |

### Phase 6: Performance and Stress Tests (TEST-PERF-*)

| Test ID | Description | Steps | Expected Result |
|---------|-------------|-------|-----------------|
| TEST-PERF-01 | Startup time comparison | Measure startup with/without service | Document difference |
| TEST-PERF-02 | Query latency | 100 queries, measure response time | Service mode faster |
| TEST-PERF-03 | Large database handling | Test with 100K+ directories | No degradation |
| TEST-PERF-04 | Memory usage | Monitor service memory | Stable, no leaks |
| TEST-PERF-05 | Concurrent clients | 10 simultaneous clients | All served correctly |
| TEST-PERF-06 | Rapid mutations | 100 rapid updates | Queue handled correctly |
| TEST-PERF-07 | Snapshot rebuild time | Measure rebuild after mutations | Within acceptable time |
| TEST-PERF-08 | IPC throughput | Measure messages/second | >100 msg/sec |
| TEST-PERF-09 | Shared memory access | Measure map/unmap time | <10ms per operation |
| TEST-PERF-10 | Rescan performance | Full rescan timing | Document duration |

### Phase 7: Error Handling and Edge Cases (TEST-ERROR-*)

| Test ID | Description | Steps | Expected Result |
|---------|-------------|-------|-----------------|
| TEST-ERROR-01 | Corrupted snapshot | Inject corruption, attempt map | Graceful rejection |
| TEST-ERROR-02 | Invalid generation | Use stale generation | Detected and rejected |
| TEST-ERROR-03 | Missing SHM object | Request non-existent SHM | Proper error handling |
| TEST-ERROR-04 | Service crash | Kill service mid-operation | Client fallback to local |
| TEST-ERROR-05 | IPC timeout | Delay service response | Timeout handled |
| TEST-ERROR-06 | Invalid message | Send malformed IPC message | Rejected gracefully |
| TEST-ERROR-07 | Oversized payload | Send >4KB payload | Rejected |
| TEST-ERROR-08 | Permission denied | Run with insufficient privileges | Clear error message |
| TEST-ERROR-09 | Out of memory | Exhaust memory during operation | Graceful degradation |
| TEST-ERROR-10 | Disk full during flush | Simulate full disk | Error reported, state preserved |

### Phase 8: Mode Transition Tests (TEST-TRANSITION-*)

| Test ID | Description | Steps | Expected Result |
|---------|-------------|-------|-----------------|
| TEST-TRANSITION-01 | Standalone to service | 1. Start standalone<br>2. Start service<br>3. Continue operations | Seamless transition |
| TEST-TRANSITION-02 | Service to standalone | 1. Use service<br>2. Stop service<br>3. Continue operations | Fallback to local |
| TEST-TRANSITION-03 | Simultaneous modes | Multiple clients, mixed modes | All clients work |
| TEST-TRANSITION-04 | Service restart during use | 1. Start operation<br>2. Restart service<br>3. Continue | Client reconnects |
| TEST-TRANSITION-05 | Generation synchronization | Updates visible to new clients | Eventual consistency |

### Phase 9: State Restoration (TEST-RESTORE)

| Test ID | Description | Steps | Expected Result |
|---------|-------------|-------|-----------------|
| TEST-RESTORE-01 | Stop test service | Stop service started by tests | Service terminated |
| TEST-RESTORE-02 | Restart original service | If was running, restart | Original service running |
| TEST-RESTORE-03 | Verify restoration | Check service state matches initial | State restored |
| TEST-RESTORE-04 | Cleanup test data | Remove temporary test files | Clean environment |

## Test Execution Matrix

### Test Sequences

```
SEQUENCE A: Full Regression (Recommended for releases)
├── TEST-INIT-* (Record state)
├── TEST-STANDALONE-* (All existing tests)
├── TEST-LIFECYCLE-* (Service control)
├── TEST-SERVICE-* (All tests with service)
├── TEST-MUTATION-* (Persistence)
├── TEST-PERF-* (Performance baseline)
├── TEST-ERROR-* (Error handling)
├── TEST-TRANSITION-* (Mode switching)
└── TEST-RESTORE-* (Cleanup)

SEQUENCE B: Quick Smoke (For CI/CD)
├── TEST-INIT-01 (Record state)
├── TEST-STANDALONE-01,05,08,15 (Quick subset)
├── TEST-LIFECYCLE-01,03,06 (Start/ping/stop)
├── TEST-SERVICE-09,10,16 (Service features)
└── TEST-RESTORE-01 (Cleanup)

SEQUENCE C: Service Only (When standalone already tested)
├── TEST-INIT-* (Record state)
├── TEST-LIFECYCLE-* (Full lifecycle)
├── TEST-SERVICE-* (All service tests)
├── TEST-MUTATION-* (Persistence)
└── TEST-RESTORE-* (Cleanup)

SEQUENCE D: Stress Test (Performance validation)
├── TEST-INIT-01 (Record state)
├── TEST-LIFECYCLE-01 (Start service)
├── TEST-PERF-* (All performance tests)
├── TEST-ERROR-* (Error handling)
└── TEST-RESTORE-* (Cleanup)
```

## Success Criteria

### Pass/Fail Criteria

| Category | Criteria |
|----------|----------|
| **Functional** | All TEST-STANDALONE and TEST-SERVICE tests pass with identical results |
| **Lifecycle** | Service starts/stops cleanly 100% of attempts |
| **Mutations** | All changes persisted correctly across service restarts |
| **Performance** | Service mode shows measurable improvement in query latency |
| **Reliability** | No crashes, memory leaks, or resource leaks detected |
| **Restoration** | Original service state restored correctly 100% of attempts |

### Performance Baselines

| Metric | Standalone | Service | Improvement |
|--------|------------|---------|-------------|
| Cold startup | < 1s | < 100ms | 10x |
| Warm query | 50-200ms | 1-10ms | 10-50x |
| Metadata update | 50-200ms | 5-50ms | 2-10x |
| Memory (per client) | 10-50MB | Shared | Significant |

## Reporting Format

### Test Report Template

```markdown
# NCD Service Test Report

## Execution Summary
- Date/Time: YYYY-MM-DD HH:MM:SS
- Platform: Windows 10/Linux Ubuntu 22.04
- Build: Release/Debug
- Test Sequence: Full Regression

## Service State
- Initial State: [Running/Not Running]
- Initial PID: [PID or N/A]
- Final State: Restored to initial

## Results Summary
| Phase | Tests | Passed | Failed | Skipped |
|-------|-------|--------|--------|---------|
| Standalone | 21 | 21 | 0 | 0 |
| Lifecycle | 10 | 10 | 0 | 0 |
| Service | 20 | 20 | 0 | 0 |
| Mutation | 11 | 11 | 0 | 0 |
| Performance | 10 | 10 | 0 | 0 |
| Error Handling | 10 | 10 | 0 | 0 |
| Transition | 5 | 5 | 0 | 0 |
| **TOTAL** | **87** | **87** | **0** | **0** |

## Parity Analysis
- Identical Results: 40/40 tests
- Minor Differences: 0
- Significant Differences: 0

## Performance Results
| Metric | Standalone | Service | Improvement |
|--------|------------|---------|-------------|
| Startup | 850ms | 45ms | 18.9x |
| Query | 125ms | 3ms | 41.7x |

## Issues Found
- None

## Recommendations
- Ready for release
```

## Automation Considerations

### Automated Test Script Requirements

1. **Service Detection**
   ```bash
   # Windows
   wmic process where "name='NCDService.exe'" get ProcessId
   
   # Linux
   pgrep -x NCDService || echo "Not running"
   ```

2. **Service Control**
   ```bash
   # Windows
   start /B NCDService.exe
   taskkill /F /IM NCDService.exe
   
   # Linux
   ./NCDService &
   kill $PID
   ```

3. **Health Check**
   ```bash
   # IPC ping to verify responsiveness
   ncd /agent check --service-status
   ```

4. **Result Comparison**
   - Capture output from standalone mode
   - Capture output from service mode
   - Diff the results (should be identical)

### Continuous Integration Integration

```yaml
# Example CI configuration
stages:
  - build
  - test_standalone
  - test_service
  - test_parity
  - performance

test_standalone:
  script:
    - ./test_harness.sh --mode=standalone --sequence=smoke
  
test_service:
  script:
    - ./test_harness.sh --mode=service --sequence=smoke

test_parity:
  script:
    - ./test_harness.sh --mode=both --sequence=full
    
performance:
  script:
    - ./test_harness.sh --mode=both --sequence=performance
  artifacts:
    reports:
      performance: performance_report.json
```

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Service fails to start | High | Fallback to standalone, log error |
| Resource leak in service | High | Periodic restart, memory monitoring |
| IPC failures | Medium | Retry logic, timeout handling |
| Test data corruption | Medium | Use isolated test databases |
| Original service not restored | Medium | Verify restoration, manual fallback |
| Performance regression | High | Baseline comparison, alert thresholds |

## Sign-Off Criteria

This test plan is complete when:
- [ ] All 87 test cases defined
- [ ] Test harness implements service lifecycle management
- [ ] All existing tests wrapped for dual-mode execution
- [ ] State restoration verified
- [ ] Performance baselines established
- [ ] CI/CD integration documented
- [ ] Test report template approved