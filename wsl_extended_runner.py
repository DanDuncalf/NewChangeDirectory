import subprocess
import os

def run_test(name, args, cwd="/mnt/e/llama/NewChangeDirectory/test", timeout=60):
    env = os.environ.copy()
    env["NCD_TEST_MODE"] = "1"
    env["NCD_UI_KEYS"] = "ENTER"
    env["NCD_UI_KEYS_STRICT"] = "1"
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
        ("test_agent_mode", ["./test_agent_mode"]),
        ("test_agent_integration", ["./test_agent_integration"]),
        ("test_result_output", ["./test_result_output"]),
        ("test_integration_extended", ["./test_integration_extended"]),
        ("test_platform_extended", ["./test_platform_extended"]),
        ("test_strbuilder_extended", ["./test_strbuilder_extended"]),
        ("test_ui_extended", ["./test_ui_extended"]),
        ("test_ui_navigator", ["./test_ui_navigator"]),
    ]
    
    for name, args in tests:
        print(f"Running {name}...")
        rc, stdout, stderr = run_test(name, args)
        status = "PASS" if rc == 0 else ("TIMEOUT" if rc == -1 else "FAIL")
        print(f"  {status} (rc={rc})")
        if rc != 0:
            lines = stdout.strip().split("\n")
            for line in lines[-10:]:
                print(f"    {line}")
            if stderr:
                for line in stderr.strip().split("\n")[-5:]:
                    print(f"    ERR: {line}")

if __name__ == "__main__":
    main()
