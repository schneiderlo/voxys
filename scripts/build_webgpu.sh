#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════════════
# build_webgpu.sh - Build wgpu-native WebGPU implementation
# ═══════════════════════════════════════════════════════════════════════════════
# This script fetches and builds wgpu-native, a Rust-based WebGPU implementation
# that's easier to build than Dawn and fully compatible with the WebGPU spec.
#
# Requirements:
#   - Git
#   - Rust toolchain (rustc, cargo)
#   - CMake (optional, for CMake integration)
#
# Usage:
#   ./scripts/build_webgpu.sh [debug|release]
# ═══════════════════════════════════════════════════════════════════════════════

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Build configuration
BUILD_TYPE="${1:-release}"
if [[ "$BUILD_TYPE" != "debug" ]] && [[ "$BUILD_TYPE" != "release" ]]; then
    echo -e "${RED}Error: Build type must be 'debug' or 'release'${NC}"
    echo "Usage: $0 [debug|release]"
    exit 1
fi

# Get script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
THIRD_PARTY_DIR="$PROJECT_ROOT/third_party"
WGPU_DIR="$THIRD_PARTY_DIR/wgpu-native"

echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}  wgpu-native WebGPU Build Script${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo ""
echo -e "${CYAN}Build type: ${BUILD_TYPE}${NC}"
echo -e "${CYAN}wgpu-native directory: ${WGPU_DIR}${NC}"
echo ""

# ─────────────────────────────────────────────────────────────────────────────
# 1. Check for Rust toolchain
# ─────────────────────────────────────────────────────────────────────────────
echo -e "${YELLOW}[1/3] Checking Rust toolchain...${NC}"

if ! command -v rustc &> /dev/null; then
    echo -e "${RED}Error: Rust not found!${NC}"
    echo ""
    echo "Please install Rust from https://rustup.rs/"
    echo "Run: curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh"
    exit 1
fi

RUST_VERSION=$(rustc --version)
echo -e "  ${GREEN}✓${NC} Found: $RUST_VERSION"

# ─────────────────────────────────────────────────────────────────────────────
# 2. Fetch wgpu-native
# ─────────────────────────────────────────────────────────────────────────────
echo -e "${YELLOW}[2/3] Fetching wgpu-native...${NC}"

if [ ! -d "$WGPU_DIR/.git" ]; then
    echo "  Cloning wgpu-native repository..."
    cd "$THIRD_PARTY_DIR"
    git clone --depth 1 --branch v22.1.0.5 https://github.com/gfx-rs/wgpu-native.git
    cd "$WGPU_DIR"
else
    echo "  wgpu-native already cloned"
    cd "$WGPU_DIR"
fi

echo -e "${GREEN}✓ wgpu-native source ready${NC}"

# ─────────────────────────────────────────────────────────────────────────────
# 3. Build wgpu-native
# ─────────────────────────────────────────────────────────────────────────────
echo -e "${YELLOW}[3/3] Building wgpu-native...${NC}"
echo "  This may take several minutes on first build..."

# Determine Cargo build flags
if [ "$BUILD_TYPE" = "debug" ]; then
    CARGO_FLAGS=""
    BUILD_DIR="debug"
else
    CARGO_FLAGS="--release"
    BUILD_DIR="release"
fi

# Build wgpu-native
echo "  Running: cargo build $CARGO_FLAGS"
cargo build $CARGO_FLAGS

# Create a more discoverable directory structure for CMake
mkdir -p "$WGPU_DIR/dist/lib"
mkdir -p "$WGPU_DIR/dist/include"

# Copy library
if [ -f "target/$BUILD_DIR/libwgpu_native.so" ]; then
    cp "target/$BUILD_DIR/libwgpu_native.so" "$WGPU_DIR/dist/lib/"
    echo -e "  ${GREEN}✓${NC} Built libwgpu_native.so"
elif [ -f "target/$BUILD_DIR/libwgpu_native.dylib" ]; then
    cp "target/$BUILD_DIR/libwgpu_native.dylib" "$WGPU_DIR/dist/lib/"
    echo -e "  ${GREEN}✓${NC} Built libwgpu_native.dylib"
elif [ -f "target/$BUILD_DIR/libwgpu_native.a" ]; then
    cp "target/$BUILD_DIR/libwgpu_native.a" "$WGPU_DIR/dist/lib/"
    echo -e "  ${GREEN}✓${NC} Built libwgpu_native.a"
elif [ -f "target/$BUILD_DIR/wgpu_native.dll" ]; then
    cp "target/$BUILD_DIR/wgpu_native.dll" "$WGPU_DIR/dist/lib/"
    echo -e "  ${GREEN}✓${NC} Built wgpu_native.dll"
fi

# Copy header
if [ -f "ffi/webgpu-headers/webgpu.h" ]; then
    cp "ffi/webgpu-headers/webgpu.h" "$WGPU_DIR/dist/include/"
    echo -e "  ${GREEN}✓${NC} Copied webgpu.h"
elif [ -f "ffi/wgpu.h" ]; then
    cp "ffi/wgpu.h" "$WGPU_DIR/dist/include/webgpu.h"
    echo -e "  ${GREEN}✓${NC} Copied wgpu.h as webgpu.h"
fi

echo -e "${GREEN}✓ wgpu-native built successfully${NC}"

# ─────────────────────────────────────────────────────────────────────────────
# Summary
# ─────────────────────────────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}  wgpu-native WebGPU built successfully!${NC}"
echo -e "${GREEN}═══════════════════════════════════════════════════════════════${NC}"
echo ""
echo -e "${CYAN}Build artifacts:${NC}"
echo "  Library: $WGPU_DIR/target/$BUILD_DIR/"
echo "  Headers: $WGPU_DIR/dist/include/"
echo ""
echo -e "${CYAN}Next steps:${NC}"
echo "  1. Configure voxy:"
echo "     mkdir -p build && cd build"
echo "     cmake .. -DCMAKE_BUILD_TYPE=${BUILD_TYPE^}"
echo ""
echo "  2. Build voxy:"
echo "     cmake --build ."
echo ""
