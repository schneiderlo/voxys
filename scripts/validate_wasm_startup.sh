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
for artifact in voxy_wasm_cc.js voxy_wasm_cc.wasm voxy_wasm_cc.data; do
    if [[ ! -f "$artifact_directory/$artifact" ]]; then
        echo "Missing WASM artifact: $artifact_directory/$artifact" >&2
        exit 2
    fi
done

temporary_directory="$(mktemp -d)"
deployment="$temporary_directory/deployment"
server_log="$temporary_directory/server.log"
port="${VOXY_WASM_TEST_PORT:-18767}"
debug_port="${VOXY_WASM_DEBUG_PORT:-18768}"
server_pid=""
browser_pid=""

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
cp "$artifact_directory/voxy_wasm_cc.js" "$deployment/voxy_wasm.js"
cp "$artifact_directory/voxy_wasm_cc.wasm" "$deployment/"
cp "$artifact_directory/voxy_wasm_cc.data" "$deployment/"

python3 -m http.server "$port" --bind 127.0.0.1 \
    --directory "$deployment" >"$server_log" 2>&1 &
server_pid=$!
for _ in {1..50}; do
    if curl --silent --fail "http://127.0.0.1:$port/index.html" >/dev/null; then
        break
    fi
    sleep 0.1
done

env -u VK_ICD_FILENAMES -u VK_DRIVER_FILES "$browser" \
    --headless=new \
    --no-sandbox \
    --user-data-dir="$temporary_directory/chrome-profile" \
    --disable-gpu-sandbox \
    --enable-unsafe-webgpu \
    --enable-unsafe-swiftshader \
    --enable-features=Vulkan \
    --use-angle=swiftshader \
    --use-vulkan=swiftshader \
    --disable-vulkan-surface \
    --remote-debugging-port="$debug_port" \
    "http://127.0.0.1:$port/index.html?physicsMaxBodies=4096" \
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
