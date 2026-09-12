# Cove structural art and compatible presentation revisions

Status: the scoped structural-art integration is verified on desktop and browser. Both reopen an older eight-brick boat, compare its exact layout/ownership and board/sail with all three visual revisions active. LOOK-01 and all parent gates remain open. No screenshots or rendered images were taken.

Base commit: `dcae5c3cb91642af38aae90915a705d37d365f95` (published to main before this work). Candidate scratch: `build-cove-toy-art-3pon83t5/`. This is subsequent working-tree art work, not a new main publication.

## Change

The installed pontoon, structural beam and dock/deck plate now have original molded cream/teal/orange surfaces, panel divisions, bevels and side details. The pontoon moldings follow its actual taper. All nine LOD models have editable Blender sources and strict cooked manifests in `data/salvage/toy-art/r01/`. Solid PBR factors need no image textures or UV seams. `tools/salvage_assets/author_functional_kit.py` keeps its earlier default UV behavior; the new recipe explicitly opts out for untextured materials.

`LoadedAssetFixture` now retains separate canonical and presentation bundle references. Schema 2 of the additive catalogue names the exact source manifest and visual replacement. Strict admission compares every normalized non-LOD field and each LOD ID, basis and threshold; limits geometry to the old union AABB plus 2 cm; refuses visual-ID reuse and duplicate selection; caps extra encoded and decoded CPU ownership at 16 MiB each. It preserves canonical definitions, layout digest, assembly, collision, sockets, mass, costs and save identity. Existing schema 1 catalogues remain supported. Rendering, GPU ownership and LOD selection use the presentation references. Read-only state reports `assetFixture.presentationParts`.

The ordinary 16 MiB per-generation and 48 MiB total GPU limits are unchanged. Final candidate uses **14,522,880 reserved bytes**, including the environment filter and sun-shadow map, for all 12 catalogued parts and 36 LOD uploads. The actual 25-placement Cove submission uses 69 color draws. These are resource/admission measurements, not frame-rate or visual-quality certification.

## Checks and retained corrections

- CMake: all 13 fixture registry cases pass. The new real Vulkan GPU case admits all parts, publishes, encodes the Cove and retires resources under the existing limits.
- Bazel: **35 cases pass** (13 fixture registry, 22 asset/inspection/GPU) after the strict compiler warning correction. Exact XML/log reports are in `checks/`. No skipped cases in these targets. This is focused verification, not a new full repository-suite or gate run.
- All three optimized packages pass the strict cooker. Their provenance records Blender 5.2.1 LTS, source hashes, closed/outward geometry, normals, LOD counts and bounds.
- Admission rejection cases use rehashed valid packages to verify changed gameplay cost, LOD thresholds, original visual-ID reuse and a translated mesh are refused. Wrong source hash, replacement hash, duplicate selection and traversal also refuse without mutating the original loaded scene.
- First authoring attempt exposed chart distortion on nonplanar n-gons. A triangulation correction passed, but the first cook then caught an accidental 17-byte namespace. Both failures were retained. The final namespace is exactly 16 bytes.
- The first runtime candidate requested **17,027,832 bytes**, exceeding the existing 16 MiB cap. Removing unused UV/tangent chart data for solid materials retained triangle counts and reduced duplicated export vertices; it now requests 14,522,880 bytes. No GPU ceiling was raised. Final authoring is `author-r04`, cooker logs end in `r04`, GPU report is `owner-r03.xml`.
- Bazel's first compilation rejected a nested variable name shadowing the catalogue file provider; the variable was renamed. `bazel-focused-r02.log` is the passing rerun.

## Old-save gameplay acceptance

- **Native:** three actual-control stages pass: restore, exact eight-brick layout inspection, boarding/helm/sailing. The isolated copy retains 18 parts, 1,149 kg, paid IDs 35–42 and 23 material. It reports three presentation parts and 14,522,880 reserved GPU bytes. `native-old-save.json` retains observations. The prior saved world and the original brick-placement observations are in `inputs/`, with hashes. The original input run failed later; its passing continuation is recorded in LEGO-02. This run uses the same earlier saved checkpoint and passes independently.
- **Browser:** hardware WebGPU startup plus two old-save stages pass: exact layout inspection and boarding/helm/sailing. The same counts, IDs, mass and material are retained, with three presentation parts and the same reservation. No uncaptured GPU/browser errors or device loss is reported. `browser-startup.json` and `browser-old-save.json` retain observations. Tested at 1280×800 with Chrome's AMD RDNA 3 hardware adapter. This is not a displayed-frame performance gate.
- Both runtime observations still report `terrainSurface: lego`. No terrain, physical part definitions or saved contents were migrated. The test supplies real input; observations only read game state.
- Both final applications build. `package-hashes.json` identifies the frozen desktop executable and the browser JS/WASM/data. The new preview is served from `build-cove-toy-art-3pon83t5/web-r01` at `http://127.0.0.1:42751/index.html?experience=salvage-cove`, preserving the previous preview origin. The app's open-panel request failed; use that address directly. Earlier preview packages remain available.
- A runner cleanup defect was found during this check: a caller-supplied isolated profile could be deleted unless an additional keep flag was set. The existing profile was copied before cleanup and restored at the same path with all regular-file hashes matching. The runner now always retains a supplied profile, and only disposes of profiles it creates. `checks/browser-runner-executed.mjs` retains the exact earlier runner used for this passing journey; the later cleanup-only correction has syntax/source verification and does not alter the game package or gameplay assertions.

## Reproduction

From the repository root, enter `nix-shell` for build/cook/native execution. Author using a new output directory:

```bash
/snap/blender/current/blender --background --factory-startup --python-exit-code 1 --python tools/salvage_assets/author_cove_toy_art.py -- --output-dir <new-output>
```

For each of `pontoon`, `beam`, `plate`:

```bash
python3 tools/salvage_assets/cook_gameplay_asset.py --sidecar <new-output>/<part>/source/<part>.gameplay.json --sources <new-output>/<part>/source --output <new-output>/<part>/cooked --converter build-native-save-host/bin/gltf_vmesh_tool --validator build-native-save-host/bin/gameplay_sidecar_tool
```

Focused regression checks:

```bash
bazel test //tests:fixture_registry //tests:salvage_asset_fixture --test_output=errors
cmake --build build-native-save-host --target voxy_native -j4
EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j4
```

Actual native continuation command (the executed source slot/report were the matching originals under `build-cove-builder-3bh5dr4t`; these retained copies contain the same tested input):

```bash
DISPLAY=:0 XAUTHORITY=<active-Xwayland-auth-file> python3 scripts/validate_native_cove_bricks.py --binary <built-voxy_native> --output <new-native-report> --storage-root <new-save-copy> --source-slot docs/validation/salvage/LOOK-01/toy-art-r01/inputs/df314a0502c8ef92ad8d9a2419a7a5f7 --resume-report docs/validation/salvage/LOOK-01/toy-art-r01/inputs/native-prior.json --expected-presentation-parts 3
```

Browser continuation used the existing isolated profile `/tmp/vp4yo4yym3/voxys-startup-zqolfI`, original origin port 36769, saved world `53dd831b593bce212c938dd2d1cb2412` and `build-cove-presentation-4yo4yym3/bricks-r16/summary.json`. The final runner preserves supplied profiles automatically. Use a short, disk-backed `TMPDIR` suitable for Chrome socket paths. The executed environment additionally set `DISPLAY=:0`, `XAUTHORITY=/run/user/1000/.mutter-Xwaylandauth.QCJQU3`, `VOXY_TEST_CHROME=/opt/google/chrome/chrome`, `VOXY_SMOKE_GPU=gaming-x11`, `VOXY_SMOKE_WIDTH=1280`, `VOXY_SMOKE_HEIGHT=800`, `VOXY_SMOKE_TIMEOUT_MS=600000`, `VOXY_SMOKE_NO_SCREENSHOT=1` and `VOXY_SMOKE_PRESENTATION_PARTS=3`.

```bash
VOXY_SMOKE_PROFILE=<isolated-saved-profile> VOXY_SMOKE_PORT=36769 VOXY_SMOKE_RESUME_WORLD=53dd831b593bce212c938dd2d1cb2412 VOXY_SMOKE_COVE_BRICKS_RESUME=build-cove-presentation-4yo4yym3/bricks-r16/summary.json VOXY_SMOKE_REPORT=<new-startup-report.json> VOXY_SMOKE_COVE_BRICKS=<new-control-report-dir> node scripts/smoke_integrated_wasm.mjs build-cove-toy-art-3pon83t5/web-r01 salvage-cove
```

On a clean machine without that retained browser profile, omit the four profile/port/world/continuation variables to run the driver's full fresh eight-brick construction, saving, reload and sailing sequence. The executed art acceptance used the older save continuation, not a second full construction journey.

## Limits and next work

 No owner/independent visual approval or full gate is claimed. Existing machinery, terrain/water shadow reception, dry hull composition, wet materials, avatar, native HUD, controller parity and displayed-frame performance remain open. This updates three structural definitions; it is not completion of the game's art direction or a replacement for actual brick construction. No unrelated two-job WIP, old save or old preview is changed.

## Combined checkpoint verification — 2026-09-12

The final combined source passes the required repository suite: 2,067 native
cases pass, three skip and four remain disabled; terrain import has ten passes
and one skip. Both actual-control construction/save/reload journeys pass.
[Checkpoint, source/package identities and full raw check results](../../checkpoints/2026-09-12-playable-cove.md)
record the current result. Earlier pending statements and original identities
above describe their historical stage. This checkpoint does not approve final
art or complete a game gate. The commit containing the checkpoint report is
the publication unit; normal repository hooks remain enabled.
