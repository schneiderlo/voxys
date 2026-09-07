#!/usr/bin/env python3
"""Linux-only, opt-in cold-driver regression. Does not change system settings.

Select LLVMpipe with VK_DRIVER_FILES and its library path before running.
The old blocking-poll binary crashes; the completion-fence version passes.
Use separate output paths when comparing two binaries.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("executable", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
if not os.environ.get("VK_DRIVER_FILES"):
    parser.error("set VK_DRIVER_FILES to the LLVMpipe Vulkan ICD JSON")
env = dict(os.environ, LIBGL_ALWAYS_SOFTWARE="1", MESA_SHADER_CACHE_DISABLE="true",
           GALLIUM_OVERRIDE_CPU_CAPS="avx", LP_NUM_THREADS="2")
# The affinity change is confined to this diagnostic process and its child.
os.sched_setaffinity(0, sorted(os.sched_getaffinity(0))[:2])
args.output.parent.mkdir(parents=True, exist_ok=True)
command = [str(args.executable.resolve()),
           "--gtest_filter=LegoPlaygroundGpu.CompoundStudsSupportRealStackAndSleep"]
start = time.monotonic()
with args.output.open("w") as log:
    child = subprocess.Popen(command, env=env, stdout=log,
                             stderr=subprocess.STDOUT, start_new_session=True)
    def send(sig):
        try:
            os.killpg(child.pid, sig)
        except ProcessLookupError:
            pass  # The test can exit between poll() and the signal.
    try:
        while child.poll() is None:
            time.sleep(.02)
            elapsed = time.monotonic() - start
            if elapsed >= 120:
                send(signal.SIGKILL)
                break
            if elapsed < 85:
                send(signal.SIGSTOP)
                time.sleep(.08)
                send(signal.SIGCONT)
    except BaseException:
        send(signal.SIGKILL)
        child.wait()
        raise
    finally:
        if child.poll() is None:
            send(signal.SIGCONT)
    result = child.wait()
output = args.output.read_text()
valid_adapter = "Backend: Vulkan" in output and \
                "GPU Adapter: llvmpipe (LLVM 20.1.2" in output
report = {"exit_code": result, "seconds": time.monotonic() - start,
          "matching_ci_adapter": valid_adapter, "command": command,
          "icd": env["VK_DRIVER_FILES"], "slow_seconds": 85, "duty_cycle": .2}
args.output.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report))
raise SystemExit(0 if result == 0 and valid_adapter else 1)
