# Stationary cove opaque/water checkpoint

This record precedes the [playable dock work](../dock-play-r01/README.md).
The cove now seeds separate per-frame RGBA16F linear HDR color and R32F radial
depth from immutable terrain caches, draws authored opaque assets into those
targets, and composes water afterward. Output conversion occurs in the final
composition. Application owns the targets (12 bytes/pixel); fixture Leave
retires model/lighting ownership while framebuffer ownership lasts until
Application teardown. At 1920×1080 the extra targets request 24,883,200 bytes.

`src/render/opaque_scene.*`, `shaders/opaque_scene.wgsl`, MeshPath HDR output,
BlitPath composition and Application frame/discard plumbing implement this
path. Submerged authored-object depth bounds optical travel; a cove-only
specialization keeps shallow-water Fresnel reflection. Legacy routes retain
their previous specialization. Alpha-blended assets and inspection guides are
not admitted to the cove HDR path. Resize and abandoned encoding invalidate
provisional work; scene inputs remain separate from output attachments.

The retained native logs show nine mesh cases and twenty fixture cases passing
after correcting a seed-shader validator failure and a stale fixture-capacity
test. `browser-diagnostics/report.json` passes the shared analytic HDR/depth
test without screenshots. It verifies linear HDR before exposure, nearest-object
depth, immutable terrain, object removal and rejected invalid resources.
`browser-final.json` and `browser-controls-final/summary.json` pass actual
browser startup, two resizes, movement, Reset, Leave and re-entry. Native/WASM
build logs and served package hashes are retained here. No full gate run or
independent acceptance is claimed.

Numeric hull probes found water closer than the object for all thirteen sampled
submerged authored-surface points. They are correctness probes, not performance
measurements or art approval. This directory also retains the prior visual
attempts that triggered the owner's screenshot-loop objection. No new images
were captured or viewed while recording this handoff. In particular,
`browser-final.png` was captured earlier but was not visually reviewed.

The boat is stationary and final appearance remains unapproved. Dynamic
machines/cargo, shadows, dry hull interiors, general transparency, flooding and
underwater medium transitions still require their REND/SIM gates. Do not use
this checkpoint to pass REND-01/02 or G05. Follow decision D20 and continue
playable functionality instead of repeating these views.
