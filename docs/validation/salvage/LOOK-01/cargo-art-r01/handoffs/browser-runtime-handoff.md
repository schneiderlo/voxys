# D44 browser continuation: compatible cargo presentations

Prepared source only. No game, browser, GPU, image or build run by this author. Root installs and runs this after final asset admission review as part of the
combined D39–D44 checkpoint. The earlier unfinished required suite was stopped
and retained; one completed suite will run against the final frozen source.
This scratch candidate changes no live files.

Existing resume scripts perform delivery, rescue, rebuilding or further saves,
so they do not provide this narrower check. Install
`runtime-overlay/scripts/validate_cove_cargo_compatibility.mjs` and apply only
`runtime-smoke-dispatch.patch` to the current runner. Matching runner input is
retained under `runtime-base/`; candidate hashes are in
`runtime-driver-sha256.json`. Preserve any later runner edits when applying.

## Actual older state and invocation

The baseline is `build-cove-guidance-r01/browser-startup-r02.json`, the passed
combined browser report. Its mechanism journey physically saved and reloaded
the exact design. The retained profile exists at `/tmp/voxys-startup-ZgT8Up` and
its IndexedDB origin is `http://127.0.0.1:46039`. Resume world
`d00074effd95777b146c7c069ba23997`; use the same profile and port so the browser
reads its actual stored world. Do not copy, patch or synthesize saved resources.
The old durable save is paused outside the workshop at the dock. The prior run
ended after reopening its restored workshop, but that later UI state was not
saved; the stored checkpoint remains the paused state.

Use the established real-GPU/display environment and final package path:

```sh
VOXY_SMOKE_PROFILE=/tmp/voxys-startup-ZgT8Up \
VOXY_SMOKE_PORT=46039 \
VOXY_SMOKE_RESUME_WORLD=d00074effd95777b146c7c069ba23997 \
VOXY_SMOKE_WIDTH=1280 VOXY_SMOKE_HEIGHT=800 VOXY_SMOKE_NO_SCREENSHOT=1 \
VOXY_SMOKE_PRESENTATION_PARTS=9 \
VOXY_SMOKE_CARGO_BASELINE=build-cove-guidance-r01/browser-startup-r02.json \
VOXY_SMOKE_COVE_CARGO_COMPATIBILITY=build-cove-cargo-art-r01/browser-continuation-r01 \
VOXY_SMOKE_REPORT=build-cove-cargo-art-r01/browser-startup-r01.json \
node scripts/smoke_integrated_wasm.mjs FINAL_WEB_PACKAGE salvage-cove
```

Use only this journey flag; do not carry the earlier objective/mechanism/refit
flags into the continuation. The existing runner validates the isolated-profile
marker and preserves caller-supplied profiles. It retains its normal browser,
console, GPU and process-error checks.

## Four stages, 60-second polling deadline after startup

1. Restore the actual paused older world with exactly nine presentations.
   Use physical F9 only if needed, with the read-only uncapped getter.
2. Physical P resumes. Actual `workshop.canOpen` must be true at the existing
   saved position; no movement or exact arbitrary waypoint is used.
3. Physical B opens Workshop. The established read-only blueprint export must
   equal the old saved blueprint byte for byte, covering all parts, connections,
   placements, paint and machinery settings. This includes the disabled,
   reversed Propeller at 100% output. No selection cycling or refit is needed.
4. Physical B closes Workshop. The world, owned design and stock remain intact.

The initial logical world/build/topology/root IDs are bound to the actual saved
baseline, not merely self-consistent inside the new process. Expected build is
6, topology revision 2, root key 7. Every stage checks exact part/mass/paid IDs,
inventory, session revision, build/part/connection/cargo/job counts, stored part
IDs and recovery design digests against that baseline: 11 parts, 1035 kg, no
paid IDs, 48 material, zero machinery, revision 3 and 17 connections. Job and
harbor state also remain unchanged. The source report is pinned by SHA-256.

Runtime body and physics incarnation may differ from the old process; every
capture must keep the same current valid body/incarnation and prove later actual
GPU completion of its submitted frame plus physics completion of its observed
mechanism tick. All four stages require the configured exact presentation count,
a ready lit environment and positive draw work, and an owned fixture at or
below the existing 16 MiB cap. World stages require scene sun shadows; Workshop
uses its existing separate presentation. Uncaptured GPU errors remain forbidden.

The driver uses real keyboard events and read-only observations/export only.
It does not buy parts, Keep/Launch, sail, repaint, reset, rescue, save, reload,
change storage, force a target click or capture an image. A failure retains its
last game state; success retains four completion-proven states and exact exported
blueprint in `summary.json`, leaving the live game running at the dock. The normal browser restore path may republish its recovered archive and advance
the storage generation before this driver starts. That existing behavior is
allowed; the driver adds no manual save action and does not edit storage bytes.

Runtime acceptance and final asset owner byte totals remain pending. This
source candidate makes no compatibility or visual-quality success claim.

Independent read-only review by `presentation_review` found no actionable
control, ownership or completion defect. It confirmed that the browser workshop
uses shadow=false (unlike the separate native path), that initial logical IDs
are bound to the source save, and that exact blueprint/export and real P/B
controls are correctly scoped. The review corrected the wording about storage:
normal restoration itself may republish the archive, so this driver claims no
additional save actions rather than byte-for-byte unchanged storage. The
reviewer ran no browser, GPU or duplicate test.

The first actual continuation is retained as failed at 1.749 seconds after one
captured paused stage. A transient per-render shadow flag was incorrectly used
as an invariant during completion polling. The bounded correction is isolated
under `runtime-shadow-r03/`; it matches the requested logical/rendered mode,
then requires a strictly later same-mode submission and proves its completion.
Both proof points are recorded; the shadow flag has no atomic submission tag.
All ownership, cap, blueprint and game-state checks remain unchanged. See
`runtime-shadow-review-r01.md`. No product correction or second-run pass is
claimed before root supplies the actual result.
