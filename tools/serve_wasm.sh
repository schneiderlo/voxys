#!/bin/bash
set -e

# --- Runfiles setup ---
# See https://github.com/bazelbuild/bazel/blob/master/tools/bash/runfiles/runfiles.bash
if [[ ! -d "${RUNFILES_DIR:-/dev/null}" && ! -f "${RUNFILES_MANIFEST_FILE:-/dev/null}" ]]; then
  if [[ -f "$0.runfiles_manifest" ]]; then
    export RUNFILES_MANIFEST_FILE="$0.runfiles_manifest"
  elif [[ -f "$0.runfiles/MANIFEST" ]]; then
    export RUNFILES_MANIFEST_FILE="$0.runfiles/MANIFEST"
  elif [[ -f "$0.runfiles/bazel_tools/tools/bash/runfiles/runfiles.bash" ]]; then
    export RUNFILES_DIR="$0.runfiles"
  fi
fi
if [[ -f "${RUNFILES_DIR:-/dev/null}/bazel_tools/tools/bash/runfiles/runfiles.bash" ]]; then
  source "${RUNFILES_DIR}/bazel_tools/tools/bash/runfiles/runfiles.bash"
elif [[ -f "${RUNFILES_MANIFEST_FILE:-/dev/null}" ]]; then
  source "$(grep -m1 "^bazel_tools/tools/bash/runfiles/runfiles.bash " "$RUNFILES_MANIFEST_FILE" | cut -d ' ' -f 2-)"
else
  echo >&2 "ERROR: cannot find @bazel_tools//tools/bash/runfiles:runfiles.bash"
  exit 1
fi
# --- End Runfiles setup ---

# Locate artifacts via runfiles
# The workspace name is "voxys"
WEB_INDEX=$(rlocation voxys/web/index.html)

# Locate WASM artifacts
# The target //:voxy_wasm produces files in voxy_wasm/ directory
WASM_JS=$(rlocation voxys/voxy_wasm/voxy_wasm_cc.js)
WASM_WASM=$(rlocation voxys/voxy_wasm/voxy_wasm_cc.wasm)
WASM_DATA=$(rlocation voxys/voxy_wasm/voxy_wasm_cc.data)

if [[ -z "$WEB_INDEX" ]]; then
  echo "ERROR: Could not locate web/index.html in runfiles."
  exit 1
fi

WEB_DIR=$(dirname "$WEB_INDEX")

if [[ -z "$WASM_JS" ]]; then
  echo "ERROR: Could not locate voxy_wasm_cc.js in runfiles."
  # Debug info
  echo "Listing runfiles voxys/voxy_wasm:"
  find . -path "*voxy_wasm*"
  exit 1
fi

# Locate config file
VOXY_CFG=$(rlocation voxys/voxy.cfg)
if [[ -n "$VOXY_CFG" ]]; then
  echo "Using config file: $VOXY_CFG"
fi

# Create temp dir
SERVE_DIR=$(mktemp -d)
echo "Preparing server in $SERVE_DIR..."

# Copy web assets
cp -r "$WEB_DIR"/* "$SERVE_DIR/"

# Copy WASM artifacts explicitly
echo "Copying WASM artifacts..."
cp "$WASM_JS" "$SERVE_DIR/voxy_wasm_cc.js"

if [[ -n "$WASM_WASM" && -f "$WASM_WASM" ]]; then
  cp "$WASM_WASM" "$SERVE_DIR/voxy_wasm_cc.wasm"
else
  echo "WARNING: .wasm file not found or empty variable"
fi

if [[ -n "$WASM_DATA" && -f "$WASM_DATA" ]]; then
  cp "$WASM_DATA" "$SERVE_DIR/voxy_wasm_cc.data"
else
  echo "WARNING: .data file not found or empty variable"
fi

# Patch index.html to use the correct JS file
sed -i 's/voxy_wasm.js/voxy_wasm_cc.js/g' "$SERVE_DIR/index.html"

echo "Starting server at http://localhost:8081"
echo "Files being served:"
ls -lh "$SERVE_DIR"
echo "Note: voxy.cfg is bundled inside voxy_wasm_cc.data"
echo "Press Ctrl+C to stop."
python3 -m http.server 8081 --directory "$SERVE_DIR"
