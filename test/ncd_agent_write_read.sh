# ncd_agent_write_read.sh -- shared by test_ncd_wsl_standalone.sh and
# test_ncd_wsl_with_service.sh (sourced, not run).
#
# Agent writes (mkdir/mv/rmdir) followed by agent reads of the same paths;
# standalone and service mode must agree. The parent directory is created
# behind NCD's back, so it is not indexed: the write must add the missing
# ancestor rather than file the leaf at the drive root.
#
# Needs from the caller: $NCD, $TESTROOT, pass(), fail(), ncd_run() (sets
# LAST_OUT / LAST_EXIT).
#
# Windows counterpart: Invoke-NcdAgentWriteReadTests in test/Win/NcdIntegration.psm1.

# First output line with any CR removed (agent output ends lines with CRLF).
_first_line() { printf '%s\n' "$1" | tr -d '\r' | head -n 1; }

run_agent_write_read_tests() {
    local mode="$1"
    local parent="$TESTROOT/Projects/unindexed_$mode"
    local leaf_a="$parent/agentdir_a_$mode"
    local leaf_b="$parent/agentdir_b_$mode"
    local name
    rm -rf "$parent"
    mkdir -p "$parent"

    name="Agent mkdir under an unindexed parent is found by query ($mode)"
    echo "[TEST] $name"
    ncd_run --agent:mkdir "$leaf_a" --json
    if [[ $LAST_EXIT -ne 0 ]]; then
        fail "$name" "mkdir exit $LAST_EXIT: $LAST_OUT"
    else
        ncd_run --agent:query "agentdir_a_$mode" --limit 1
        if [[ "$(_first_line "$LAST_OUT")" == "$leaf_a" ]]; then
            pass "$name"
        else
            fail "$name" "query got '$LAST_OUT', expected '$leaf_a'"
        fi
    fi

    name="Agent mv: query finds the new path, not the old ($mode)"
    echo "[TEST] $name"
    ncd_run --agent:mv "$leaf_a" "$leaf_b" --json
    if [[ $LAST_EXIT -ne 0 ]]; then
        fail "$name" "mv exit $LAST_EXIT: $LAST_OUT"
    else
        ncd_run --agent:query "agentdir_b_$mode" --limit 1
        local got_new; got_new="$(_first_line "$LAST_OUT")"
        ncd_run --agent:query "agentdir_a_$mode"
        if [[ "$got_new" != "$leaf_b" ]]; then
            fail "$name" "query new got '$got_new', expected '$leaf_b'"
        elif [[ $LAST_EXIT -eq 0 ]]; then
            fail "$name" "old path still indexed: $LAST_OUT"
        else
            pass "$name"
        fi
    fi

    name="Agent check and complete see the moved path ($mode)"
    echo "[TEST] $name"
    ncd_run --agent:check "$leaf_b" --json
    if ! echo "$LAST_OUT" | grep -q '"in_db":true'; then
        fail "$name" "check: $LAST_OUT"
    else
        ncd_run --agent:complete "agentdir_b_"
        if echo "$LAST_OUT" | grep -q "agentdir_b_$mode"; then
            pass "$name"
        else
            fail "$name" "complete: $LAST_OUT"
        fi
    fi

    name="Agent rmdir: query no longer finds it ($mode)"
    echo "[TEST] $name"
    ncd_run --agent:rmdir "$leaf_b" --json
    if [[ $LAST_EXIT -ne 0 ]]; then
        fail "$name" "rmdir exit $LAST_EXIT: $LAST_OUT"
    else
        ncd_run --agent:query "agentdir_b_$mode"
        if [[ $LAST_EXIT -eq 0 ]]; then
            fail "$name" "removed path still indexed: $LAST_OUT"
        else
            pass "$name"
        fi
    fi

    rm -rf "$parent"
}
