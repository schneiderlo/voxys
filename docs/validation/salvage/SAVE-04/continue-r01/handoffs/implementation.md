# D45 — Continue saved Cove

Scratch only. Root must publish D39–D44 first, then integrate these five files as a separate bounded SAVE-04/UX-01 checkpoint. No C++, save format, physics, asset, config, renderer or build-budget changes. No actual browser run has been performed for this candidate.

## Player behavior

After a successfully confirmed save or resume, the LEGO Landscape/Shore navigation shows one accessible **Continue saved Cove** link. It returns to that world on the same browser profile and origin, paused through the existing resume path. The existing Leave action still waits for C++ removal acknowledgment before navigating. Leaving does not save. The save help also retains the bookmark option.

The link is hidden until valid metadata exists. Its native anchor semantics supply keyboard navigation and Enter activation; it has an existing visible focus style, a 44 px minimum height, and is exempt from the navigation rule that hides other anchors on narrow windows. It initializes only on the two LEGO routes.

## Save authority and failure rules

- Local key: `voxys.cove.last-confirmed-world.v1`. Its value is only a lowercase, nonzero 32-hex world ID. No payload, generation, save URL, name or world-list data is stored here.
- Resume remembers only after `store.publish` commits, the live-owner checks pass, and existing action 4 accepts the exact paired-lineage digest. Manual/automatic save remembers only after durable publication, closed-owner/required action 7 checks and successful saved-address update.
- Pending/failed/unacknowledged/page-closed operations do not replace the prior pointer. Reading or writing localStorage is caught locally; denied access, throwing getters/setters, corrupt/oversized values and absent storage never fail or revoke a valid save.
- The link takes the current page URL, clears query and hash, then sets only `experience=salvage-cove` and the validated `world`. Only HTTP(S) pages are accepted; origin/path remain the current page's. Persisted values cannot supply a destination URL.
- This hint does not enumerate, read, alter or create IndexedDB save resources. A stale pointer reaches the existing missing/damaged/conflicting/busy restore refusal. It never silently starts a new world. Existing storage locking, durable current/mirror transactions and late-owner fencing are unchanged.

## Exact files

`stage-candidates-r01.json` records original and candidate hashes for:

1. `web/cove_saves.js`: private metadata validation/remembering and `installContinue`; two successful-ack hooks.
2. `web/index.html`: hidden native anchor, narrow-window exception and hit area, LEGO-only initialization, concise save help.
3. `scripts/test_cove_saves.mjs`: existing cases retained, nine new metadata/order/refusal/link tests.
4. `scripts/validate_cove_continue.mjs`: new bounded real-control browser journey.
5. `scripts/smoke_integrated_wasm.mjs`: optional dispatch only when `VOXY_SMOKE_COVE_CONTINUE` is set.

The four existing files have exact `base/` copies. Root should compare base hashes before applying and use the localized patch if another owner has touched those files. `candidate.patch` includes the new driver.

## Actual scratch checks

`node overlay/scripts/test_cove_saves.mjs` passed **26 tests, 0 failed, 0 skipped**, 93.77328 ms in `node-tests-r02.log`. The earlier `node --test` invocation emitted only a one-file aggregate pass; its preserved `node-tests-r01.log` is not used as the case count. New driver and optional dispatch pass `node --check`. Tests cover confirmation ordering, retained previous pointers on failures/closure/ack refusal, same-origin canonical URL, invalid/zero/oversized values, unavailable storage access and writes, missing-world refusal and the semantic/mobile link contract. These are controlled Node fixtures, not real browser storage faults.

After integration, run the existing cove save, expedition storage and preview UI Node files once, plus the actual journey below. The native required suite should remain cached because C++ and its data inputs are unchanged, but the normal commit hook must still run. Do not claim a browser or gate pass from these source/unit checks.

## One real Leave → Continue journey

The dispatch requires an existing selected world and the original passing combined report. It validates the old report's exact saved blueprint, part/connection/settings bytes, IDs, stock and logical ownership. It never injects save metadata or game state.

The real successful initial resume must leave the navigation hint pointing at the confirmed world. It may refresh a hint already present after an earlier run; the driver does not claim that it newly created the key. Physical clicks then use Leave and the visible Continue anchor. Landing URL/hit target/semantic anchor/44 px height are checked before the click. The same world must return paused with a fresh observation incarnation. P/B open the unchanged design for read-only blueprint comparison; B/P close and pause without an additional manual save action. Each normal resume still performs its required durable lineage publication. Each game stage waits for a later submitted frame and actual GPU/physics completion bound to that page's exact live body owner. The entire journey is capped at 120 seconds. No haul, refit, camera matrix or screenshots.

Use a new output directory and report path. Preserve the retained profile and original port; confirm no existing process owns that profile or port before root invokes the runner:

```sh
VOXY_SMOKE_PROFILE=/tmp/voxys-startup-ZgT8Up \
VOXY_SMOKE_PORT=46039 \
VOXY_SMOKE_RESUME_WORLD=d00074effd95777b146c7c069ba23997 \
VOXY_SMOKE_COVE_CONTINUE=build-cove-continue-r01/browser-r01 \
VOXY_SMOKE_CONTINUE_BASELINE=build-cove-guidance-r01/browser-startup-r02.json \
VOXY_SMOKE_PRESENTATION_PARTS=9 \
VOXY_SMOKE_REPORT=build-cove-continue-r01/browser-startup-r01.json \
node scripts/smoke_integrated_wasm.mjs <final-web-package> salvage-cove
```

Set the baseline to the exact existing report path if root has renamed it; it must be the actual passing combined report containing `cove_mechanisms`, its blueprint and retained profile/origin. The runner preserves explicitly supplied profiles automatically. Root owns process cleanup and final outer browser/sample/console error checks.
