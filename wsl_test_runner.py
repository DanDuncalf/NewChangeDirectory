import subprocess
import sys
import time
import os

def run_test(name, args, cwd="/mnt/e/llama/NewChangeDirectory/test", timeout=60):
    env = os.environ.copy()
    env["NCD_TEST_MODE"] = "1"
    env["NCD_UI_KEYS"] = "ENTER"
    env["XDG_DATA_HOME"] = f"/tmp/ncd_test_{name}"
    
    try:
        result = subprocess.run(
            ["wsl", "bash", "-c", f"cd {cwd} && {' '.join(args)}"],
            capture_output=True,
            text=True,
            timeout=timeout,
            env=env
        )
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired as e:
        return -1, e.stdout or "", e.stderr or ""

def main():
    tests = [
        ("test_database", ["./test_database"]),
        ("test_matcher", ["./test_matcher"]),
        ("test_scanner", ["./test_scanner"]),
        ("test_strbuilder", ["./test_strbuilder"]),
        ("test_common", ["./test_common"]),
        ("test_platform", ["./test_platform"]),
        ("test_metadata", ["./test_metadata"]),
        ("test_shared_state", ["./test_shared_state"]),
        ("test_bugs", ["./test_bugs"]),
        ("test_db_corruption", ["./test_db_corruption"]),
        ("test_cli_parse", ["./test_cli_parse"]),
        ("test_service_lifecycle", ["./test_service_lifecycle"]),
        ("test_service_integration", ["./test_service_integration"]),
        ("test_service_lazy_load", ["./test_service_lazy_load"]),
        ("test_service_version_compat", ["./test_service_version_compat"]),
    ]
    
    results = {}
    for name, args in tests:
        print(f"Running {name}...")
        rc, stdout, stderr = run_test(name, args)
        passed = rc == 0
        results[name] = {"rc": rc, "passed": passed, "stdout": stdout, "stderr": stderr}
        status = "PASS" if passed else ("TIMEOUT" if rc == -1 else "FAIL")
        print(f"  {status} (rc={rc})")
        if not passed and stdout:
            # Show last few lines of output
            lines = stdout.strip().split("\n")
            for line in lines[-10:]:
                print(f"    {line}")
        if not passed and stderr:
            lines = stderr.strip().split("\n")
            for line in lines[-5:]:
                print(f"    ERR: {line}")
    
    print("\n" + "="*50)
    print("SUMMARY")
    print("="*50)
    passed_count = sum(1 for v in results.values() if v["passed"])
    for name, res in results.items():
        status = "PASS" if res["passed"] else ("TIMEOUT" if res["rc"] == -1 else "FAIL")
        print(f"{name}: {status}")
    print(f"\nTotal: {len(results)}, Passed: {passed_count}, Failed: {len(results) - passed_count}")

if __name__ == "__main__":
    main()
