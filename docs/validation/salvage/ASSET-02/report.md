# ASSET-02 — gameplay sidecars and bound offline cooking

**Standalone implementation passes 9 C++ sanitizer cases and 20 Python
integration cases using the actual converter.** Independent review, including
the final URI correction, passed. Root subsequently completed normal Bazel/CMake
registration and verified 9 parser + 20 real-converter cases in each, plus all
9 parser cases in each combined suite. See [integrated acceptance](integration/report.md).
The worker did not change a checkbox or commit; root recorded completion after
these checks. No GPU test or runtime rendering was performed.

Executor: `lego_gameplay`, 2026-09-07. G00 base at report time:
`7f28fabfabf3d63726f6cfa1001c3ce1ed557911`. These ASSET-02 additions are outside
that checkpoint. Existing authoring scripts, probe assets and shared build files
were preserved.

## Implemented boundary

The [schema and usage guide](../../../../tools/salvage_assets/GAMEPLAY_SIDECAR.md)
is self-contained. The [complete fixture](../../../../tools/salvage_assets/fixtures/probe.gameplay.json)
adds explicit, stable IDs and typed canonical gameplay data beside the unchanged
ASSET-01 GLB. It is a diagnostic block with simplified authored physical proxies,
not approved game art or a shipped catalog part.

- C++ parses closed schema-1 JSON with independent byte/depth/record limits.
  Unknown fields, malformed IDs/units/frames, duplicate IDs, invalid metadata
  and missing cooked assets fail before publishing a parsed result.
- Physical, socket, module, cost, material and visual invariants come from
  `PartCatalog::create`, including mass/inertia plausibility and disjoint
  buoyancy. No parallel Python physics validator was introduced.
- Durable socket/collision/volume/LOD/tool-anchor IDs are authored u64 decimal
  strings, independent of array positions. Exact content namespace/version is
  retained. Normalization preserves all accepted fields and sorts by numeric ID.
- Each LOD binds a basename GLB to its exact byte length, SHA-256 and explicit
  exported-glTF-to-canonical rotation. Source buffer/image dependencies must be
  embedded; external filesystem/network URIs fail even when the GLB hash matches.
- The Python wrapper snapshots the validated input, invokes the actual existing
  `gltf_vmesh_tool`, validates the resulting VMESH and sidecar through C++, and
  publishes one complete directory. Failed conversion, invalid VMESH/metadata,
  stale binding and publication conflicts leave no new bundle.
- The output manifest binds the original and normalized sidecar, source GLBs,
  actual converter/validator executables and exact cooked VMESH bytes. An
  existing output file or directory, including an empty directory, is never
  replaced. Linux no-replace rename was exercised with a destination created
  between the precheck and publication primitive.

## Coordinate and runtime limits

VMESH mesh-local vertices and node hierarchy remain unchanged. The explicit
remaining basis is recorded per LOD and applied by the offline DATA-01 helper;
probe rotation 12 maps exported forward/right diagnostic points as expected.
Canonical sockets, collision/buoyancy volumes and tool anchors must not receive
that basis again. The authoring probe's original raw-Blender export conversion
was not reapplied.

The cooker does **not** evaluate the whole runtime prefab, bake canonical VMESH,
register engine assets or demonstrate visual/socket fit in engine. MeshPath still
ignores stored node transforms. The ASSET-04 adapter must evaluate the hierarchy
and apply part placement × part rotation × per-LOD basis × node transform.
LOOK-01 prework has been coordinated to preserve this boundary.

ASSET-03 still owns the supported-glTF profile and the known existing matrix/TRS,
attribute-count and unsupported-feature issues. This wrapper's source-digest and
container checks are not a claim that arbitrary models safely preserve every
appearance/function. The actual integration cases use the trusted ASSET-01
probe and bounded malformed fixtures. No arbitrary external model was fetched.

## Actual commands and results

Compiler: GCC 15.2.0. Python: 3.14.4. Linux x86_64, kernel 7.0.0-31, glibc 2.43.
The standalone build uses existing GoogleTest archives and the existing JSON
header as a system include. It is sequential, with one compiler process at a
time, and applies the repository's complete strict GCC warning profile with
`-Werror`, `-O1`, undefined-behavior/float-cast-overflow sanitizers and immediate
sanitizer failure. No warning was suppressed.

From the repository root:

```sh
SALVAGE_CXX=/nix/store/788mx070y81zjlg5ipcl0cra3afviw9k-gcc-wrapper-15.2.0/bin/g++ \
  bash tools/salvage_assets/build_sidecar_standalone.sh \
  > docs/validation/salvage/ASSET-02/compile-standalone.log 2>&1
/tmp/salvage-sidecar-build/gameplay_sidecar_tests \
  > docs/validation/salvage/ASSET-02/tests-cpp.log 2>&1
python3 tools/salvage_assets/test_cook_gameplay_asset.py \
  > docs/validation/salvage/ASSET-02/tests-python.log 2>&1
```

[Compile output](compile-standalone.log): exit 0, empty/no diagnostics.
[C++ output](tests-cpp.log): 9/9 cases pass.
[Python output](tests-python.log): 20/20 cases pass. The suite requires actual
converter/validator executables and fails if missing; it does not skip them.

Coverage includes canonical lossless IDs above 2⁵³, normalized-field preservation
and idempotence, numeric ordering, explicit basis math, shared mass/inertia/
collision/buoyancy/module rejection, malformed/unknown fields, duplicate IDs/keys,
byte/depth/count limits, independently bounded direct C++ CLI input, missing
assets, stale digest/size, path escape and source symlinks, external dependency
URIs, partial converter failure, successful-exit garbage VMESH, empty real-converter
output, malformed draw ranges/indices, nonblocking FIFO rejection, failed validator,
existing-output preservation and atomic no-replace conflict.

A real GLB-node-reordering test changes node order and scene references, proves
the stale sidecar fails, explicitly updates the source binding, then invokes the
real converter again. The cooked VMESH changes; the complete part/tool-anchor
metadata and durable LOD ID remain unchanged. This proves that gameplay IDs are
not copied from the source node ordering; it does not validate a runtime loader.

Independent review found two real defects in the first passing implementation:
Python opened a FIFO before checking its type, allowing an indefinite block;
and the actual converter could produce an empty, parseable VMESH that was
accepted as a visual. Linux input opens now include `O_NONBLOCK`; bounded
subprocess tests cover FIFO sidecars and sources. The C++ resolver now checks
nonempty geometry/materials/logical meshes/submeshes, finite vertex values,
nonzero normals, valid indices and usable triangle ranges after parsing.
Actual empty-GLB and malformed-draw-output integration tests protect that boundary.
These checks do not claim the full ASSET-03 importer profile is implemented.

The first compile of the new drawable check failed under `-Wdouble-promotion`;
[diagnostic](compile-drawable-first.log) is retained. An explicit conversion
fixed it without warning suppression; the final strict rebuild passes. Earlier
passing runs are retained as `tests-cpp-first.log`, `tests-python-first.log`
(15 cases), `tests-python-before-fifo-fix.log` (16), and
`tests-python-before-drawable-fix.log` (17). They are historical coverage, not
proof of the later fixes. The 19-case drawable-fix run is preserved as `tests-python-before-uri-fix.log`.

Final source investigation also found that TinyGLTF treats unknown `data:`
prefixes as external filenames. The wrapper now accepts only four recognized
binary/glTF-buffer/PNG/JPEG base64 prefixes and validates a nonempty base64
payload before conversion. The twentieth Python case covers unknown prefixes,
malformed base64 and empty payloads. Current [tests-python.log](tests-python.log)
contains all 20 passing cases. This further limits external dependency access;
it does not replace the remaining supported-glTF profile work.

## Retained actual cook

Executed the documented command with output
`docs/validation/salvage/ASSET-02/cooked-probe-final`, then repeated with output
`/tmp/salvage-asset02-final-repeat`. Both commands returned 0; their actual stdout is
[cook-probe-final.log](cook-probe-final.log) and [cook-repeat-final.log](cook-repeat-final.log).
All three output files are byte-identical between the two runs. A third cook
using the final URI-checking wrapper at `/tmp/salvage-asset02-uri-repeat` also
returned 0 and produced these same bytes; see [cook-uri-repeat.log](cook-uri-repeat.log):

| Output | SHA-256 |
|---|---|
| [Normalized gameplay metadata](cooked-probe-final/gameplay.json) | `d023d402e4747f9ed69e78d9b5b1e1e2e330f972b2c87374ae7af6b4565c5f5e` |
| [Cook manifest](cooked-probe-final/cook-manifest.json) | `2a77ea227a8a20994070c92759cedd98046c99a9b91762711d14edd47f253e5e` |
| [VMESH](cooked-probe-final/lod-9007199254740997.vmesh) | `4444fd14cfc967b46f3dcbe1079c04f1d0329ed890754ce98f367e71c1c36689` |

The original `cooked-probe/` and `*-before-review.json` manifests remain as
pre-correction evidence. The final cook has the same metadata/VMESH bytes, but
a new cook-manifest digest because the validator executable changed.

The VMESH is 145,557 bytes. The unchanged source probe GLB is 102,836 bytes with
SHA `e2e4f6a7b0d95ac7eff48e2cf6c391c3605f288629b70e7d3d0009fc1735b103`.
[Source hashes](source-hashes.json), [tested executable hashes](artifacts.json)
and [machine-readable result](summary.json) bind the actual inspected sources
and outcomes. No screenshot/render or timing acceptance is claimed.

## Remaining integration and limits

Root completed shared CMake/Bazel registration as recorded above. The CLI needs
`gameplay_sidecar.cpp` + `gameplay_sidecar_main.cpp`, existing construction types/
catalog, `src/moto/vmesh_io.cpp`, `src`/`tools` include roots and the existing
`third_party/tinygltf/json.hpp` include. The C++ test needs the parser, shared
construction types/catalog and GoogleTest. Python integration needs real
converter and validator executable paths. Existing source/compiler flags must
remain enabled. No renderer or GPU library is needed by these standalone units.

The [independent review](review.md) and its follow-up identify precisely which
source snapshots passed. Root's later optimized/shared validation and its
runfiles change have separate identities in the integration report. Windows publication has an untested standard rename
path; other unsupported hosts/filesystems fail rather than use a replacing
rename. Hard crashes can leave private staging directories, and power-loss
fsync durability is not promised. At most eight bounded LOD cooks run sequentially;
resource limits are offline safeguards, not runtime performance evidence.

Published directories are immutable through this tool, but there is no global
content registry preventing separate directories from claiming the same content
key/version. Future registry/runtime work must verify exact cooked/normalized
identities, reject conflicting versions, and implement the hierarchy adapter.
No gameplay inventory, authority, socket physics, mesh collision derivation,
shipping asset kit, final appearance or release acceptance is implied.
