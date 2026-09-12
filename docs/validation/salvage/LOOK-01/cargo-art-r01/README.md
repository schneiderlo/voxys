# Cove generator and cradle — D44

**Both affected fixture checks and final native/WASM builds passed**, following offline authoring, both strict cooks and independent review. Both native and browser continuations passed with existing saves and nine presentations. Root selected the reviewed candidate in catalog `cove-workshop-r06.json`. D39–D44 form one publication. The earlier unfinished mandatory suite was canceled after 520.422 seconds with exit 8 and `voxy_tests` reporting `NO STATUS`; it is not a pass. The [joint D39–D44 required result](../../checkpoints/driven-cove-r01/checks/summary.json) passed: **2,096 native cases passed, 3 skipped and 4 disabled**; terrain import **10 passed / 1 skipped** from cache. Bazel elapsed **1,132.896 seconds**; native test execution **1,125.548 seconds**. Publication remains pending. The [joint checkpoint](../../checkpoints/2026-09-12-driven-cove.md) owns publication.

The generator and cargo cradle now share the Cove's molded cream, teal and orange styling. The generator has clear control accents and roof studs; the cradle has cream runners, teal outer panels and orange ends. Existing lift eyes, keyed seats, mounting openings, physics and saves remain unchanged. No screenshots or image generation were used.

| Part | Vertices, near / middle / far | Triangles, near / middle / far | Draws per LOD |
|---|---:|---:|---:|
| Generator | 2924 / 1292 / 716 | 5632 / 2408 / 512 | 5 |
| Cradle | 1620 / 734 / 564 | 2932 / 1278 / 400 | 4 |

The [installed recipe](../../../../../tools/salvage_assets/author_cove_cargo_art.py) reuses the functional geometry and the existing molded material palette. All colors are solid linear PBR factors derived from the pinned sRGB palette; there are no texture images, UVs or tangents. Recipe/helper hashes are in both [generator](provenance/generator.json) and [cradle](provenance/cradle.json) provenance records. The source `.blend`, GLBs and cooked packages live in `data/salvage/toy-art/r05/{generator,cradle}`.

## Preserved fit and clearance

Canonical metadata outside presentation LOD bindings is exactly equal to the original r09 assets: physical part keys, mass, collision, buoyancy, costs, footprints, sockets and module definitions. LOD IDs `1/2/3`, thresholds `200/60/0` pixels and render-to-canonical rotation `12` remain exact. Visual counters `1001–1003` and `1101–1103` are unique; the catalog appends two presentations to the existing seven.

The independent review found every original cooked vertex position retained at 1 μm comparison precision. Generator lift-eye/mount/pin and cradle floor/pedestal/keyed housing retain the inherited geometry. New generator detail above canonical Y `0.465 m` stays outside central X `[-0.30, 0.30] m`; the eye remains centered at `(0, 0.52, 0) m`, nominal inner radius `0.055 m`. New cradle detail stays outside the open interior X `(-0.76, 0.76) m`.

Actual cooked bounds, in canonical metres, are:

| Part | Minimum XYZ | Maximum XYZ | Largest growth from original |
|---|---|---|---:|
| Generator | `(-.875, -.820, -.775)` | `(.875, .649, .775)` | 9 mm upward |
| Cradle | `(-1.012, -.320, -1.012)` | `(1.012, .330, 1.012)` | 12 mm outward; 10 mm upward |

All three LODs fit the existing envelope plus 20 mm. These local clearances rotate and translate with the existing rigid part placement; they are not fixed world coordinates for moving cargo or boats. In the original authored Cove layout, the static dock generator is centered at `(6, 1.92, -53.5) m`, rotation 0: its visual bounds are X `[5.125, 6.875]`, Y `[1.100, 2.569]`, Z `[-54.275, -52.725]`. Its collision X `[5.1, 6.9]` and Z `[-54.3, -52.7]` stay unchanged. The dock lane at X `4.5 m` and its player-clearance contract therefore retain their existing lateral space.

The authored boat cradle is centered at `(1.5, 1.28, -55) m`, rotation 0; its new visible bounds are X `[.488, 2.512]`, Y `[.960, 1.610]`, Z `[-56.012, -53.988]`. The recoverable generator starts at `(-4.5, -3, -58) m`; its later position follows the observed cargo body. These are initial registry transforms, not a claim about a later live simulation pose.

## Bounded graphics storage

The actual cooked header/material accounting replaces **1,746,056 bytes** with **724,872 bytes**, reducing requested storage by **1,021,184 bytes**. No image texture ownership is added.

- Full Cove, including existing dock markings: **9,731,960 bytes**, observed in every native and browser continuation stage.
- Existing fixture case, excluding its 7,760 dock bytes: expected **9,724,200 bytes**.
- Owner cap remains **16,777,216 bytes**; unique uploads remain 36. Existing instance/draw caps and 64-brick builder check remain in force.

The actual GPU owner case now confirms **9,724,200 bytes** without dock markings, matching its requested charge exactly. It submits 93 color draws for the scene and **160 draws / 92 placements with 64 large bricks and three palette previews**, within the unchanged caps. Both continuations confirm the full-app **9,731,960-byte** charge including dock markings. [Candidate report](candidate-report.json) records per-LOD counts and hashes; [payload manifest](payload-manifest.json) lists the exact 23 asset/recipe files. [Applied-payload check](checks/applied-payload-r01.json) proves root installed those exact reviewed bytes and the proposed catalog.

## Recorded checks

| Check | Result |
|---|---|
| Background Blender authoring | All six exports, two `.blend` saves, manifold/interface/bounds checks and completion markers finished |
| Blender process shutdown | Sandbox PulseAudio `pa_write()` shutdown hung after completion; session interrupted, **exit 130** |
| Generator strict cook | **Exit 0**, all three LODs published |
| Cradle strict cook | **Exit 0**, all three LODs published |
| Independent source/data review | **No actionable finding**; metadata, original positions, openings, hashes, IDs and storage checked |
| Applied assets/recipe/catalog | Exact reviewed bytes confirmed |
| Canonical registry case | **1 passed, 0 skipped, 0.552 seconds** |
| Actual GPU owner/capacity case | **1 passed, 0 skipped, 1.269 seconds**; exact 9,724,200 bytes and 64-brick capacity |
| Combined focused invocation | **Exit 0, 9.523 seconds**; these are the only two selected cases |
| Native and WASM application builds | **Both exit 0**; frozen package hashes retained |
| Native older-save continuation | **4 stages passed in 3.542 seconds**; exact owned design, settings and inventory retained |
| Browser older-save continuation r01 | **Failed after 1.749 seconds**, one paused stage passed; driver used a transient render field as a permanent invariant |
| Browser older-save continuation r02 | **4 stages passed in 2.371 seconds**, exact saved blueprint/stock and later submitted/completed work |
| Browser r02 outer startup | **Passed**, empty browser/sample error arrays and no error-level console entries |
| Completed mandatory suite and publication | **Passed** in the [joint D39–D44 required result](../../checkpoints/driven-cove-r01/checks/summary.json); publication pending |

The Blender interruption is retained in [author-r01.log](checks/author-r01.log); it is not represented as a clean process exit. Thumbnail generation was disabled. Both [generator](checks/cook-generator-r01.log) and [cradle](checks/cook-cradle-r01.log) cook logs use the existing `salvage-rigid-v1` converter and gameplay validator. The [independent review](handoffs/art-review.md) is source/data evidence, not an app test or artistic approval. The [author handoff](handoffs/author-handoff.md) is the original pre-integration snapshot, including reproducible author/cook commands; its candidate status is historical.

The [focused log](checks/focused-r01.log) and [registry](checks/fixture_registry-r01-test.xml) / [GPU](checks/salvage_asset_fixture-r01-test.xml) XML preserve exact results. [Native](checks/native-build-r01.log) and [WASM](checks/wasm-build-r01.log) build logs accompany the [frozen package manifest](checks/package-r01.json). The native executable and WASM module hashes remain equal to the prior D39–D43 package because D44 changes selected data. The final WASM data package is **88,043,106 bytes**, SHA256 `a4036c2e3d91b77de399365353a0a9ea5eb7bba2de8545205ca3a7c7efa9cec0`; its generated loader changed accordingly.

## Actual native continuation

[The native report](native/r01/summary.json) records four stages: restore the older save, resume, open the existing design in the workshop, and close it without edits. Each stage confirms nine presentations, an active unchanged physical owner, readable HUD, ready environment and scene shadows, then waits for actual GPU and physics completion. The fixture reports **9,731,960 bytes in every stage**, 95 world draws and 96 workshop draws. Dock markings hide in the workshop. No refit, screenshot or new sailing matrix was performed.

The old save has 11 parts, 17 connections, mass 1035 kg, material 48, machinery 0 and no paid part IDs. Exact serialized owned part/settings and connection hashes remain unchanged. Propeller disabled/reversed settings remain in those same owned bytes. The original source archive is untouched: schema 4, generation 2, tick 1625, 23,849 payload bytes, SHA256 `d73e19d1f0d1a5bc2a7c2426f8740c335d52be2cec78afd0f1e5cf11c42b7919`.

The copied slot's restored archive is a distinct generation 3 payload: 13,924 bytes, SHA256 `a5643c4435e76792aaa2e11e462ec4fe19cfdb9925095e3aa8dda9ec788a2732`. The check compares owned design/inventory/settings exactly; it does **not** claim the complete copied archive remained byte-identical. [Archive identities](native/r01/archive-identities.json) retain both payloads, their different storage-wrapper hashes and matching current/mirror replicas. Process termination was requested, exit −15, no forced kill and no error lines. [Driver](checks/validate_native_cove_art_resume.py) and [process log](native/r01/process.log) are retained.

## Actual browser continuation

The browser reused the real retained profile and storage origin from the passed
D39–D43 mechanism journey, loading world `d00074effd95777b146c7c069ba23997`.
The [passing report](browser/r02/summary.json) records four stages in **2.371
seconds**: restore the older paused save, physical P Resume, physical B Workshop
with an exact read-only blueprint export, and physical B close. F9 used the
existing uncapped option; no performance acceptance is claimed.

Every stage retains build 6, topology revision 2, root key 7, session revision 3,
11 parts, 17 connections, mass 1035 kg, 48 material, zero machinery and no paid
IDs. The exported blueprint equals the earlier saved design byte for byte,
including all part definitions, connections, placements, paint and settings.
That includes the disabled, reversed Propeller at 100% output. No selection
cycling, refit, sailing, purchase, manual save or image was required. Normal
browser restoration can republish its recovered archive/generation; this is
exact owned-design compatibility, not a claim of unchanged storage bytes.

All four stages show **nine presentations and 9,731,960 owned GPU bytes**,
inside the unchanged 16 MiB cap. Each matched requested logical/shadow-status
sample is followed by a strictly later submission under the same current
world/body and logical mode, then actual completion of that later submission
and physics tick:

| Stage | Matched serial | Later submitted serial | Completed serial |
|---|---:|---:|---:|
| Paused restored save | 33 | 34 | 43 |
| Resumed | 70 | 73 | 75 |
| Workshop exact design | 213 | 215 | 221 |
| Closed workshop | 246 | 247 | 250 |

The shadow flag is a per-render observation with no atomic submission tag. The
proof therefore records both points rather than calling the matched flag a
per-submission counter. All identity, ownership, cap, environment, draw-work,
pending-state and blueprint checks remain intact. The [final driver](checks/validate_cove_cargo_compatibility.mjs)
is pinned to SHA-256 `0f8cd58024d8ed563552472e8ea3690e2aa4e8e4ca74356cbccb86b127c0485c`;
[before/after hashes](checks/browser-final-driver-sha256.json), its exact patch,
original syntax evidence and [independent review](handoffs/browser-final-review.md)
are retained. [Outer report](browser/r02/startup.json.gz) and the [raw runner log](checks/browser-continuation-r02.log.gz)
record success, `browserErrors=[]`, `sample.errors=[]`, and no error-level
console entries.

The [first attempt](browser/r01/summary.json) remains **failed**. It passed its
paused restoration stage, then stopped after 1.749 seconds when a completion
poll outside Workshop observed `sceneSunShadows=false`. A failure observation
moments later showed true again; browser/sample error arrays were empty.
The original [outer report](browser/r01/startup.json.gz) and [runner log](checks/browser-continuation-r01.log.gz)
are preserved. Application reads this field from BlitPath, which can reset it
across render, discard and transition updates. Requiring it to remain stable in
every poll was an invalid driver expectation. The raw failure does not identify
the exact boundary at which false was sampled.

The driver correction matches requested mode/status, then proves strictly later
same-mode submitted and completed work. No product or shader fix is claimed.
The first source-only correction and subsequent review strengthening remain in
`checks/browser-candidate-r02.*`; only the final r03 driver was run as browser
attempt r02. [Source-contract review](handoffs/browser-shadow-review-r01.md) and
[continuation handoff](handoffs/browser-runtime-handoff.md) preserve their
pre-run status as historical snapshots. The final [joint D39–D44 required result](../../checkpoints/driven-cove-r01/checks/summary.json) passed; publication
remains pending in the linked checkpoint.

The already passing [motion/dock](../driven-cove-r01/README.md), [native mission](../../UX-01/recovery-guidance-r01/README.md) and [browser objective](../../UX-01/objective-card-r01/README.md) journeys retain their exact earlier package identities. They are not labeled as D44 runtime passes. LOOK-01 and the wider game-production gates remain open.
