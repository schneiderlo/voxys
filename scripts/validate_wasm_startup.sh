#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

browser="${CHROME_BIN:-}"
if [[ -z "$browser" ]]; then
    browser="$(command -v google-chrome || command -v chromium || command -v chromium-browser || true)"
fi
if [[ -z "$browser" ]]; then
    echo "Chrome/Chromium is required for WASM application validation" >&2
    exit 2
fi

artifact_directory="${1:-bazel-bin/voxy_wasm}"
artifact_base=""
for candidate in voxy_wasm_cc voxy_wasm; do
    if [[ -f "$artifact_directory/$candidate.js" &&
          -f "$artifact_directory/$candidate.wasm" &&
          -f "$artifact_directory/$candidate.data" ]]; then
        artifact_base="$candidate"
        break
    fi
done
if [[ -z "$artifact_base" ]]; then
    echo "Missing WASM .js/.wasm/.data set in: $artifact_directory" >&2
    exit 2
fi

temporary_directory="$(mktemp -d)"
deployment="$temporary_directory/deployment"
server_log="$temporary_directory/server.log"
port="${VOXY_WASM_TEST_PORT:-18767}"
debug_port="${VOXY_WASM_DEBUG_PORT:-18768}"
server_pid=""
browser_pid=""
gpu_mode="${VOXY_WASM_GPU_MODE:-swiftshader}"
if [[ "$gpu_mode" == "hardware" ]]; then
    gpu_flags=(
        --enable-unsafe-webgpu
        --enable-features=Vulkan
        --use-angle=vulkan
        --disable-vulkan-surface
    )
elif [[ "$gpu_mode" == "swiftshader" ]]; then
    gpu_flags=(
        --enable-unsafe-webgpu
        --enable-unsafe-swiftshader
        --use-webgpu-adapter=swiftshader
        --use-gpu-in-tests
        --enable-accelerated-2d-canvas
        # Complex WGSL compilation can exceed Chromium's software-GPU
        # watchdog even though the resulting pipeline executes correctly.
        --disable-gpu-watchdog
    )
else
    echo "VOXY_WASM_GPU_MODE must be 'swiftshader' or 'hardware'" >&2
    exit 2
fi

cleanup() {
    if [[ -n "$browser_pid" ]]; then
        kill "$browser_pid" 2>/dev/null || true
        wait "$browser_pid" 2>/dev/null || true
    fi
    if [[ -n "$server_pid" ]]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -rf "$temporary_directory"
}
trap cleanup EXIT

mkdir -p "$deployment"
cp web/index.html "$deployment/index.html"
cp web/loader.js web/network_transport.js web/style.css "$deployment/"
cp "$artifact_directory/$artifact_base.js" "$deployment/voxy_wasm.js"
cp "$artifact_directory/$artifact_base.wasm" "$deployment/"
cp "$artifact_directory/$artifact_base.data" "$deployment/"

python3 -m http.server "$port" --bind 127.0.0.1 \
    --directory "$deployment" >"$server_log" 2>&1 &
server_pid=$!
for _ in {1..50}; do
    if curl --silent --fail "http://127.0.0.1:$port/index.html" >/dev/null; then
        break
    fi
    sleep 0.1
done

test_url="http://127.0.0.1:$port/index.html?physicsMaxBodies=64&physicsSelfTest=1"

env -u VK_ICD_FILENAMES -u VK_DRIVER_FILES "$browser" \
    --headless=new \
    --no-sandbox \
    --user-data-dir="$temporary_directory/chrome-profile" \
    --disable-gpu-sandbox \
    "${gpu_flags[@]}" \
    --remote-debugging-port="$debug_port" \
    "$test_url" \
    >"$temporary_directory/chrome.log" 2>&1 &
browser_pid=$!

set +e
node ./scripts/wait_for_wasm_startup.mjs "$debug_port"
status=$?
set -e
if ((status != 0)); then
    tail -100 "$temporary_directory/chrome.log" >&2
    tail -100 "$server_log" >&2
fi
exit "$status"
