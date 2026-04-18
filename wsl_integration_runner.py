import subprocess
import sys
import time
import os

def run_shell_test(name, script_path, cwd="/mnt/e/llama/NewChangeDirectory/test", timeout=120):
    env = os.environ.copy()
    env["NCD_TEST_MODE"] = "1"
    env["NCD_UI_KEYS"] = "ENTER"
    
    # Kill any lingering services first
    subprocess.run(["wsl", "bash", "-c", "pkill -9 -f 'ncd_service' 2>/dev/null; pkill -9 -f 'NCDService' 2>/dev/null; pkill -9 -f 'NewChangeDirectory' 2>/dev/null; sleep 1"], capture_output=True)
    
    try:
        result = subprocess.run(
            ["wsl", "bash", "-c", f"cd {cwd} && bash {script_path}"],
            capture_output=True,
            text=True,
            timeout=timeout,
            env=env
        )
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired as e:
        # Kill lingering processes after timeout
        subprocess.run(["wsl", "bash", "-c", "pkill -9 -f 'ncd_service' 2>/dev/null; pkill -9 -f 'NCDService' 2>/dev/null; pkill -9 -f 'NewChangeDirectory' 2>/dev/null"], capture_output=True)
        return -1, e.stdout or "", e.stderr or ""

def main():
    tests = [
        ("test_service_wsl", "test_service_wsl.sh"),
        ("test_ncd_wsl_standalone", "test_ncd_wsl_standalone.sh"),
        ("test_ncd_wsl_with_service", "test_ncd_wsl_with_service.sh"),
        ("test_integration", "Wsl/test_integration.sh"),
        ("test_features", "Wsl/test_features.sh"),
        ("test_agent_commands", "Wsl/test_agent_commands.sh"),
        ("test_recursive_mount", "Wsl/test_recursive_mount.sh"),
    ]
    
    results = {}
    for name, script in tests:
        print(f"\n{'='*50}")
        print(f"Running {name}...")
        print(f"{'='*50}")
        rc, stdout, stderr = run_shell_test(name, script)
        passed = rc == 0
        results[name] = {"rc": rc, "passed": passed, "stdout": stdout, "stderr": stderr}
        status = "PASS" if passed else ("TIMEOUT" if rc == -1 else "FAIL")
        print(f"Result: {status} (rc={rc})")
        
        # Show summary lines
        lines = stdout.split("\n")
        for line in lines:
            if "Test Summary" in line or "RESULT:" in line or "Total:" in line or "Passed:" in line or "Failed:" in line:
                print(f"  {line}")
        
        # Show last 20 lines for failures/timeouts
        if not passed:
            print("--- Last 30 lines of output ---")
            for line in lines[-30:]:
                print(f"  {line}")
            if stderr:
                err_lines = stderr.split("\n")
                for line in err_lines[-10:]:
                    print(f"  ERR: {line}")
    
    print("\n" + "="*50)
    print("INTEGRATION TEST SUMMARY")
    print("="*50)
    passed_count = sum(1 for v in results.values() if v["passed"])
    for name, res in results.items():
        status = "PASS" if res["passed"] else ("TIMEOUT" if res["rc"] == -1 else "FAIL")
        print(f"{name}: {status}")
    print(f"\nTotal: {len(results)}, Passed: {passed_count}, Failed: {len(results) - passed_count}")

if __name__ == "__main__":
    main()
