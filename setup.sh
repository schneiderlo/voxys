#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════════════
# setup.sh - Initial environment setup for voxy
# ═══════════════════════════════════════════════════════════════════════════════
# This script automates:
#   1. Fetching native dependencies (GLM, zstd, GLFW, etc.)
#   2. Installing and activating EMSDK (v4.0.17)
#   3. Pre-fetching the emdawnwebgpu port for WASM builds
#   4. Setting up git hooks
# ═══════════════════════════════════════════════════════════════════════════════

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THIRD_PARTY_DIR="$PROJECT_ROOT/third_party"
EMSDK_DIR="$THIRD_PARTY_DIR/emsdk"
EMSCRIPTEN_VERSION="4.0.17"

echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}  voxy Initial Setup Script${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo ""

# ─────────────────────────────────────────────────────────────────────────────
# 1. Prerequisites Check
# ─────────────────────────────────────────────────────────────────────────────
echo -e "${YELLOW}[1/5] Checking prerequisites...${NC}"
for cmd in git curl python3 unzip make g++; do
    if ! command -v $cmd &> /dev/null; then
        echo -e "  ${RED}✗${NC} $cmd is not installed. Please install it and try again."
        exit 1
    fi
done
echo -e "  ${GREEN}✓${NC} All prerequisites found"

# ─────────────────────────────────────────────────────────────────────────────
# 2. Fetch Native Dependencies
# ─────────────────────────────────────────────────────────────────────────────
echo -e "${YELLOW}[2/5] Fetching native dependencies...${NC}"
if [ -f "$PROJECT_ROOT/scripts/fetch_deps.sh" ]; then
    bash "$PROJECT_ROOT/scripts/fetch_deps.sh"
else
    echo -e "  ${RED}✗${NC} scripts/fetch_deps.sh not found!"
    exit 1
fi

# ─────────────────────────────────────────────────────────────────────────────
# 3. Setup EMSDK
# ─────────────────────────────────────────────────────────────────────────────
echo -e "${YELLOW}[3/5] Setting up EMSDK...${NC}"
mkdir -p "$THIRD_PARTY_DIR"

if [ ! -d "$EMSDK_DIR" ]; then
    echo "  Cloning EMSDK..."
    git clone https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
else
    echo "  EMSDK already exists, updating..."
    cd "$EMSDK_DIR" && git pull && cd "$PROJECT_ROOT"
fi

echo "  Installing Emscripten $EMSCRIPTEN_VERSION..."
"$EMSDK_DIR/emsdk" install "$EMSCRIPTEN_VERSION"
"$EMSDK_DIR/emsdk" activate "$EMSCRIPTEN_VERSION"

# ─────────────────────────────────────────────────────────────────────────────
# 4. Fetch emdawnwebgpu Port
# ─────────────────────────────────────────────────────────────────────────────
echo -e "${YELLOW}[4/5] Pre-fetching emdawnwebgpu port...${NC}"
source "$EMSDK_DIR/emsdk_env.sh" > /dev/null 2>&1

echo "  Running embuilder to fetch emdawnwebgpu..."
# This will download and unzip the port into the cache
python3 "$EMSDK_DIR/upstream/emscripten/embuilder.py" build emdawnwebgpu

echo -e "  ${GREEN}✓${NC} emdawnwebgpu port ready"

# ─────────────────────────────────────────────────────────────────────────────
# 5. Setup Git Hooks
# ─────────────────────────────────────────────────────────────────────────────
echo -e "${YELLOW}[5/5] Setting up Git hooks...${NC}"
if [ -f "$PROJECT_ROOT/scripts/setup-hooks.sh" ]; then
    bash "$PROJECT_ROOT/scripts/setup-hooks.sh"
else
    echo -e "  ${YELLOW}!${NC} scripts/setup-hooks.sh not found, skipping."
fi

echo ""
echo -e "${GREEN}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}  Setup completed successfully!${NC}"
echo -e "${GREEN}═══════════════════════════════════════════════════════════════${NC}"
echo ""
echo -e "${BLUE}Next steps:${NC}"
echo "  1. Build Native: bazel build //:voxy_native"
echo "  2. Build WASM:   bazel build --config=wasm //:voxy_wasm"
echo "  3. Serve WASM:  bazel run --config=wasm //tools:serve_wasm"
echo ""
