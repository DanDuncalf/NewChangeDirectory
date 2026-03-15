#!/usr/bin/env bash
#
# ncd.sh  --  Norton Change Directory wrapper for Linux
#
# Source this file (do NOT execute it) so that the cd command takes effect
# in the current shell session:
#
#   source ~/bin/ncd.sh "$@"
#   # or via an alias:
#   alias ncd='source /path/to/ncd.sh'
#
# Why a wrapper?
#   A child process cannot change the working directory of its parent shell
#   (OS limitation).  This script sources the result file written by
#   NewChangeDirectory so that the exported variables are visible here,
#   then calls 'cd' directly.
#
# Environment variables set by ncd_result.sh:
#   NCD_STATUS   OK | ERROR
#   NCD_DRIVE    (empty on Linux)
#   NCD_PATH     /home/user/Downloads  (empty on error)
#   NCD_MESSAGE  Human-readable status / error string
#

# -----------------------------------------------------------------------
# 0.  Locate the NewChangeDirectory binary (same directory as this script,
#     or on PATH).
# -----------------------------------------------------------------------
_ncd_script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -x "${_ncd_script_dir}/NewChangeDirectory" ]]; then
    _ncd_bin="${_ncd_script_dir}/NewChangeDirectory"
elif command -v NewChangeDirectory &>/dev/null; then
    _ncd_bin="NewChangeDirectory"
else
    echo "ncd: NewChangeDirectory binary not found." >&2
    unset _ncd_script_dir _ncd_bin
    return 1
fi

# -----------------------------------------------------------------------
# 1.  Determine result file path (mirrors platform_get_temp_path logic).
# -----------------------------------------------------------------------
_ncd_result="${XDG_RUNTIME_DIR:-/tmp}/ncd_result.sh"

# Clean up any leftover result file from a previous run.
rm -f "${_ncd_result}" 2>/dev/null

# -----------------------------------------------------------------------
# 2.  Initialise output variables so they are always set even if the
#     binary fails to create the result file.
# -----------------------------------------------------------------------
NCD_STATUS="ERROR"
NCD_DRIVE=""
NCD_PATH=""
NCD_MESSAGE="NewChangeDirectory did not produce a result."

# -----------------------------------------------------------------------
# 3.  Run the binary -- pass all arguments straight through.
# -----------------------------------------------------------------------
"${_ncd_bin}" "$@"

# -----------------------------------------------------------------------
# 4.  Source the result file (sets NCD_STATUS, NCD_PATH, NCD_MESSAGE).
# -----------------------------------------------------------------------
if [[ -f "${_ncd_result}" ]]; then
    # shellcheck disable=SC1090
    source "${_ncd_result}"
    rm -f "${_ncd_result}" 2>/dev/null
fi

# -----------------------------------------------------------------------
# 5.  Act on the result.
# -----------------------------------------------------------------------
if [[ "${NCD_STATUS}" == "OK" && -n "${NCD_PATH}" ]]; then
    cd "${NCD_PATH}" || {
        echo "ncd: cd failed: ${NCD_PATH}" >&2
    }
    [[ -n "${NCD_MESSAGE}" ]] && echo "${NCD_MESSAGE}"
else
    [[ -n "${NCD_MESSAGE}" ]] && echo "${NCD_MESSAGE}" >&2
fi

# -----------------------------------------------------------------------
# 6.  Clean up variables so they don't pollute the shell session.
# -----------------------------------------------------------------------
unset NCD_STATUS NCD_DRIVE NCD_PATH NCD_MESSAGE
unset _ncd_script_dir _ncd_bin _ncd_result
