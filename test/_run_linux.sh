#!/bin/bash
cd /mnt/e/llama/NewChangeDirectory/test
export NCD_TEST_MODE=1
failed=0
for f in test_database test_matcher test_bugs test_db_corruption test_scanner test_metadata test_platform test_common test_strbuilder test_history test_config test_service_lazy_load test_service_integration test_legacy_service_shutdown test_service_version_compat test_service_lifecycle test_agent_mode test_cli_parse test_agent_integration test_agent_ls_check test_agent_query_tree test_ipc test_result_output test_shared_state test_shm_platform test_agent_cli_extended test_cli_parse_extended test_strbuilder_extended test_common_extended test_integration_extended test_database_extended test_odd_cases test_main test_ui_selector test_ui_history test_ui_config test_ui_navigator test_ui_extended test_ui_exclusions test_stress test_scanner_extended test_matcher_extended test_result_edge_cases test_cli_edge_cases test_config test_history test_metadata test_platform_extended test_framework_contract test_ipc_extended fuzz_database_load fuzz_ipc test_service_stress test_service_rescan test_service_init_db test_shm_stress test_service_race_conditions test_service_database test_service_ipc test_service_empty_db test_service_logging test_service_parity; do
  if [ -x "./$f" ]; then
    result=$(./$f 2>&1 | grep -E "Tests:.*run" | tail -1)
    if echo "$result" | grep -q "0 failed"; then
      echo "PASS $f: $result"
    else
      echo "FAIL $f: $result"
      failed=1
    fi
  else
    echo "SKIP $f (not built)"
  fi
done
echo ""
if [ $failed -eq 0 ]; then
  echo "ALL PASSED"
else
  echo "SOME FAILED"
fi
