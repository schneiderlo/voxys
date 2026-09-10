# LEGO-01: playable brick cove restored

2026-09-09, root, `codex/salvage-implementation`, work after G00 `7f28fab`.
The cove again uses the existing stepped brick terrain, round studs and grouped
brick surface. Player height queries and authored boat/cargo contacts use that
same surface. This completes the scoped LEGO-01 correction; it does not pass
SIM-03, LOOK-01 or any complete game gate. No screenshots were taken.

## What changed

- Removed the water-anchored cove's forced smooth-terrain fallback. Existing
  LEGO routes retain their surface implementation. Read-only scene telemetry
  reports `terrainSurface: "lego"`.
- Added authored exterior-face contacts against brick tops, exposed risers,
  round stud caps and sides. Traversal preserves gaps between pontoons instead
  of filling an aggregate bounding box. It shares the renderer/CPU plate recipe.
- Contact traversal has an 8,192 face/cell budget and bounded candidate storage.
  Unsupported or incomplete evaluation fails explicitly; it never publishes a
  partial contact set or substitutes a primitive hull. Smooth triangle contacts
  remain supported for their existing routes.
- Raised the actual 420 kg generator from local root Y -3.36 m to -3.00 m.
  At the cove's -200 m water datum, the quantized seabed plate is -203.84 m and
  stud caps are -203.66 m. The old root embedded the generator's underside by
  0.34 m; the new authored placement starts 0.02 m above the studs.
- Reproducible cove composition now retains the proven eleven-part, 1,035 kg
  skiff. The unsuccessful twelve-part outboard winch experiment remains an
  explicit `compose_cove.py --outboard-winch` experiment, not the default craft.
  Delivery-zone data remains, but completed delivery gameplay is not claimed.

## Executed verification

Hardware: AMD Radeon 890M / RADV STRIX1, native Vulkan and Chrome 152 WebGPU.
Native tests were built with `bazel build -c opt //tests:voxy_tests` inside the
repository Nix shell; both native and WASM applications built successfully.
The exact source digest list and executed logs are stored alongside this file.

- [Eight GPU contact cases](contacts-native.log): four new LEGO cases cover
  stud support, real pontoon clearance, exposed riser impact, and round studs
  with open diagonal gaps. Four existing triangle/failure cases also pass.
- [Twenty-three cove/registry cases](cove-native.log) pass, including the
  authored cargo's six-second GPU settling check and actual skiff flotation.
- [Two final grounding/walking cases](grounding-walking-native.log) pass after
  extending that settling check: both the actual generator and compiled skiff
  rest on studs. Their lowest hull points are -203.666 m and -203.667 m;
  residual speeds are 0.037 and 0.100 m/s. The skiff test lowers the test water
  below the seabed to exercise grounding, leaving shipping water unchanged.
  The player walks up a plate terrace, jumps, and lands at the stud-cap height.
- [Real browser journey](browser-journey.json): thirteen recorded stages cover
  spawn, walking beside the boat, boarding, helm, hooking, reeling, paying out,
  sailing, release, steering, moving-deck walking, recovery and resize. Leave
  subsequently drains the scene and exits. The actual cable remains intact
  through the tow. The generator stays at its authored X/Z before attachment.
- Ten browser UI lifecycle tests and both shared-source synchronization checks
  pass. No screenshots, direct pose setters or injected success were used.

Browser reproduction:

```sh
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_COVE_TERRAIN=1 \
VOXY_SMOKE_REPORT=/tmp/lego-browser.json \
VOXY_SMOKE_COVE_PLAYER=/tmp/lego-journey \
node scripts/smoke_integrated_wasm.mjs /path/to/staged-web salvage-cove
```

The staged web directory contains `web/` and the matching `voxy_wasm.js`,
`voxy_wasm.wasm` and `voxy_wasm.data`. The verified local package is
`/tmp/voxys-lego-cove-web-r03`, served for the owner at
`http://127.0.0.1:38193/?experience=salvage-cove`.

## Limits and next work

An attempted separate WASM GPU-unit harness refused its hardware adapter
before executing tests. It is not counted as a pass. The actual application
hardware-browser journey above did execute and pass. Earlier cargo ejection
and rope failures led to the corrected contact selection and starting pose;
the final successful journey did not use the optional cable-break branch.

Terrain remains a heightfield with explicit brick/stud geometry; individual
terrain-brick excavation is not implemented. Full character reactions, CCD,
deep-containment coverage, general machine collision acceptance, final art and
performance review remain in their parent tasks. Session-only delivery work
and the experimental outboard rig remain unfinished. Per D21, next prioritize
PLAY-02's player workshop rather than expand salvage scenarios or image reviews.
