#!/usr/bin/env bash
set -e
export NCD_TEST_MODE=1
export NCD_UI_KEYS=ENTER
export NCD_UI_KEYS_STRICT=1
export XDG_DATA_HOME=/tmp/ncd_unit_run_manual
mkdir -p "$XDG_DATA_HOME"
cd /mnt/e/llama/NewChangeDirectory/test

echo '=== test_database ==='
./test_database >/dev/null 2>&1 && echo PASS || echo FAIL
echo '=== test_matcher ==='
./test_matcher >/dev/null 2>&1 && echo PASS || echo FAIL
echo '=== test_scanner ==='
./test_scanner >/dev/null 2>&1 && echo PASS || echo FAIL
echo '=== test_strbuilder ==='
./test_strbuilder >/dev/null 2>&1 && echo PASS || echo FAIL
echo '=== test_common ==='
./test_common >/dev/null 2>&1 && echo PASS || echo FAIL
echo '=== test_platform ==='
./test_platform >/dev/null 2>&1 && echo PASS || echo FAIL
echo '=== test_metadata ==='
./test_metadata >/dev/null 2>&1 && echo PASS || echo FAIL
echo '=== test_shared_state ==='
./test_shared_state >/dev/null 2>&1 && echo PASS || echo FAIL
echo '=== test_bugs ==='
./test_bugs >/dev/null 2>&1 && echo PASS || echo FAIL
echo '=== test_db_corruption ==='
./test_db_corruption >/dev/null 2>&1 && echo PASS || echo FAIL
echo '=== test_cli_parse ==='
./test_cli_parse >/dev/null 2>&1 && echo PASS || echo FAIL
echo '=== test_service_lifecycle ==='
./test_service_lifecycle >/dev/null 2>&1 && echo PASS || echo FAIL
echo '=== test_service_integration ==='
./test_service_integration >/dev/null 2>&1 && echo PASS || echo FAIL
echo '=== test_service_lazy_load ==='
./test_service_lazy_load >/dev/null 2>&1 && echo PASS || echo FAIL
echo '=== test_service_version_compat ==='
./test_service_version_compat >/dev/null 2>&1 && echo PASS || echo FAIL
rm -rf "$XDG_DATA_HOME"
