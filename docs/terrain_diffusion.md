# Terrain Diffusion Import

Voxys can use Terrain Diffusion as an offline terrain source.

The flow is:

```text
Terrain Diffusion API -> int16 meters -> Voxys uint16 .r16 -> .ldh
```

This keeps Python and PyTorch out of the renderer and out of the WASM build.

## 1. Start Terrain Diffusion

In a separate checkout, use this repo's uv-managed requirements:

```bash
# From the Voxys repo root.
VOXYS_ROOT="$(pwd)"
git clone https://github.com/xandergos/terrain-diffusion /tmp/terrain-diffusion
cd /tmp/terrain-diffusion
uv run \
  --with-requirements "$VOXYS_ROOT/requirements/terrain-diffusion.txt" \
  python -m terrain_diffusion api xandergos/terrain-diffusion-30m \
  --device cuda \
  --port 8000
```

CPU also works, but first tile generation can be slow.

## 2. Build the Voxys LDH Tool

From this repo:

```bash
bazel build //tools/ldh_tool:ldh_tool
```

This is the preferred compressor path.

If `ldh_tool` is not available, the importer will try a slower Python fallback
that writes LDH directly through the system `libzstd`.

## 3. Import a Tile

Example 4096 square tile:

```bash
uv run python tools/terrain_diffusion_import.py \
  --api-url http://127.0.0.1:8000 \
  --output data/generated/td_seed_1234_4096.ldh \
  --size 4096 \
  --seed 1234 \
  --meters-per-sample 30 \
  --chunk-size 1024 \
  --ldh-tool bazel-bin/tools/ldh_tool/ldh_tool \
  --checksum
```

To force the Python LDH fallback:

```bash
uv run python tools/terrain_diffusion_import.py \
  --output data/generated/td_seed_1234_1024.ldh \
  --size 1024 \
  --seed 1234 \
  --prefer-python-ldh
```

The tool writes:

```text
data/generated/td_seed_1234_4096.ldh
data/generated/td_seed_1234_4096.json
```

The JSON file contains the source seed, source height range, and suggested
`voxy.cfg` terrain values.

`--chunk-size` controls peak import memory. `1024` is conservative. Larger
chunks can be faster if the Terrain Diffusion API and machine memory can handle
larger responses.

## 4. Use the Imported Terrain

Copy the values printed by the import tool into `voxy.cfg`:

```toml
[terrain]
heightmap = "data/generated/td_seed_1234_4096.ldh"
height_scale = 3120.0
cell_scale = 30.0
```

Use the actual values printed by the tool. The numbers above are only examples.

## Encoding

The default encoding is `symmetric-zero`.

That means:

```text
0 meters       -> uint16 midpoint
-height_scale -> 0
+height_scale -> 65535
```

This matches Voxys' current shader mapping and keeps sea level stable.

For special cases, `--encoding range --min-meters A --max-meters B` is also
available, but it needs shader-side care if exact meter reconstruction matters.
