#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════════════
# fetch_deps.sh - Download third-party dependencies for voxy
# ═══════════════════════════════════════════════════════════════════════════════
# Usage: ./scripts/fetch_deps.sh
#
# This script downloads:
#   - GLM (0.9.9.8)          - Math library
#   - zstd (1.5.5)           - Compression
#   - GLFW (3.4)             - Windowing (native only)
#   - stb                    - Image loading
#   - wgpu-native (22.1.0.5) - WebGPU Native Implementation
#   - X11 Dev Headers        - For hermetic build of GLFW
#
# Note: Dawn must be installed separately. See README for instructions.
# ═══════════════════════════════════════════════════════════════════════════════

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Get script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
THIRD_PARTY_DIR="$PROJECT_ROOT/third_party"

echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}  voxy Dependency Fetcher${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo ""

# Create third_party directory
mkdir -p "$THIRD_PARTY_DIR"
cd "$THIRD_PARTY_DIR"

# ─────────────────────────────────────────────────────────────────────────────
# GLM - OpenGL Mathematics
# ─────────────────────────────────────────────────────────────────────────────
echo -e "${YELLOW}[1/6]${NC} GLM (OpenGL Mathematics)..."
if [ -d "glm" ]; then
    echo -e "  ${GREEN}✓${NC} Already exists"
else
    echo -e "  Cloning from GitHub..."
    git clone --depth 1 --branch 0.9.9.8 https://github.com/g-truc/glm.git
    echo -e "  ${GREEN}✓${NC} Done"
fi

# ─────────────────────────────────────────────────────────────────────────────
# zstd - Fast Compression
# ─────────────────────────────────────────────────────────────────────────────
echo -e "${YELLOW}[2/6]${NC} zstd (Compression)..."
if [ -d "zstd" ]; then
    echo -e "  ${GREEN}✓${NC} Already exists"
else
    echo -e "  Cloning from GitHub..."
    git clone --depth 1 --branch v1.5.5 https://github.com/facebook/zstd.git
    echo -e "  ${GREEN}✓${NC} Done"
fi

# ─────────────────────────────────────────────────────────────────────────────
# GLFW - Window/Input
# ─────────────────────────────────────────────────────────────────────────────
echo -e "${YELLOW}[3/6]${NC} GLFW (Windowing)..."
if [ -d "glfw" ]; then
    echo -e "  ${GREEN}✓${NC} Already exists"
else
    echo -e "  Cloning from GitHub..."
    git clone --depth 1 --branch 3.4 https://github.com/glfw/glfw.git
    echo -e "  ${GREEN}✓${NC} Done"
fi

# ─────────────────────────────────────────────────────────────────────────────
# stb - Single-file Libraries
# ─────────────────────────────────────────────────────────────────────────────
echo -e "${YELLOW}[4/6]${NC} stb (Image Loading)..."
if [ -f "stb/stb_image.h" ]; then
    echo -e "  ${GREEN}✓${NC} Already exists"
else
    echo -e "  Downloading header files..."
    mkdir -p stb
    curl -sL -o stb/stb_image.h \
        "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"
    curl -sL -o stb/stb_image_write.h \
        "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h"
    echo -e "  ${GREEN}✓${NC} Done"
fi

# ─────────────────────────────────────────────────────────────────────────────
# wgpu-native - WebGPU Implementation
# ─────────────────────────────────────────────────────────────────────────────
echo -e "${YELLOW}[5/6]${NC} wgpu-native (WebGPU)..."
if [ -d "wgpu-native" ]; then
    echo -e "  ${GREEN}✓${NC} Already exists"
else
    echo -e "  Downloading release v22.1.0.5..."
    mkdir -p wgpu-native

    URL="https://github.com/gfx-rs/wgpu-native/releases/download/v22.1.0.5/wgpu-linux-x86_64-release.zip"

    curl -L -o wgpu.zip "$URL"
    unzip -q wgpu.zip -d wgpu-native/dist
    rm wgpu.zip

    echo -e "  ${GREEN}✓${NC} Done"
fi

# ─────────────────────────────────────────────────────────────────────────────
# X11 Headers - For hermetic build
# ─────────────────────────────────────────────────────────────────────────────
echo -e "${YELLOW}[6/6]${NC} X11 Development Headers..."
X11_DIR="x11_headers"

if [ -d "$X11_DIR/include/X11" ]; then
    echo -e "  ${GREEN}✓${NC} Already exists"
else
    echo -e "  Fetching X11 development headers (from Ubuntu repositories)..."
    mkdir -p "$X11_DIR"
    mkdir -p "$X11_DIR/temp"

    cd "$X11_DIR/temp"

    # Using versions detected.
    # Note: Modern debs use ZSTD compression. System might lack zstd binary.
    # If so, we can use the `zstd` we just cloned in step 2 to decompress!
    # Or just use python/perl if available. Or just hope the system has it.
    # Since `tar --zstd` failed, we need a workaround.
    # We can compile the `zstd` we fetched!

    # Download debs
    wget -q "http://archive.ubuntu.com/ubuntu/pool/main/libx/libx11/libx11-dev_1.8.12-1build1_amd64.deb"
    wget -q "http://archive.ubuntu.com/ubuntu/pool/main/libx/libxcursor/libxcursor-dev_1.2.3-1_amd64.deb"
    wget -q "http://archive.ubuntu.com/ubuntu/pool/main/libx/libxrandr/libxrandr-dev_1.5.4-1_amd64.deb"
    wget -q "http://archive.ubuntu.com/ubuntu/pool/main/libx/libxinerama/libxinerama-dev_1.1.4-3build2_amd64.deb"
    wget -q "http://archive.ubuntu.com/ubuntu/pool/main/libx/libxi/libxi-dev_1.8.2-1_amd64.deb"
    wget -q "http://archive.ubuntu.com/ubuntu/pool/main/x/xorgproto/x11proto-dev_2024.1-1_all.deb"
    wget -q "http://archive.ubuntu.com/ubuntu/pool/main/libx/libxext/libxext-dev_1.3.4-1build3_amd64.deb"
    wget -q "http://archive.ubuntu.com/ubuntu/pool/main/libx/libxfixes/libxfixes-dev_6.0.0-2build2_amd64.deb"

    # Compile zstd if needed? No, that's too slow/complex.
    # Python's `tarfile` module supports xz/gz/bz2 but maybe not zstd in older versions.
    # Ubuntu 24.04 `python3` definitely supports it if libzstd is installed.
    # But wait, `zstd` command was missing.

    # Workaround: Use python to decompress if possible?
    # Or just tell user they need `zstd` installed?
    # But this is "hermetic".

    # We have `third_party/zstd` source code!
    # We can build a minimal zstd binary using gcc quickly.

    if ! command -v zstd &> /dev/null; then
        echo "  Building temporary zstd from source..."
        cd "$THIRD_PARTY_DIR/zstd/lib"
        # Minimal single-file build or just link enough to get zstd cli?
        # Actually `programs/zstd.c` is in the repo but not downloaded?
        # fetch_deps.sh cloned the repo!
        # Let's verify what we have.
        # We cloned zstd repo.
        cd "$THIRD_PARTY_DIR/zstd"
        make -j4 zstd > /dev/null 2>&1
        ZSTD_BIN="$PWD/zstd"
        cd "$THIRD_PARTY_DIR/$X11_DIR/temp"
    else
        ZSTD_BIN="zstd"
    fi

    # Extract all .deb files
    for deb in *.deb; do
        ar x "$deb"
        if [ -f data.tar.zst ]; then
            # Decompress zst to tar using our built zstd or system zstd
            "$ZSTD_BIN" -d data.tar.zst -o data.tar
            tar -xf data.tar
            rm data.tar data.tar.zst
        elif [ -f data.tar.xz ]; then
            tar -xf data.tar.xz
            rm data.tar.xz
        fi
        rm "$deb"
    done

    # Move headers to clean location
    mkdir -p ../include
    cp -r usr/include/* ../include/

    # Cleanup
    cd ..
    rm -rf temp
    cd ..

    echo -e "  ${GREEN}✓${NC} Done"
fi

# ─────────────────────────────────────────────────────────────────────────────
# Google Test - Testing Framework
# ─────────────────────────────────────────────────────────────────────────────
echo -e "${YELLOW}[Bonus]${NC} Google Test (Testing)..."
if [ -d "googletest" ]; then
    echo -e "  ${GREEN}✓${NC} Already exists"
else
    echo -e "  Cloning from GitHub..."
    git clone --depth 1 --branch v1.14.0 https://github.com/google/googletest.git
    echo -e "  ${GREEN}✓${NC} Done"
fi

echo ""
echo -e "${GREEN}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}  All dependencies fetched successfully!${NC}"
echo -e "${GREEN}═══════════════════════════════════════════════════════════════${NC}"
echo ""
echo -e "${BLUE}Next steps:${NC}"
echo "  1. Build voxy:"
echo "     bazel build //:voxy_native"
echo ""
