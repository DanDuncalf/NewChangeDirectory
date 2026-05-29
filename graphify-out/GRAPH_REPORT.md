# Graph Report - E:/llama/NewChangeDirectory  (2026-05-29)

## Corpus Check
- 181 files · ~272,840 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 2826 nodes · 8781 edges · 89 communities detected
- Extraction: 54% EXTRACTED · 46% INFERRED · 0% AMBIGUOUS · INFERRED: 4031 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_IPC Protocol & Service Control|IPC Protocol & Service Control]]
- [[_COMMUNITY_Database Core & Search Engine|Database Core & Search Engine]]
- [[_COMMUNITY_Core Source Modules (.c)|Core Source Modules (.c)]]
- [[_COMMUNITY_Packaging & Deployment Scripts|Packaging & Deployment Scripts]]
- [[_COMMUNITY_IPC Message Transport Layer|IPC Message Transport Layer]]
- [[_COMMUNITY_Agent Mode Filesystem Ops|Agent Mode Filesystem Ops]]
- [[_COMMUNITY_JSON Parsing & Serialization|JSON Parsing & Serialization]]
- [[_COMMUNITY_Test Runner & Build Infra|Test Runner & Build Infra]]
- [[_COMMUNITY_Service Source Modules (.c)|Service Source Modules (.c)]]
- [[_COMMUNITY_Test Source Modules (.c)|Test Source Modules (.c)]]
- [[_COMMUNITY_CLI Argument Parsing|CLI Argument Parsing]]
- [[_COMMUNITY_Platform Path & File Utils|Platform Path & File Utils]]
- [[_COMMUNITY_Agent Mode Integration Tests|Agent Mode Integration Tests]]
- [[_COMMUNITY_Agent Mode CLI Config|Agent Mode CLI Config]]
- [[_COMMUNITY_Python Test Harness|Python Test Harness]]
- [[_COMMUNITY_Platform Windows-Specific|Platform Windows-Specific]]
- [[_COMMUNITY_Matcher Fuzzy & Glob|Matcher Fuzzy & Glob]]
- [[_COMMUNITY_String Builder Utilities|String Builder Utilities]]
- [[_COMMUNITY_MCP Server Integration|MCP Server Integration]]
- [[_COMMUNITY_Chained Matching Algorithm|Chained Matching Algorithm]]
- [[_COMMUNITY_TUI Terminal UI|TUI Terminal UI]]
- [[_COMMUNITY_Service Executable Detection|Service Executable Detection]]
- [[_COMMUNITY_Memory & GC Management|Memory & GC Management]]
- [[_COMMUNITY_Service Metadata & Config|Service Metadata & Config]]
- [[_COMMUNITY_PowerShell Test Environment|PowerShell Test Environment]]
- [[_COMMUNITY_File Existence & Validation|File Existence & Validation]]
- [[_COMMUNITY_Path Sanitization & Normalization|Path Sanitization & Normalization]]
- [[_COMMUNITY_Binary DB Format & CRC|Binary DB Format & CRC]]
- [[_COMMUNITY_Test Environment Variables|Test Environment Variables]]
- [[_COMMUNITY_Agent Mode Output Formatting|Agent Mode Output Formatting]]
- [[_COMMUNITY_Agent E2E & Cleanup Tests|Agent E2E & Cleanup Tests]]
- [[_COMMUNITY_Agent Command Dispatch|Agent Command Dispatch]]
- [[_COMMUNITY_Service Process Management|Service Process Management]]
- [[_COMMUNITY_Scanner Thread Pool|Scanner Thread Pool]]
- [[_COMMUNITY_NCD Service Test Suite|NCD Service Test Suite]]
- [[_COMMUNITY_Agent Error Codes & Paths|Agent Error Codes & Paths]]
- [[_COMMUNITY_Database Test Utilities|Database Test Utilities]]
- [[_COMMUNITY_Agent Misc Test Cases|Agent Misc Test Cases]]
- [[_COMMUNITY_Buffer & Memory Sizes|Buffer & Memory Sizes]]
- [[_COMMUNITY_Test Temp Path Detection|Test Temp Path Detection]]
- [[_COMMUNITY_Quality & Graph Test IDs|Quality & Graph Test IDs]]
- [[_COMMUNITY_Test Health Metrics|Test Health Metrics]]
- [[_COMMUNITY_Build Environment Setup|Build Environment Setup]]
- [[_COMMUNITY_Python Build Automation|Python Build Automation]]
- [[_COMMUNITY_PowerShell Test Commands|PowerShell Test Commands]]
- [[_COMMUNITY_Binary Layout Verification|Binary Layout Verification]]
- [[_COMMUNITY_PowerShell Test Output|PowerShell Test Output]]
- [[_COMMUNITY_Agent & Quality Test IDs|Agent & Quality Test IDs]]
- [[_COMMUNITY_Agent & Quality Test IDs 2|Agent & Quality Test IDs 2]]
- [[_COMMUNITY_Agent Misc Test IDs|Agent Misc Test IDs]]
- [[_COMMUNITY_Agent Test Case Group A|Agent Test Case Group A]]
- [[_COMMUNITY_Agent Test Case Group B|Agent Test Case Group B]]
- [[_COMMUNITY_IPC CLI Entry Point|IPC CLI Entry Point]]
- [[_COMMUNITY_Header Size Definitions|Header Size Definitions]]
- [[_COMMUNITY_Doc & Report Test IDs|Doc & Report Test IDs]]
- [[_COMMUNITY_Agent & MCP Test IDs|Agent & MCP Test IDs]]
- [[_COMMUNITY_PowerShell NCD Module|PowerShell NCD Module]]
- [[_COMMUNITY_PowerShell Completion|PowerShell Completion]]
- [[_COMMUNITY_PowerShell Test Util|PowerShell Test Util]]
- [[_COMMUNITY_Python Discovery Module|Python Discovery Module]]
- [[_COMMUNITY_PowerShell Integration|PowerShell Integration]]
- [[_COMMUNITY_Shared State Header|Shared State Header]]
- [[_COMMUNITY_Platform Header|Platform Header]]
- [[_COMMUNITY_Database Header|Database Header]]
- [[_COMMUNITY_Matcher Header|Matcher Header]]
- [[_COMMUNITY_Scanner Header|Scanner Header]]
- [[_COMMUNITY_UI Header|UI Header]]
- [[_COMMUNITY_Service State Header|Service State Header]]
- [[_COMMUNITY_State Backend Header|State Backend Header]]
- [[_COMMUNITY_SHM Platform Header|SHM Platform Header]]
- [[_COMMUNITY_Control IPC Header|Control IPC Header]]
- [[_COMMUNITY_CLI Header|CLI Header]]
- [[_COMMUNITY_Service Publish Header|Service Publish Header]]
- [[_COMMUNITY_NCD Core Header|NCD Core Header]]
- [[_COMMUNITY_Agent Test Header|Agent Test Header]]
- [[_COMMUNITY_Test Framework Header|Test Framework Header]]
- [[_COMMUNITY_Fuzz Database Header|Fuzz Database Header]]
- [[_COMMUNITY_Python Env Module|Python Env Module]]
- [[_COMMUNITY_IPC Test Common Header|IPC Test Common Header]]
- [[_COMMUNITY_Service Test Common Header|Service Test Common Header]]
- [[_COMMUNITY_Test Startup C|Test Startup C]]
- [[_COMMUNITY_Test Legacy Header|Test Legacy Header]]
- [[_COMMUNITY_Bench Matcher Header|Bench Matcher Header]]
- [[_COMMUNITY_Posix Exit Header|Posix Exit Header]]
- [[_COMMUNITY_Doc MCP Server Quality|Doc: MCP Server Quality]]
- [[_COMMUNITY_Doc Agent Rules Quality|Doc: Agent Rules Quality]]
- [[_COMMUNITY_Doc Coverage Report|Doc: Coverage Report]]
- [[_COMMUNITY_Doc Best Practices Testing|Doc: Best Practices Testing]]
- [[_COMMUNITY_Doc PowerShell Runner|Doc: PowerShell Runner]]

## God Nodes (most connected - your core abstractions)
1. `db_free()` - 255 edges
2. `db_create()` - 171 edges
3. `ipc_client_disconnect()` - 137 edges
4. `ipc_client_connect()` - 131 edges
5. `db_add_dir()` - 118 edges
6. `ipc_client_init()` - 114 edges
7. `ipc_client_cleanup()` - 112 edges
8. `db_add_drive()` - 111 edges
9. `db_metadata_free()` - 107 edges
10. `ui_inject_keys()` - 104 edges

## Surprising Connections (you probably didn't know these)
- `ipc_error_strings()` --calls--> `ipc_error_string()`  [INFERRED]
  E:\llama\NewChangeDirectory\test\test_service_lazy_load.c → E:\llama\NewChangeDirectory\src\control_ipc_common.c
- `db_ext_retain_null_does_not_crash()` --calls--> `db_retain()`  [INFERRED]
  E:\llama\NewChangeDirectory\test\test_database_extended.c → E:\llama\NewChangeDirectory\src\database.c
- `binary_load_corrupted_rejected()` --calls--> `db_free()`  [INFERRED]
  E:\llama\NewChangeDirectory\test\test_database.c → E:\llama\NewChangeDirectory\src\database.c
- `binary_load_truncated_rejected()` --calls--> `db_free()`  [INFERRED]
  E:\llama\NewChangeDirectory\test\test_database.c → E:\llama\NewChangeDirectory\src\database.c
- `config_rescan_interval_default()` --calls--> `db_config_init_defaults()`  [INFERRED]
  E:\llama\NewChangeDirectory\test\test_agent_mkdir.c → E:\llama\NewChangeDirectory\src\database.c

## Hyperedges (group relationships)
- **Offset-Based Design for Cross-Process Shared Memory** — a19, a20, a01, a26, a27 [EXTRACTED 1.00]
- **Test Isolation: VHD/Ramdisk + LOCALAPPDATA Redirect + NCD_TEST_MODE** — t02, t03, t04, t01, t18 [EXTRACTED 1.00]
- **MCP Server Wraps NCD Agent Mode as 14 FastMCP Tools** — mcp01, a04, mcp02, mcp03 [EXTRACTED 1.00]
- **P0 Code Quality Items Targeting Production Safety** — q02, q03, q04, q05, q06, q07, q08, q09 [EXTRACTED 1.00]
- **4-Agent Parallel Test Expansion (430+ Tests Total)** — t13, t14, t15, t16, t12 [EXTRACTED 1.00]
- **Agent Workflow Rules for NCD Development** — r01, r02, r03, r04, r05, r06 [EXTRACTED 1.00]
- **NCD Service Architecture (IPC + SHM + State Machine)** — m11, m10, m08, m09, a32, a09, a12, a13 [EXTRACTED 1.00]
- **Graphify Knowledge Graph Core Communities** — g04, g05, g06, g07, g08, g09, g10, g11, g12, g01 [EXTRACTED 1.00]
- **Heap Corruption Investigation for 0xC0000374 Crash** — x01, x02, x03, a10, m02, m03 [EXTRACTED 1.00]
- **NCD Core Module Architecture** — m01, m02, m03, m04, m05, m06, m07, m08, m09, m10, m11 [EXTRACTED 1.00]

## Communities

### Community 0 - "IPC Protocol & Service Control"
Cohesion: 0.03
Nodes (326): ipc_client_check_version(), ipc_client_get_detailed_status(), ipc_client_get_state_info(), ipc_client_get_version(), ipc_client_ping(), ipc_client_request_flush(), ipc_client_request_rescan(), ipc_client_request_shutdown() (+318 more)

### Community 1 - "Database Core & Search Engine"
Cohesion: 0.02
Nodes (300): db_remove_path(), agent_build_test_drive_root(), agent_create_test_db(), benchmark_search(), generate_synthetic_db(), main(), db_add_dir(), db_add_drive() (+292 more)

### Community 2 - "Core Source Modules (.c)"
Cohesion: 0.03
Nodes (186): inject_case_insensitive(), inject_clear_keys(), inject_config_sequence(), inject_empty(), inject_from_bad_file(), inject_from_file(), inject_mixed_sequence(), inject_navigation_keys() (+178 more)

### Community 3 - "Packaging & Deployment Scripts"
Cohesion: 0.03
Nodes (187): db_atomic_file_install(), db_check_all_versions(), db_config_exists(), db_config_init_defaults(), db_config_load(), db_config_path(), db_config_save(), db_default_path() (+179 more)

### Community 4 - "IPC Message Transport Layer"
Cohesion: 0.04
Nodes (147): ipc_free_message(), ipc_server_send_error(), ipc_server_send_response(), ipc_server_send_version_mismatch(), ipc_connection_get_fd(), ipc_platform_conn_write(), ipc_server_accept(), ipc_server_cleanup() (+139 more)

### Community 5 - "Agent Mode Filesystem Ops"
Cohesion: 0.04
Nodes (131): agent_mode_mkdir(), agent_mode_mkdirs(), agent_txn_add(), agent_txn_free(), agent_txn_init(), agent_txn_rollback(), ajb_append(), ajb_append_json_escape() (+123 more)

### Community 6 - "JSON Parsing & Serialization"
Cohesion: 0.03
Nodes (101): apply_long_val(), apply_short_val(), find_value_sep(), is_bundle_bool(), parse_args(), parse_drive_list_token(), parse_skip_drive_list(), tokenize_args() (+93 more)

### Community 7 - "Test Runner & Build Infra"
Cohesion: 0.03
Nodes (108): binary_needs_rebuild(), build_all(), build_linux_main(), build_linux_tests(), build_windows_main(), build_windows_tests(), ensure_ncd_service_for_tests(), get_mtime() (+100 more)

### Community 8 - "Service Source Modules (.c)"
Cohesion: 0.07
Nodes (85): handle_pending_metadata(), build_database_snapshot(), build_metadata_snapshot(), compute_database_snapshot_size(), compute_metadata_snapshot_size(), compute_string_pool_size(), copy_ansi_to_shm(), derive_mount_point() (+77 more)

### Community 9 - "Test Source Modules (.c)"
Cohesion: 0.05
Nodes (69): close_state_backend(), ensure_state_initialized(), get_state_database(), get_state_metadata(), history_delete_service_cb(), crc64_init(), shm_compute_checksum(), shm_crc64() (+61 more)

### Community 10 - "CLI Argument Parsing"
Cohesion: 0.06
Nodes (69): parse_agent_args(), glob_match(), agent_parse_case_insensitive_subcommand(), agent_parse_check_db_age(), agent_parse_check_json_and_flags(), agent_parse_check_path_and_flags(), agent_parse_check_path_only(), agent_parse_check_service_status() (+61 more)

### Community 11 - "Platform Path & File Utils"
Cohesion: 0.1
Nodes (52): agent_build_db_path(), agent_dir_exists(), agent_find_exe(), agent_get_temp_dir(), agent_make_fs_tree(), agent_rm_rf(), agent_run(), agent_test_drive_letter() (+44 more)

### Community 12 - "Agent Mode Integration Tests"
Cohesion: 0.04
Nodes (52): Agent Mode API (LLM Integration), Binary Database Format (NCDB/NCMD), Atomic Write Pattern (temp-then-rename), Per-Drive/Per-Mount Database Files, Consolidated Metadata, Multi-Threaded Directory Scanner, Wrapper Script Pattern, Heuristics-Based Search Result Ranking (+44 more)

### Community 13 - "Agent Mode CLI Config"
Cohesion: 0.11
Nodes (49): agent_check_all_flags(), agent_complete_with_limit(), agent_ls_with_dirs_only(), agent_ls_with_files_only(), agent_mkdir_with_path(), agent_mkdirs_with_file(), agent_mode_without_subcommand_rejected(), agent_tree_with_flat_and_depth() (+41 more)

### Community 14 - "Python Test Harness"
Cohesion: 0.11
Nodes (47): parse_args(), log(), main(), Run *cmd* with real-time output and stall detection.      Args:         cmd:, run_with_monitor(), a_flag_sets_both_hidden_and_system(), agent_ls_depth_zero_rejected(), agent_ls_pattern_and_default_depth() (+39 more)

### Community 15 - "Platform Windows-Specific"
Cohesion: 0.04
Nodes (4): progress_callback_overflow_protection(), progress_callback_updates_console(), progress_callback_with_null_database(), test_progress_callback()

### Community 16 - "Matcher Fuzzy & Glob"
Cohesion: 0.11
Nodes (42): heur_promote_match(), heur_sanitize(), result_cancel(), result_error(), result_ok(), write_result(), cleanup_result_file(), get_result_file_path() (+34 more)

### Community 17 - "String Builder Utilities"
Cohesion: 0.05
Nodes (0): 

### Community 18 - "MCP Server Integration"
Cohesion: 0.08
Nodes (33): _find_ncd(), _get_ncd_binary(), ncd_check(), ncd_chmod(), ncd_complete(), ncd_help(), ncd_ln(), ncd_ls() (+25 more)

### Community 19 - "Chained Matching Algorithm"
Cohesion: 0.06
Nodes (0): 

### Community 20 - "TUI Terminal UI"
Cohesion: 0.17
Nodes (32): cli_agent_ls_missing_path(), cli_agent_missing_subcommand(), cli_agent_query_missing_search_term(), cli_agent_unknown_subcommand(), cli_argument_empty_string(), cli_argument_unicode_all_planes(), cli_argument_very_long_4096_chars(), cli_argument_with_embedded_whitespace() (+24 more)

### Community 21 - "Service Executable Detection"
Cohesion: 0.25
Nodes (23): dir_exists(), find_exe(), make_input_file(), mkdir_dry_run_does_not_create(), mkdir_force_fails_on_non_empty(), mkdir_force_recreate_empty(), mkdir_mode_0700(), mkdir_verify_existing_passes() (+15 more)

### Community 22 - "Memory & GC Management"
Cohesion: 0.33
Nodes (20): append_garbage(), corrupt_checksum_detected(), corrupt_checksum_with_test_nc(), corrupt_dir_count_overflow(), corrupt_drive_count_large(), corrupt_drive_count_overflow(), corrupt_drive_count_zero(), corrupt_magic_number() (+12 more)

### Community 23 - "Service Metadata & Config"
Cohesion: 0.3
Nodes (17): backup_metadata(), ensure_service_running_with_args(), ensure_service_stopped(), find_live_ncd_service_process(), force_terminate_service(), get_service_executable_path(), init_db_creates_metadata_when_missing(), init_db_help_shows_option() (+9 more)

### Community 24 - "PowerShell Test Environment"
Cohesion: 0.26
Nodes (16): Build-Project(), Clear-TuiEnvironmentVariables(), Invoke-Test(), Remove-TestTempDirs(), Remove-TestVhds(), Repair-Environment(), Restore-EnvironmentState(), Run-ParallelExpansionTests() (+8 more)

### Community 25 - "File Existence & Validation"
Cohesion: 0.33
Nodes (15): create_file(), dir_exists(), file_exists(), find_exe(), get_temp_dir(), mkdir_p(), rm_rf(), rmdir_fails_on_non_empty() (+7 more)

### Community 26 - "Path Sanitization & Normalization"
Cohesion: 0.12
Nodes (0): 

### Community 27 - "Binary DB Format & CRC"
Cohesion: 0.38
Nodes (14): add_drive_header(), create_valid_db_header(), fuzz_db_checksum_collision(), fuzz_db_directory_entry_corruption(), fuzz_db_drive_header_corruption(), fuzz_db_header_random_corruption(), fuzz_db_mismatched_drive_counts(), fuzz_db_name_pool_corruption() (+6 more)

### Community 28 - "Test Environment Variables"
Cohesion: 0.38
Nodes (14): cleanup_test_env(), run_ncd_with_keys(), setup_test_env(), tui_config_adjust_timeout(), tui_config_cancel(), tui_config_end_key(), tui_config_home_key(), tui_config_multiple_toggles() (+6 more)

### Community 29 - "Agent Mode Output Formatting"
Cohesion: 0.44
Nodes (12): chmod_changes_mode(), chmod_recursive_changes_all(), chmod_returns_unsupported_on_windows(), find_exe(), get_temp_dir(), normalize_exit_code(), rm_rf(), verify_empty_directory_passes() (+4 more)

### Community 30 - "Agent E2E & Cleanup Tests"
Cohesion: 0.24
Nodes (13): db_logs_path(), db_ext_logs_path_creation_fails(), database_directory_created_automatically(), dir_exists(), file_exists(), get_test_log_path(), get_test_logs_dir(), log_file_format_validation() (+5 more)

### Community 31 - "Agent Command Dispatch"
Cohesion: 0.38
Nodes (12): dir_exists(), get_temp_dir(), mkdirs_flat_format_empty_lines(), mkdirs_flat_format_nested(), mkdirs_flat_format_simple(), mkdirs_json_format_nested(), mkdirs_json_format_simple(), mkdirs_json_format_string_array() (+4 more)

### Community 32 - "Service Process Management"
Cohesion: 0.29
Nodes (12): ensure_service_stopped(), find_live_ncd_service_process(), force_terminate_service(), is_live_ncd_service_process(), run_command_raw(), run_service_command(), run_service_command_ex(), service_executable_exists() (+4 more)

### Community 33 - "Scanner Thread Pool"
Cohesion: 0.18
Nodes (4): contract_modern_skip_uses_skip_test(), contract_run_test_skip_prints_skipped(), contract_skip_test_produces_structured_marker(), _return_skip()

### Community 34 - "NCD Service Test Suite"
Cohesion: 0.27
Nodes (10): Run-TestEnvironment(), Show-Summary(), Start-NcdServiceWindows(), Start-NcdServiceWsl(), Stop-NcdServiceWindows(), Stop-NcdServiceWsl(), Test-ServiceStatusWindows(), Test-ServiceStatusWsl() (+2 more)

### Community 35 - "Agent Error Codes & Paths"
Cohesion: 0.22
Nodes (9): agent_mkdir_creates_directory(), agent_mkdir_handles_existing_directory(), cleanup_test_dir(), config_rescan_interval_bounds(), config_rescan_interval_default(), config_rescan_interval_never(), dir_exists(), main() (+1 more)

### Community 36 - "Database Test Utilities"
Cohesion: 0.42
Nodes (9): create_test_db(), find_exe(), get_temp_dir(), ln_creates_symlink(), mv_force_overwrite_empty(), mv_renames_directory(), mv_updates_database(), rm_rf() (+1 more)

### Community 37 - "Agent Misc Test Cases"
Cohesion: 0.2
Nodes (11): State Backend Abstraction (local vs service), Service-to-Standalone Fallback Chain, Service-Side Rescan with Non-Blocking Old Snapshot, Service Logging System (-log0 through -log5), Graceful Service Shutdown with Timeout, Service -init: Blocking DB Scan on Startup, Service State Machine, Community 9: State Backend (42 nodes) (+3 more)

### Community 38 - "Buffer & Memory Sizes"
Cohesion: 0.22
Nodes (0): 

### Community 39 - "Test Temp Path Detection"
Cohesion: 0.57
Nodes (6): Get-ExpectedLocalAppData(), Repair-Environment(), Test-Environment(), Test-IsTestTempPath(), Write-Finding(), Write-Section()

### Community 40 - "Quality & Graph Test IDs"
Cohesion: 0.29
Nodes (7): Service Version Compatibility Protocol, IPC Diagnostic Tools, Community 1: IPC Client Protocol (258 nodes), Community 6: IPC Server Protocol (112 nodes), Control IPC Module (named pipes / Unix sockets), P0.1: Validate IPC Payloads Before Dispatch, P0.6: Fix Windows IPC Broken-Pipe Handling

### Community 41 - "Test Health Metrics"
Cohesion: 0.33
Nodes (6): 95% Coverage Target (880+ tests), Parallel Test Expansion (430+ tests), Agent 1: Data Integrity (90 tests), Agent 2: Service Infrastructure (100 tests), Agent 3: UI & Main Flow (100 tests), Agent 4: Input Processing (140 tests)

### Community 42 - "Build Environment Setup"
Cohesion: 0.4
Nodes (0): 

### Community 43 - "Python Build Automation"
Cohesion: 0.4
Nodes (1): Windows-specific integration tests.

### Community 44 - "PowerShell Test Commands"
Cohesion: 0.5
Nodes (2): Test-Condition(), Write-TestLog()

### Community 45 - "Binary Layout Verification"
Cohesion: 0.67
Nodes (2): shm_database_compute_layout(), shm_database_layout_free()

### Community 46 - "PowerShell Test Output"
Cohesion: 0.5
Nodes (0): 

### Community 47 - "Agent & Quality Test IDs"
Cohesion: 0.5
Nodes (4): Two UI Modes (Selector + Navigator), Community 2: TUI Key Injection Tests (181 nodes), UI Module (TUI selector + navigator + history), NCD_UI_KEYS Keystroke Injection

### Community 48 - "Agent & Quality Test IDs 2"
Cohesion: 0.5
Nodes (4): Two Snapshots Architecture, Community 5: SHM Snapshots (104 nodes), Shared State Module (SHM snapshot format), P0.7: Close SHM Publication Name Gaps

### Community 49 - "Agent Misc Test IDs"
Cohesion: 0.5
Nodes (4): Windows SHM: CreateFileMapping, Linux SHM: shm_open / mmap, Per-User SHM Naming, SHM Platform Module (cross-platform shared memory)

### Community 50 - "Agent Test Case Group A"
Cohesion: 0.67
Nodes (3): Zero-Copy Shared Memory Architecture, SHM Uses Offsets Not Pointers, Flexible Array Member Pattern for SHM Sections

### Community 51 - "Agent Test Case Group B"
Cohesion: 0.67
Nodes (3): Fuzzy Matching with Damerau-Levenshtein Distance, Matcher Module (chain + fuzzy matching), P0.2: Fix Matcher Name-Index Correctness

### Community 52 - "IPC CLI Entry Point"
Cohesion: 1.0
Nodes (0): 

### Community 53 - "Header Size Definitions"
Cohesion: 1.0
Nodes (0): 

### Community 54 - "Doc & Report Test IDs"
Cohesion: 1.0
Nodes (2): Lessons Learned Document, Agent Rule: Self-Improvement via lessons_learned

### Community 55 - "Agent & MCP Test IDs"
Cohesion: 1.0
Nodes (2): Platform Detection Macros, Platform Module (NCD-specific platform wrappers)

### Community 56 - "PowerShell NCD Module"
Cohesion: 1.0
Nodes (0): 

### Community 57 - "PowerShell Completion"
Cohesion: 1.0
Nodes (0): 

### Community 58 - "PowerShell Test Util"
Cohesion: 1.0
Nodes (0): 

### Community 59 - "Python Discovery Module"
Cohesion: 1.0
Nodes (0): 

### Community 60 - "PowerShell Integration"
Cohesion: 1.0
Nodes (0): 

### Community 61 - "Shared State Header"
Cohesion: 1.0
Nodes (0): 

### Community 62 - "Platform Header"
Cohesion: 1.0
Nodes (0): 

### Community 63 - "Database Header"
Cohesion: 1.0
Nodes (0): 

### Community 64 - "Matcher Header"
Cohesion: 1.0
Nodes (0): 

### Community 65 - "Scanner Header"
Cohesion: 1.0
Nodes (0): 

### Community 66 - "UI Header"
Cohesion: 1.0
Nodes (0): 

### Community 67 - "Service State Header"
Cohesion: 1.0
Nodes (0): 

### Community 68 - "State Backend Header"
Cohesion: 1.0
Nodes (0): 

### Community 69 - "SHM Platform Header"
Cohesion: 1.0
Nodes (0): 

### Community 70 - "Control IPC Header"
Cohesion: 1.0
Nodes (0): 

### Community 71 - "CLI Header"
Cohesion: 1.0
Nodes (0): 

### Community 72 - "Service Publish Header"
Cohesion: 1.0
Nodes (0): 

### Community 73 - "NCD Core Header"
Cohesion: 1.0
Nodes (0): 

### Community 74 - "Agent Test Header"
Cohesion: 1.0
Nodes (0): 

### Community 75 - "Test Framework Header"
Cohesion: 1.0
Nodes (0): 

### Community 76 - "Fuzz Database Header"
Cohesion: 1.0
Nodes (0): 

### Community 77 - "Python Env Module"
Cohesion: 1.0
Nodes (0): 

### Community 78 - "IPC Test Common Header"
Cohesion: 1.0
Nodes (0): 

### Community 79 - "Service Test Common Header"
Cohesion: 1.0
Nodes (0): 

### Community 80 - "Test Startup C"
Cohesion: 1.0
Nodes (0): 

### Community 81 - "Test Legacy Header"
Cohesion: 1.0
Nodes (0): 

### Community 82 - "Bench Matcher Header"
Cohesion: 1.0
Nodes (0): 

### Community 83 - "Posix Exit Header"
Cohesion: 1.0
Nodes (0): 

### Community 84 - "Doc: MCP Server Quality"
Cohesion: 1.0
Nodes (1): String Builder (dynamic string construction)

### Community 85 - "Doc: Agent Rules Quality"
Cohesion: 1.0
Nodes (1): Common Module (memory allocation wrappers)

### Community 86 - "Doc: Coverage Report"
Cohesion: 1.0
Nodes (1): Agent Rule: Plan Mode Default

### Community 87 - "Doc: Best Practices Testing"
Cohesion: 1.0
Nodes (1): Agent Rule: Liberal Subagent Use

### Community 88 - "Doc: PowerShell Runner"
Cohesion: 1.0
Nodes (1): Agent Rule: Verification Before Done

## Knowledge Gaps
- **130 isolated node(s):** `Locate the NCD binary using multiple fallback strategies.`, `Run an NCD agent-mode command and return the parsed output.`, `Search the NCD indexed database for directories matching the search term.`, `List live filesystem contents at the given path.`, `Show directory structure from the NCD database.` (+125 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **Thin community `IPC CLI Entry Point`** (2 nodes): `test_startup.c`, `main()`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Header Size Definitions`** (2 nodes): `shm_types.h`, `shm_align_size()`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Doc & Report Test IDs`** (2 nodes): `Lessons Learned Document`, `Agent Rule: Self-Improvement via lessons_learned`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Agent & MCP Test IDs`** (2 nodes): `Platform Detection Macros`, `Platform Module (NCD-specific platform wrappers)`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `PowerShell NCD Module`** (1 nodes): `install_vs_arm64.ps1`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `PowerShell Completion`** (1 nodes): `install_vs_arm64_tools.ps1`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `PowerShell Test Util`** (1 nodes): `install_vs_native_desktop.ps1`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Python Discovery Module`** (1 nodes): `test_stale_socket.py`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `PowerShell Integration`** (1 nodes): `ncd.ps1`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Shared State Header`** (1 nodes): `cli.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Platform Header`** (1 nodes): `control_ipc.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Database Header`** (1 nodes): `control_ipc_common.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Matcher Header`** (1 nodes): `database.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Scanner Header`** (1 nodes): `matcher.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `UI Header`** (1 nodes): `ncd.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Service State Header`** (1 nodes): `platform.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `State Backend Header`** (1 nodes): `result.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `SHM Platform Header`** (1 nodes): `scanner.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Control IPC Header`** (1 nodes): `service_publish.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `CLI Header`** (1 nodes): `service_state.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Service Publish Header`** (1 nodes): `shared_state.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `NCD Core Header`** (1 nodes): `shm_platform.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Agent Test Header`** (1 nodes): `state_backend.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Test Framework Header`** (1 nodes): `ui.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Fuzz Database Header`** (1 nodes): `agent_test_common.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Python Env Module`** (1 nodes): `generate_report.py`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `IPC Test Common Header`** (1 nodes): `ipc_test_common.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Service Test Common Header`** (1 nodes): `service_test_common.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Test Startup C`** (1 nodes): `test_framework.c`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Test Legacy Header`** (1 nodes): `test_framework.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Bench Matcher Header`** (1 nodes): `test_main_internal.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Posix Exit Header`** (1 nodes): `test_main_shim.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Doc: MCP Server Quality`** (1 nodes): `String Builder (dynamic string construction)`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Doc: Agent Rules Quality`** (1 nodes): `Common Module (memory allocation wrappers)`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Doc: Coverage Report`** (1 nodes): `Agent Rule: Plan Mode Default`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Doc: Best Practices Testing`** (1 nodes): `Agent Rule: Liberal Subagent Use`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Doc: PowerShell Runner`** (1 nodes): `Agent Rule: Verification Before Done`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `parse_args()` connect `Python Test Harness` to `IPC Protocol & Service Control`, `TUI Terminal UI`, `Agent Mode CLI Config`, `Test Runner & Build Infra`?**
  _High betweenness centrality (0.155) - this node is a cross-community bridge._
- **Why does `main()` connect `IPC Protocol & Service Control` to `Python Test Harness`?**
  _High betweenness centrality (0.140) - this node is a cross-community bridge._
- **Why does `db_free()` connect `Database Core & Search Engine` to `Packaging & Deployment Scripts`, `IPC Message Transport Layer`, `Agent Mode Filesystem Ops`, `Database Test Utilities`, `Service Source Modules (.c)`, `Test Source Modules (.c)`, `Memory & GC Management`, `Binary DB Format & CRC`?**
  _High betweenness centrality (0.131) - this node is a cross-community bridge._
- **Are the 253 inferred relationships involving `db_free()` (e.g. with `remove_path_from_database()` and `name_index_free()`) actually correct?**
  _`db_free()` has 253 INFERRED edges - model-reasoned connections that need verification._
- **Are the 170 inferred relationships involving `db_create()` (e.g. with `agent_mode_mv()` and `add_path_to_database()`) actually correct?**
  _`db_create()` has 170 INFERRED edges - model-reasoned connections that need verification._
- **Are the 136 inferred relationships involving `ipc_client_disconnect()` (e.g. with `check_service_version()` and `resolve_text_encoding()`) actually correct?**
  _`ipc_client_disconnect()` has 136 INFERRED edges - model-reasoned connections that need verification._
- **Are the 129 inferred relationships involving `ipc_client_connect()` (e.g. with `check_service_version()` and `resolve_text_encoding()`) actually correct?**
  _`ipc_client_connect()` has 129 INFERRED edges - model-reasoned connections that need verification._