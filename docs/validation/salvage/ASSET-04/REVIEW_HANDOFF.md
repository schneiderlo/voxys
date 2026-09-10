# Independent ASSET-04 review handoff

This file requests a bounded independent technical review; it does not record
one. Read AGENTS.md, README.md, GAME_IMPLEMENTATION_TODO.md and ASSET-04/design.md
first. The game direction is build machines, explore and salvage. The owner
rejected the primitive cove's visual quality. This task proves the first asset
pipeline, while LOOK-01 separately requires a convincing in-engine cove.

Current review subject: staged pontoon v2-rc01, authored hierarchy stand v1-r02,
and the shared strict registry/bundle/prefab/GPU ownership/application path.
The pontoon part is namespace 766f7879732d73616c766167652d7631, counter 3,
version 2. Prototype v1 remains separate. Candidate manifest:
`5cb23035a14272257af22ae4af47e7fcd863b8b310186346acbf7915fba8818e`.
The five installed inspection registries select this exact candidate. It has
not been published as an independently accepted immutable v2 bundle.

## Authoritative starting points

- `data/salvage/pontoon/release-candidates/v2-rc01/README.md` and `provenance.json`:
  editable source, maps, GLBs, cooked payloads, previews, frozen recipes and hashes.
- `design.md`: part/frame/socket/mass/volume/LOD contract and full acceptance scope.
- `runtime-design.md`: bounded strict admission and asynchronous GPU ownership.
- `release-runtime.md` and its `integration/summary.json`: current exact-manifest
  native/browser admission, actual tests, views, lifecycle, device loss and inputs.
- `materials.md`: actual native/browser texture/normal/PBR diagnostic readbacks.
- `assembly.md`, `rotations.md`: validated welds, prototype-beam limitation and
  all 24 actual rendered proper rotations.
- `hierarchy.md`, `hierarchy-bounds.md`: independent pre-export Blender oracle,
  composed shear/nonuniform transforms, shared mesh instances, winding and bounds.
- `native-motion.md`, `sectors.md`: original native/browser moving frames, traces,
  videos and actual sector transitions. Inspect moving evidence yourself; root's
  sampled-frame inspection is not an independent moving-image review.
- `integration/report.md`: historical integration and preserved failures; use
  each checkpoint's exact source hashes rather than merging different runs into
  a fictional single final test run.

All paths above, unless beginning with data/, are relative to this directory.
The full original captures remain beside their reports. Prior r04 and current
rc01 runtime payloads are byte-identical; cook manifests differ in tool identity.
Old captures establish geometry/shading details. The final runtime record proves
the current selected manifest actually admits and renders. Verify that chain.

## Required independent decisions

- [ ] Inspect actual generator/spec, editable/source outputs, strict converter
  profile and cooker identities. Check original procedural-map provenance,
  source/sidecar hashes and reproducibility claims. Distinguish identical
  payload/pixel data from differing Blender/PNG container bytes.
- [ ] Independently assess units, one basis conversion, 1×.96×4 m body, .18 m
  pegs, engaged .96 m spacing, socket keys, all-rotation fit, nine collision and
  flotation boxes, 120 kg mass, COM/inertia and documented volume approximation.
- [ ] Review full composed node transforms, actual shared geometry, triangle
  winding, inverse-transpose normals, render bounds and texture/color-space/PBR
  evidence. Inspect actual images, not only green runner summaries.
- [ ] Review bounded file/schema/mesh/texture/instance/draw admission, rejection
  preservation, exact content IDs/versions, private inspection BuildModel and
  lack of authority/inventory mutation.
- [ ] Review candidate/active/retiring owner lifetimes, error scopes, actual queue
  completion, unresolved encode tickets, resize, Reset, drained Leave and device
  loss. Native forwarded-loss evidence is distinct from real browser destruction.
- [ ] Inspect native/browser moving evidence for LOD/sector continuity and stable
  residency. Capture overhead is not game FPS; GPU reservation is not total RAM.
- [ ] Check both build-system and browser dependencies plus the current manifest
  selection. Verify raw evidence/source hashes and identify any stale or missing
  final-code coverage that matters to acceptance.
- [ ] Record independent findings with concrete file/line or artifact references,
  severity and required corrections. Explicitly accept or reject ASSET-04's
  technical pipeline; do not approve LOOK-01, physical boats or AAA quality from
  this dry inspector. Do not check the parent task or create a gate commit until
  the root integrates findings and all stated requirements really pass.

Known scope limits: crossbeams still use v1 boxes without modeled socket wells;
actual pontoon-on-pontoon nesting is separately shown. The hierarchy stand is
an intentionally artificial diagnostic. Water/opaque composition and the cove's
art remain unfinished. Recovery after real browser device loss is safe stop and
reload, not live restoration. Final publication is pending this independent
review. Earlier worker reviews predate substantial integration work and do not
constitute acceptance of the final combined pipeline.
