import subprocess
import os
import sys
import time
import glob
import check

def run_test(executable, input_file, time_limit):
    try:
        start_time = time.time()
        result = subprocess.run(
            [executable, input_file],
            capture_output=True,
            text=True,
            timeout=time_limit
        )
        end_time = time.time()
        return result.stdout, result.stderr, result.returncode, end_time - start_time
    except subprocess.TimeoutExpired:
        return None, "Timeout", -1, time_limit

def main():
    if len(sys.argv) < 4:
        print("Usage: python3 grader.py <executable> <input_dir> <time_limit>")
        sys.exit(1)

    executable = sys.argv[1]
    input_dir = sys.argv[2]
    time_limit = float(sys.argv[3])

    if not os.path.exists(executable):
        print(f"Error: Executable {executable} not found")
        sys.exit(1)
        
    if not os.path.exists(input_dir):
        print(f"Error: Input directory {input_dir} not found")
        sys.exit(1)

    input_files = sorted(glob.glob(os.path.join(input_dir, "*.in")))
    
    if not input_files:
        print(f"No .in files found in {input_dir}")
        sys.exit(0)
        
    print(f"Running {executable} on {len(input_files)} files in {input_dir} with {time_limit}s limit")
    print("-" * 60)
    print(f"{'File':<20} | {'Status':<10} | {'Time':<8} | {'Message'}")
    print("-" * 60)
    
    passed = 0
    total = 0
    durations = []
    
    for input_file in input_files:
        total += 1
        filename = os.path.basename(input_file)
        
        stdout, stderr, ret, duration = run_test(executable, input_file, time_limit)
        
        status = ""
        message = ""
        
        if ret == -1:
            status = "TIMEOUT"
            message = f"> {time_limit}s"
        elif ret != 0:
            status = "CRASH"
            message = f"Return code {ret}"
        else:
            # Verify output
            success, msg = check.check(stdout)
            if success:
                status = "PASS"
                passed += 1
            else:
                status = "FAIL"
                print(f"{filename} output:\n{stdout}\n")
                message = msg
                
        print(f"{filename:<20} | {status:<10} | {duration:.4f}s | {message}")
        durations.append(duration)
        
    print("-" * 60)
    print(f"Summary: {passed}/{total} passed, Average Time: {sum(durations)/total:.4f}s")

if __name__ == "__main__":
    main()
