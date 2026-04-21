# Graph Report - .  (2026-04-19)

## Corpus Check
- 164 files · ~456,918 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 2354 nodes · 7519 edges · 83 communities detected
- Extraction: 55% EXTRACTED · 45% INFERRED · 0% AMBIGUOUS · INFERRED: 3391 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Database Core Operations|Database Core Operations]]
- [[_COMMUNITY_IPC Client Protocol|IPC Client Protocol]]
- [[_COMMUNITY_TUI Key Injection Tests|TUI Key Injection Tests]]
- [[_COMMUNITY_Config & Heuristics|Config & Heuristics]]
- [[_COMMUNITY_Agent CLI Extended Tests|Agent CLI Extended Tests]]
- [[_COMMUNITY_Shared Memory Snapshots|Shared Memory Snapshots]]
- [[_COMMUNITY_IPC Server Protocol|IPC Server Protocol]]
- [[_COMMUNITY_Argument Parsing & Glob Matching|Argument Parsing & Glob Matching]]
- [[_COMMUNITY_Agent Mode Parsing|Agent Mode Parsing]]
- [[_COMMUNITY_State Backend Management|State Backend Management]]
- [[_COMMUNITY_Agent Command Handlers|Agent Command Handlers]]
- [[_COMMUNITY_Build System & Test Framework|Build System & Test Framework]]
- [[_COMMUNITY_Result Writing & Heuristics|Result Writing & Heuristics]]
- [[_COMMUNITY_CLI Edge Case Tests|CLI Edge Case Tests]]
- [[_COMMUNITY_String Builder Tests|String Builder Tests]]
- [[_COMMUNITY_Community 15|Community 15]]
- [[_COMMUNITY_Community 16|Community 16]]
- [[_COMMUNITY_Community 17|Community 17]]
- [[_COMMUNITY_Community 18|Community 18]]
- [[_COMMUNITY_Community 19|Community 19]]
- [[_COMMUNITY_Community 20|Community 20]]
- [[_COMMUNITY_Community 21|Community 21]]
- [[_COMMUNITY_Community 22|Community 22]]
- [[_COMMUNITY_Community 23|Community 23]]
- [[_COMMUNITY_Community 24|Community 24]]
- [[_COMMUNITY_Community 25|Community 25]]
- [[_COMMUNITY_Community 26|Community 26]]
- [[_COMMUNITY_Community 27|Community 27]]
- [[_COMMUNITY_Community 28|Community 28]]
- [[_COMMUNITY_Community 29|Community 29]]
- [[_COMMUNITY_Community 30|Community 30]]
- [[_COMMUNITY_Community 31|Community 31]]
- [[_COMMUNITY_Community 32|Community 32]]
- [[_COMMUNITY_Community 33|Community 33]]
- [[_COMMUNITY_Community 34|Community 34]]
- [[_COMMUNITY_Community 35|Community 35]]
- [[_COMMUNITY_Community 36|Community 36]]
- [[_COMMUNITY_Community 37|Community 37]]
- [[_COMMUNITY_Community 38|Community 38]]
- [[_COMMUNITY_Community 39|Community 39]]
- [[_COMMUNITY_Community 40|Community 40]]
- [[_COMMUNITY_Community 41|Community 41]]
- [[_COMMUNITY_Community 42|Community 42]]
- [[_COMMUNITY_Community 43|Community 43]]
- [[_COMMUNITY_Community 44|Community 44]]
- [[_COMMUNITY_Community 45|Community 45]]
- [[_COMMUNITY_Community 46|Community 46]]
- [[_COMMUNITY_Community 47|Community 47]]
- [[_COMMUNITY_Community 48|Community 48]]
- [[_COMMUNITY_Community 49|Community 49]]
- [[_COMMUNITY_Community 50|Community 50]]
- [[_COMMUNITY_Community 51|Community 51]]
- [[_COMMUNITY_Community 52|Community 52]]
- [[_COMMUNITY_Community 53|Community 53]]
- [[_COMMUNITY_Community 54|Community 54]]
- [[_COMMUNITY_Community 55|Community 55]]
- [[_COMMUNITY_Community 56|Community 56]]
- [[_COMMUNITY_Community 57|Community 57]]
- [[_COMMUNITY_Community 58|Community 58]]
- [[_COMMUNITY_Community 59|Community 59]]
- [[_COMMUNITY_Community 60|Community 60]]
- [[_COMMUNITY_Community 61|Community 61]]
- [[_COMMUNITY_Community 62|Community 62]]
- [[_COMMUNITY_Community 63|Community 63]]
- [[_COMMUNITY_Community 64|Community 64]]
- [[_COMMUNITY_Community 65|Community 65]]
- [[_COMMUNITY_Community 66|Community 66]]
- [[_COMMUNITY_Community 67|Community 67]]
- [[_COMMUNITY_Community 68|Community 68]]
- [[_COMMUNITY_Community 69|Community 69]]
- [[_COMMUNITY_Community 70|Community 70]]
- [[_COMMUNITY_Community 71|Community 71]]
- [[_COMMUNITY_Community 72|Community 72]]
- [[_COMMUNITY_Community 73|Community 73]]
- [[_COMMUNITY_Community 74|Community 74]]
- [[_COMMUNITY_Community 75|Community 75]]
- [[_COMMUNITY_Community 76|Community 76]]
- [[_COMMUNITY_Community 77|Community 77]]
- [[_COMMUNITY_Community 78|Community 78]]
- [[_COMMUNITY_Community 79|Community 79]]
- [[_COMMUNITY_Community 80|Community 80]]
- [[_COMMUNITY_Community 81|Community 81]]
- [[_COMMUNITY_Community 82|Community 82]]

## God Nodes (most connected - your core abstractions)
1. `db_free()` - 241 edges
2. `db_create()` - 162 edges
3. `ipc_client_disconnect()` - 118 edges
4. `db_add_dir()` - 115 edges
5. `ipc_client_connect()` - 113 edges
6. `db_add_drive()` - 108 edges
7. `db_metadata_free()` - 105 edges
8. `ui_inject_keys()` - 104 edges
9. `ui_set_io_backend()` - 97 edges
10. `run_test()` - 96 edges

## Surprising Connections (you probably didn't know these)
- `suite_fuzz_database_load()` --calls--> `run_test()`  [INFERRED]
  test\fuzz_database_load.c → wsl_test_runner.py
- `suite_agent_integration()` --calls--> `run_test()`  [INFERRED]
  test\test_agent_integration.c → wsl_test_runner.py
- `suite_agent_ls()` --calls--> `run_test()`  [INFERRED]
  test\test_agent_ls_check.c → wsl_test_runner.py
- `suite_agent_check()` --calls--> `run_test()`  [INFERRED]
  test\test_agent_ls_check.c → wsl_test_runner.py
- `suite_agent_mode()` --calls--> `run_test()`  [INFERRED]
  test\test_agent_mode.c → wsl_test_runner.py

## Communities

### Community 0 - "Database Core Operations"
Cohesion: 0.02
Nodes (295): benchmark_search(), generate_synthetic_db(), main(), db_add_dir(), db_add_drive(), db_check_file_version(), db_create(), db_filter_excluded() (+287 more)

### Community 1 - "IPC Client Protocol"
Cohesion: 0.03
Nodes (258): ipc_client_check_version(), ipc_client_cleanup(), ipc_client_connect(), ipc_client_disconnect(), ipc_client_get_detailed_status(), ipc_client_get_state_info(), ipc_client_get_version(), ipc_client_init() (+250 more)

### Community 2 - "TUI Key Injection Tests"
Cohesion: 0.04
Nodes (181): inject_case_insensitive(), inject_clear_keys(), inject_config_sequence(), inject_empty(), inject_from_bad_file(), inject_from_file(), inject_mixed_sequence(), inject_navigation_keys() (+173 more)

### Community 3 - "Config & Heuristics"
Cohesion: 0.03
Nodes (188): db_check_all_versions(), db_config_exists(), db_config_init_defaults(), db_config_load(), db_config_path(), db_config_save(), db_default_path(), db_dir_history_add() (+180 more)

### Community 4 - "Agent CLI Extended Tests"
Cohesion: 0.02
Nodes (84): suite_agent_cli_extended(), agent_complete_basic(), agent_complete_json(), agent_complete_limit(), agent_complete_no_match(), agent_mkdir_creates_directory(), agent_mkdir_existing_directory(), agent_mkdir_invalid_path() (+76 more)

### Community 5 - "Shared Memory Snapshots"
Cohesion: 0.05
Nodes (104): build_database_snapshot(), build_metadata_snapshot(), compute_database_snapshot_size(), compute_metadata_snapshot_size(), compute_string_pool_size(), copy_ansi_to_shm(), derive_mount_point(), derive_volume_label() (+96 more)

### Community 6 - "IPC Server Protocol"
Cohesion: 0.05
Nodes (112): ipc_free_message(), ipc_server_accept(), ipc_server_cleanup(), ipc_server_close_connection(), ipc_server_receive(), ipc_server_send_error(), ipc_server_send_response(), ipc_server_send_version_mismatch() (+104 more)

### Community 7 - "Argument Parsing & Glob Matching"
Cohesion: 0.04
Nodes (83): apply_long_val(), apply_short_val(), find_value_sep(), is_bundle_bool(), parse_args(), parse_drive_list_token(), parse_skip_drive_list(), tokenize_args() (+75 more)

### Community 8 - "Agent Mode Parsing"
Cohesion: 0.06
Nodes (70): parse_agent_args(), glob_match(), agent_parse_case_insensitive_subcommand(), agent_parse_check_db_age(), agent_parse_check_json_and_flags(), agent_parse_check_path_and_flags(), agent_parse_check_path_only(), agent_parse_check_service_status() (+62 more)

### Community 9 - "State Backend Management"
Cohesion: 0.09
Nodes (42): close_state_backend(), ensure_state_initialized(), get_state_database(), get_state_metadata(), save_metadata_if_dirty(), set_error(), state_backend_close(), state_backend_error_string() (+34 more)

### Community 10 - "Agent Command Handlers"
Cohesion: 0.12
Nodes (49): agent_check_all_flags(), agent_complete_with_limit(), agent_ls_with_dirs_only(), agent_ls_with_files_only(), agent_mkdir_with_path(), agent_mkdirs_with_file(), agent_mode_without_subcommand_rejected(), agent_tree_with_flat_and_depth() (+41 more)

### Community 11 - "Build System & Test Framework"
Cohesion: 0.08
Nodes (42): binary_needs_rebuild(), build_linux_main(), build_linux_tests(), build_windows_main(), build_windows_tests(), _classify_block(), _cleanup_wsl_dir(), count_checks_in_script() (+34 more)

### Community 12 - "Result Writing & Heuristics"
Cohesion: 0.11
Nodes (44): heur_get_preferred(), heur_promote_match(), heur_sanitize(), result_cancel(), result_error(), result_ok(), write_result(), cleanup_result_file() (+36 more)

### Community 13 - "CLI Edge Case Tests"
Cohesion: 0.13
Nodes (43): parse_args(), a_flag_sets_both_hidden_and_system(), agent_ls_depth_zero_rejected(), agent_ls_pattern_and_default_depth(), agent_query_all_depth_and_limit_parse(), agent_tree_default_depth_is_three(), agent_tree_depth_zero_rejected(), bundle_with_unknown_char_is_unknown_option() (+35 more)

### Community 14 - "String Builder Tests"
Cohesion: 0.05
Nodes (1): suite_strbuilder_extended()

### Community 15 - "Community 15"
Cohesion: 0.12
Nodes (37): add_path_to_database(), agent_json_escape(), agent_mode_check(), agent_mode_ls(), agent_mode_mkdir(), agent_mode_mkdirs(), agent_mode_query(), agent_mode_tree() (+29 more)

### Community 16 - "Community 16"
Cohesion: 0.05
Nodes (1): suite_common_extended()

### Community 17 - "Community 17"
Cohesion: 0.17
Nodes (32): cli_agent_ls_missing_path(), cli_agent_missing_subcommand(), cli_agent_query_missing_search_term(), cli_agent_unknown_subcommand(), cli_argument_empty_string(), cli_argument_unicode_all_planes(), cli_argument_very_long_4096_chars(), cli_argument_with_embedded_whitespace() (+24 more)

### Community 18 - "Community 18"
Cohesion: 0.11
Nodes (21): errno_to_ipc(), ipc_client_check_version(), ipc_client_connect(), ipc_client_get_detailed_status(), ipc_client_get_state_info(), ipc_client_get_version(), ipc_client_init(), ipc_client_ping() (+13 more)

### Community 19 - "Community 19"
Cohesion: 0.29
Nodes (22): agent_check_db_age_json(), agent_check_missing_db(), agent_check_path_exists(), agent_check_path_json(), agent_check_path_not_found(), agent_check_service_status(), agent_check_stats_json(), agent_ls_basic_json() (+14 more)

### Community 20 - "Community 20"
Cohesion: 0.33
Nodes (21): agent_query_basic_json(), agent_query_basic_plain(), agent_query_case_insensitive(), agent_query_chain_search(), agent_query_limit(), agent_query_missing_db(), agent_query_no_match_json(), agent_tree_basic_json() (+13 more)

### Community 21 - "Community 21"
Cohesion: 0.33
Nodes (21): append_garbage(), corrupt_checksum_detected(), corrupt_checksum_with_test_nc(), corrupt_dir_count_overflow(), corrupt_drive_count_large(), corrupt_drive_count_overflow(), corrupt_drive_count_zero(), corrupt_magic_number() (+13 more)

### Community 22 - "Community 22"
Cohesion: 0.34
Nodes (21): a_flag_is_not_agent_alias(), agent_service_status_after_stop(), agent_service_status_json_not_running(), agent_service_status_json_running(), agent_service_status_not_running(), agent_service_status_running(), agentic_debug_mode_with_service_exits_cleanly(), ensure_service_stopped() (+13 more)

### Community 23 - "Community 23"
Cohesion: 0.15
Nodes (13): errno_to_shm(), set_last_errno(), shm_create(), shm_get_page_size(), shm_make_ctl_name(), shm_make_db_name(), shm_make_meta_name(), shm_make_name() (+5 more)

### Community 24 - "Community 24"
Cohesion: 0.27
Nodes (15): Build-Project(), Clear-TuiEnvironmentVariables(), Invoke-Test(), Remove-TestTempDirs(), Repair-Environment(), Restore-EnvironmentState(), Run-ParallelExpansionTests(), Run-UnitTests() (+7 more)

### Community 25 - "Community 25"
Cohesion: 0.2
Nodes (14): db_logs_path(), db_ext_logs_path_creation_fails(), database_directory_created_automatically(), dir_exists(), file_exists(), get_test_log_path(), get_test_logs_dir(), log_file_format_validation() (+6 more)

### Community 26 - "Community 26"
Cohesion: 0.38
Nodes (15): add_drive_header(), create_valid_db_header(), fuzz_db_checksum_collision(), fuzz_db_directory_entry_corruption(), fuzz_db_drive_header_corruption(), fuzz_db_header_random_corruption(), fuzz_db_mismatched_drive_counts(), fuzz_db_name_pool_corruption() (+7 more)

### Community 27 - "Community 27"
Cohesion: 0.38
Nodes (15): cleanup_test_env(), run_ncd_with_keys(), setup_test_env(), suite_tui_integration(), tui_config_adjust_timeout(), tui_config_cancel(), tui_config_end_key(), tui_config_home_key() (+7 more)

### Community 28 - "Community 28"
Cohesion: 0.48
Nodes (14): ensure_service_running(), ensure_service_stopped(), force_terminate_service(), legacy_shutdown_block_command_waits_for_stop(), legacy_shutdown_double_stop_is_safe(), legacy_shutdown_force_kill_as_last_resort(), legacy_shutdown_graceful_stop_succeeds(), legacy_shutdown_ipc_request_shutdown_works() (+6 more)

### Community 29 - "Community 29"
Cohesion: 0.38
Nodes (13): dir_exists(), get_temp_dir(), mkdirs_flat_format_empty_lines(), mkdirs_flat_format_nested(), mkdirs_flat_format_simple(), mkdirs_json_format_nested(), mkdirs_json_format_simple(), mkdirs_json_format_string_array() (+5 more)

### Community 30 - "Community 30"
Cohesion: 0.27
Nodes (10): Run-TestEnvironment(), Show-Summary(), Start-NcdServiceWindows(), Start-NcdServiceWsl(), Stop-NcdServiceWindows(), Stop-NcdServiceWsl(), Test-ServiceStatusWindows(), Test-ServiceStatusWsl() (+2 more)

### Community 31 - "Community 31"
Cohesion: 0.22
Nodes (9): agent_mkdir_creates_directory(), agent_mkdir_handles_existing_directory(), cleanup_test_dir(), config_rescan_interval_bounds(), config_rescan_interval_default(), config_rescan_interval_never(), dir_exists(), main() (+1 more)

### Community 32 - "Community 32"
Cohesion: 0.57
Nodes (6): Get-ExpectedLocalAppData(), Repair-Environment(), Test-Environment(), Test-IsTestTempPath(), Write-Finding(), Write-Section()

### Community 33 - "Community 33"
Cohesion: 0.29
Nodes (7): Database Module, Service Architecture, TUI System, Agent Mode API, IPC Control Channel, Shared Memory Architecture, NewChangeDirectory (NCD)

### Community 34 - "Community 34"
Cohesion: 0.33
Nodes (0): 

### Community 35 - "Community 35"
Cohesion: 0.6
Nodes (5): Check-LogErrors(), Clear-ServiceLog(), Get-ServiceLog(), Test-ServiceCycle(), Write-Header()

### Community 36 - "Community 36"
Cohesion: 0.4
Nodes (0): 

### Community 37 - "Community 37"
Cohesion: 0.5
Nodes (2): Test-Condition(), Write-TestLog()

### Community 38 - "Community 38"
Cohesion: 0.67
Nodes (2): shm_database_compute_layout(), shm_database_layout_free()

### Community 39 - "Community 39"
Cohesion: 0.5
Nodes (0): 

### Community 40 - "Community 40"
Cohesion: 0.67
Nodes (0): 

### Community 41 - "Community 41"
Cohesion: 1.0
Nodes (2): main(), run_test()

### Community 42 - "Community 42"
Cohesion: 1.0
Nodes (2): main(), run_shell_test()

### Community 43 - "Community 43"
Cohesion: 1.0
Nodes (2): main(), wildcard_match()

### Community 44 - "Community 44"
Cohesion: 1.0
Nodes (0): 

### Community 45 - "Community 45"
Cohesion: 1.0
Nodes (0): 

### Community 46 - "Community 46"
Cohesion: 1.0
Nodes (0): 

### Community 47 - "Community 47"
Cohesion: 1.0
Nodes (0): 

### Community 48 - "Community 48"
Cohesion: 1.0
Nodes (0): 

### Community 49 - "Community 49"
Cohesion: 1.0
Nodes (0): 

### Community 50 - "Community 50"
Cohesion: 1.0
Nodes (0): 

### Community 51 - "Community 51"
Cohesion: 1.0
Nodes (0): 

### Community 52 - "Community 52"
Cohesion: 1.0
Nodes (2): Agent Documentation, Agent Testing Guide

### Community 53 - "Community 53"
Cohesion: 1.0
Nodes (0): 

### Community 54 - "Community 54"
Cohesion: 1.0
Nodes (0): 

### Community 55 - "Community 55"
Cohesion: 1.0
Nodes (0): 

### Community 56 - "Community 56"
Cohesion: 1.0
Nodes (0): 

### Community 57 - "Community 57"
Cohesion: 1.0
Nodes (0): 

### Community 58 - "Community 58"
Cohesion: 1.0
Nodes (0): 

### Community 59 - "Community 59"
Cohesion: 1.0
Nodes (0): 

### Community 60 - "Community 60"
Cohesion: 1.0
Nodes (0): 

### Community 61 - "Community 61"
Cohesion: 1.0
Nodes (0): 

### Community 62 - "Community 62"
Cohesion: 1.0
Nodes (0): 

### Community 63 - "Community 63"
Cohesion: 1.0
Nodes (0): 

### Community 64 - "Community 64"
Cohesion: 1.0
Nodes (0): 

### Community 65 - "Community 65"
Cohesion: 1.0
Nodes (0): 

### Community 66 - "Community 66"
Cohesion: 1.0
Nodes (0): 

### Community 67 - "Community 67"
Cohesion: 1.0
Nodes (0): 

### Community 68 - "Community 68"
Cohesion: 1.0
Nodes (0): 

### Community 69 - "Community 69"
Cohesion: 1.0
Nodes (0): 

### Community 70 - "Community 70"
Cohesion: 1.0
Nodes (0): 

### Community 71 - "Community 71"
Cohesion: 1.0
Nodes (0): 

### Community 72 - "Community 72"
Cohesion: 1.0
Nodes (0): 

### Community 73 - "Community 73"
Cohesion: 1.0
Nodes (0): 

### Community 74 - "Community 74"
Cohesion: 1.0
Nodes (0): 

### Community 75 - "Community 75"
Cohesion: 1.0
Nodes (0): 

### Community 76 - "Community 76"
Cohesion: 1.0
Nodes (0): 

### Community 77 - "Community 77"
Cohesion: 1.0
Nodes (0): 

### Community 78 - "Community 78"
Cohesion: 1.0
Nodes (0): 

### Community 79 - "Community 79"
Cohesion: 1.0
Nodes (0): 

### Community 80 - "Community 80"
Cohesion: 1.0
Nodes (0): 

### Community 81 - "Community 81"
Cohesion: 1.0
Nodes (1): Scanner Module

### Community 82 - "Community 82"
Cohesion: 1.0
Nodes (1): Matcher Module

## Knowledge Gaps
- **27 isolated node(s):** `Run a command and return (returncode, stdout, stderr).`, `Check if WSL is installed and responsive.`, `Return file modification time as datetime or None.`, `Return the newest mtime among files matching glob patterns.`, `Check if a binary is missing or older than its sources.` (+22 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **Thin community `Community 44`** (2 nodes): `debug_args.c`, `main()`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 45`** (2 nodes): `debug_parse_db.py`, `reconstruct()`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 46`** (2 nodes): `service_rapid_test.ps1`, `Write-Header()`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 47`** (2 nodes): `service_spawn_test.ps1`, `Write-Header()`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 48`** (2 nodes): `stale_sock_test.c`, `main()`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 49`** (2 nodes): `test_getenv.c`, `main()`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 50`** (2 nodes): `shm_align_size()`, `shm_types.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 51`** (2 nodes): `main()`, `debug_test.c`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 52`** (2 nodes): `Agent Documentation`, `Agent Testing Guide`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 53`** (1 nodes): `fix_args.py`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 54`** (1 nodes): `fix_scanner.py`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 55`** (1 nodes): `test_ps_arg.ps1`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 56`** (1 nodes): `test_ps_arg2.ps1`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 57`** (1 nodes): `test_ps_arg3.ps1`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 58`** (1 nodes): `test_r.ps1`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 59`** (1 nodes): `test_stale_socket.py`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 60`** (1 nodes): `write_script.py`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 61`** (1 nodes): `ncd.ps1`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 62`** (1 nodes): `cli.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 63`** (1 nodes): `control_ipc.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 64`** (1 nodes): `database.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 65`** (1 nodes): `matcher.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 66`** (1 nodes): `ncd.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 67`** (1 nodes): `platform.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 68`** (1 nodes): `result.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 69`** (1 nodes): `scanner.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 70`** (1 nodes): `service_publish.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 71`** (1 nodes): `service_state.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 72`** (1 nodes): `shared_state.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 73`** (1 nodes): `shm_platform.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 74`** (1 nodes): `state_backend.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 75`** (1 nodes): `ui.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 76`** (1 nodes): `ipc_test_common.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 77`** (1 nodes): `test_framework.c`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 78`** (1 nodes): `test_framework.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 79`** (1 nodes): `test_main_internal.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 80`** (1 nodes): `test_main_shim.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 81`** (1 nodes): `Scanner Module`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 82`** (1 nodes): `Matcher Module`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `run_test()` connect `Agent CLI Extended Tests` to `Database Core Operations`, `IPC Client Protocol`, `TUI Key Injection Tests`, `Shared Memory Snapshots`, `Argument Parsing & Glob Matching`, `Agent Mode Parsing`, `State Backend Management`, `Result Writing & Heuristics`, `String Builder Tests`, `Community 16`, `Community 19`, `Community 20`, `Community 21`, `Community 22`, `Community 25`, `Community 26`, `Community 27`, `Community 28`, `Community 29`, `Community 31`?**
  _High betweenness centrality (0.327) - this node is a cross-community bridge._
- **Why does `db_free()` connect `Database Core Operations` to `Config & Heuristics`, `Agent CLI Extended Tests`, `Shared Memory Snapshots`, `IPC Server Protocol`, `State Backend Management`, `Community 15`, `Community 20`, `Community 21`, `Community 26`?**
  _High betweenness centrality (0.112) - this node is a cross-community bridge._
- **Why does `ipc_service_exists()` connect `IPC Client Protocol` to `Shared Memory Snapshots`, `State Backend Management`, `Community 15`, `Community 22`, `Community 28`?**
  _High betweenness centrality (0.051) - this node is a cross-community bridge._
- **Are the 240 inferred relationships involving `db_free()` (e.g. with `name_index_free()` and `flush_all_dirty_dbs()`) actually correct?**
  _`db_free()` has 240 INFERRED edges - model-reasoned connections that need verification._
- **Are the 161 inferred relationships involving `db_create()` (e.g. with `add_path_to_database()` and `perform_rescan()`) actually correct?**
  _`db_create()` has 161 INFERRED edges - model-reasoned connections that need verification._
- **Are the 117 inferred relationships involving `ipc_client_disconnect()` (e.g. with `check_service_version()` and `resolve_text_encoding()`) actually correct?**
  _`ipc_client_disconnect()` has 117 INFERRED edges - model-reasoned connections that need verification._
- **Are the 114 inferred relationships involving `db_add_dir()` (e.g. with `add_path_to_database()` and `scan_mount()`) actually correct?**
  _`db_add_dir()` has 114 INFERRED edges - model-reasoned connections that need verification._