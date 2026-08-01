#!/usr/bin/env bash
# Usage:
#   ./scripts/screenshot_tour.sh [output_dir] [view_count] [width] [height]

set -euo pipefail

OUT_DIR=${1:-"screenshots"}
VIEW_COUNT=${2:-9}
WIDTH=${3:-3440}
HEIGHT=${4:-1440}

for VALUE_NAME in VIEW_COUNT WIDTH HEIGHT; do
    VALUE=${!VALUE_NAME}
    if ! [[ "$VALUE" =~ ^[1-9][0-9]*$ ]]; then
        echo "${VALUE_NAME,,} must be a positive integer" >&2
        exit 2
    fi
done

mkdir -p "$OUT_DIR"

echo "Building voxy..."
bazel build //:voxy_native

BINARY="bazel-bin/voxy_native"
if [ ! -f "$BINARY" ]; then
    BINARY="build/voxy_native"
    if [ ! -f "$BINARY" ]; then
        echo "Could not find the built voxy binary." >&2
        exit 1
    fi
fi

echo "Capturing $VIEW_COUNT viewpoints at ${WIDTH}x${HEIGHT}..."
"$BINARY" \
    --width "$WIDTH" \
    --height "$HEIGHT" \
    --screenshot-tour "$VIEW_COUNT" \
    --screenshot-dir "$OUT_DIR" \
    --screenshot-frames 60

python3 - "$OUT_DIR" "$VIEW_COUNT" "$WIDTH" "$HEIGHT" <<'PY'
from pathlib import Path
import struct
import sys

directory = Path(sys.argv[1])
count, expected_width, expected_height = map(int, sys.argv[2:])
png_signature = b"\x89PNG\r\n\x1a\n"

for index in range(count):
    path = directory / f"view_{index}.png"
    try:
        with path.open("rb") as stream:
            header = stream.read(24)
    except OSError as error:
        raise SystemExit(f"missing screenshot {path}: {error}")
    if (
        len(header) != 24
        or header[:8] != png_signature
        or header[12:16] != b"IHDR"
    ):
        raise SystemExit(f"invalid PNG header: {path}")
    width, height = struct.unpack(">II", header[16:24])
    if (width, height) != (expected_width, expected_height):
        raise SystemExit(
            f"wrong screenshot size for {path}: "
            f"{width}x{height}, expected "
            f"{expected_width}x{expected_height}"
        )
PY

echo "Screenshot tour complete. Images saved to $OUT_DIR/"
