#!/bin/bash
# Usage: ./scripts/screenshot_tour.sh [output_dir]

OUT_DIR=${1:-"screenshots"}
mkdir -p "$OUT_DIR"

echo "Building voxy..."
# Build native binary first
bazel build //:voxy_native

# Check if build was successful
if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

BINARY="bazel-bin/voxy_native"
if [ ! -f "$BINARY" ]; then
    # Fallback to CMake build location if bazel is not used
    BINARY="build/voxy_native"
    if [ ! -f "$BINARY" ]; then
        echo "Could not find voxy binary. Please build it first."
        exit 1
    fi
fi

# Capture all viewpoints in a single run (faster: no reload between shots)
# Update NUM_TARGETS when adding new teleport targets
NUM_TARGETS=6

echo "Capturing $NUM_TARGETS viewpoints in one run..."
"$BINARY" --screenshot-tour $NUM_TARGETS --screenshot-dir "$OUT_DIR" --screenshot-frames 60

echo "Screenshot tour complete. Images saved to $OUT_DIR/"
