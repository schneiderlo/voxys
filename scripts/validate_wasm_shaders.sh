#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

browser="${CHROME_BIN:-}"
if [[ -z "$browser" ]]; then
    browser="$(command -v google-chrome || command -v chromium || command -v chromium-browser || true)"
fi
if [[ -z "$browser" ]]; then
    echo "Chrome/Chromium is required for WebGPU shader parity" >&2
    exit 2
fi

node - <<'NODE'
const fs = require("fs");
const expected = require("./web/shader_parity_manifest.js").slice().sort();
const actual = fs.readdirSync("./shaders")
    .filter((name) => name.endsWith(".wgsl"))
    .sort();
if (JSON.stringify(expected) !== JSON.stringify(actual)) {
    console.error("shader parity manifest is stale");
    console.error("expected manifest:", actual.join(", "));
    process.exit(1);
}
NODE

temporary_directory="$(mktemp -d)"
server_log="$temporary_directory/server.log"
port="${VOXY_SHADER_TEST_PORT:-18765}"
debug_port="${VOXY_SHADER_DEBUG_PORT:-18766}"
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
    for _ in {1..10}; do
        if rm -rf "$temporary_directory" 2>/dev/null; then
            return
        fi
        sleep 0.1
    done
    rm -rf "$temporary_directory"
}
trap cleanup EXIT

python3 -m http.server "$port" --bind 127.0.0.1 \
    --directory "$repo_root" >"$server_log" 2>&1 &
server_pid=$!

for _ in {1..50}; do
    if curl --silent --fail "http://127.0.0.1:$port/web/shader_parity.html" \
        >/dev/null; then
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
    --use-webgpu-adapter=swiftshader \
    --use-gpu-in-tests \
    --disable-gpu-watchdog \
    --remote-debugging-port="$debug_port" \
    "http://127.0.0.1:$port/web/shader_parity.html" \
    >"$temporary_directory/chrome.log" 2>&1 &
browser_pid=$!

set +e
node ./scripts/wait_for_shader_parity.mjs "$debug_port"
status=$?
set -e
if ((status != 0)); then
    tail -100 "$temporary_directory/chrome.log" >&2
fi
exit "$status"
