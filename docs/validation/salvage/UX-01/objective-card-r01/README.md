# Browser next objective card

Prepared 2026-09-12 for implementation item D42. **Source review, 36 CPU/UI
assertion groups and the second actual browser card journey pass.** The card
journey completed five stages in 3.033 seconds. The combined outer browser run also passed with no browser/sample/console
errors, and the following mechanism journey passed 28 stages in 84.663 seconds.
The [final joint required suite](../../checkpoints/driven-cove-r01/checks/summary.json)
also passed. The [joint checkpoint](../../checkpoints/2026-09-12-driven-cove.md)
records publication.
The first missed Accept click remains unexplained and is preserved below.
No screenshots were taken. This is scoped browser guidance acceptance; it does
not complete the broader UX-01 gate or claim that the game is finished.

## What the player sees

One **Next objective** card appears above Workshop and Interact in the Cove
panel. It gives a short instruction and at most one useful action: accept the
recovery job, board, use the helm, hook the generator, open winch controls,
deliver the load or power the harbor. An unavailable action becomes guidance
without a clickable substitute. The card disappears while building and returns
when the workshop closes. Existing paint, building, save and detailed controls
remain available.

Pause, pending work, rescue and saving show waiting instructions. A revoked
expedition shows “Expedition unavailable” and points to save status/reload.
Delivery and harbor power are announced as saved only after their durable state
is confirmed. Opening winch controls opens the existing drawer and focuses Reel;
it does not start a motor. The card fits the existing 352 px panel and wraps its
text and button label. Both actual runs measured the card and its text/action inside both tested
viewports without scrolling. The second also completed the real Accept and
workshop visibility checks.

## Authority, focus and cleanup

The controller reads the existing buttons after their normal permission update.
It promotes only an enabled, unhidden target in an available group. A click
rereads engine observations and requires the same step, target and current
permission before invoking that original handler once. This avoids invoking an
old Accept/Hook action after the job or tow state changes. The card adds no
engine action, gameplay permission, save schema, asset, ABI or ownership change.

An unchanged refresh preserves keyboard focus and does not rewrite live-region
text. A real pending transition clears the action. Cleanup removes the new
listener and hides the card. The existing three-line construction status and
paint controls are unchanged.

## Completed evidence

| Record | Actual result |
|---|---|
| `checks/node-ui-r01.json` | Initial 34 groups pass: 26 existing and eight new. The original invocation's result was recorded; raw stdout was not retained. |
| `checks/node-ui-r02.json` and `.log` | Final 36 groups pass: 26 existing and ten new; no skipped groups. |
| `source-review/ui-semantic-review.md` | Two concrete findings corrected, then independently verified with no remaining actionable finding. |
| `checks/driver-syntax-r01.json` | New browser driver and scratch smoke runner pass Node syntax checks. No driver execution. |
| `source-review/browser-driver-handoff.md` | Independent source review found no actionable issue in real controls, natural layout checks, identity/completion proof or combined-run dispatch. |
| `browser/r01/summary.json` | Two layout stages pass; actual Accept wait fails after 17.73 seconds. Retained failed attempt. |
| `browser/r02/summary.json` | Five real browser stages pass in 3.033 seconds, including Accept and workshop hide/restore. |
| `source-review/input-trace-review.md` | Successful trusted input and one existing-handler forward recorded. Original missed-click cause remains unresolved. |

The first review found a transient hide/disable on each refresh that could steal
focus, and an offer of actions after `session.admissionOpen` became explicitly
false. The final check adds regressions for both and checks unchanged live text.
Only those review-driven changes justified the second focused invocation. The
legacy console aggregate omits an existing recovered-design assertion block;
36 is the count of actual top-level assertion groups, not the sum of those old
console labels. Ten new groups cover permissions, stale clicks, one-action
forwarding, drawer focus, save/pause priority, durable power, hidden workshop,
failure and cleanup, stable keyboard focus and revoked admission.

Candidate handoffs and hashes are retained unchanged as historical snapshots.
Their statements about frozen live files describe the time of handoff, not the
current integration state. They are not final package hashes. Root owns final
source/package identity and joint checkpoint publication.

## Actual browser acceptance and retained first failure

The prepared `scripts/validate_cove_objectives.mjs` uses actual CDP input and
read-only game observations, with a 90-second polling deadline after startup.
It first uses the existing physical F9 toggle if needed and verifies the
read-only uncapped getter. The integrated F9 input-routing fix is required;
this card makes no frame-rate or performance claim.

The five required captured stages are:

1. Initial enabled Accept card at 1280×800, with natural page/panel scroll offsets.
2. The same card at 640×480, with label, title, detail, action and actual text
   rectangles inside the viewport and panel. No forced scrolling or CSS edits.
3. Real card click accepts the job while preserving the exact boat and stock.
4. Physical B opens Workshop and hides both card and action.
5. Physical B closes Workshop and restores the Board objective.

Each captured state requires later completion of its submitted GPU serial and
physics tick under the same world, incarnation, build, topology, root and body
identity. The exact starter remains 11 parts, 1035 kg, no paid part IDs, 48
salvage material and zero machinery, with exactly seven presentation parts.
The driver checks uncaptured GPU errors; the outer runner keeps its existing
browser/runtime error scanner. It records measured DOM labels/bounds, game
observations, completion proof and failure state in `summary.json`.

The objective dispatch runs before the optional mechanism journey so both use
one browser startup. Success leaves the job accepted, the player at the dock,
the workshop closed, the game running and the viewport restored to 1280×800.
It performs no refit, sailing, save, reload, reset or screenshot. The mechanism
journey accepts this unchanged boat with an already accepted job.

Reproduce the focused CPU check from the repository root with:

```sh
node scripts/test_salvage_preview.mjs
```

Add these variables to the established real-GPU smoke environment, using the
actual final web package path and a fresh world/profile:

```sh
VOXY_SMOKE_WIDTH=1280 VOXY_SMOKE_HEIGHT=800 VOXY_SMOKE_NO_SCREENSHOT=1 \
VOXY_SMOKE_PRESENTATION_PARTS=7 \
VOXY_SMOKE_COVE_OBJECTIVES=build-cove-browser-objectives-r01/browser-journey-r01 \
VOXY_SMOKE_COVE_MECHANISMS=build-cove-mechanisms-r01/browser-journey-final \
node scripts/smoke_integrated_wasm.mjs FINAL_WEB_PACKAGE salvage-cove
```

Omit the mechanism flag for the optional standalone card check. Root owns the
actual run and the following mechanism journey.

The first combined attempt, `browser/r01/summary.json`, failed after **17.73
seconds**. Its first two stages passed at 1280×800 and 640×480, including actual
text bounds, natural zero scroll offsets and the unobstructed enabled Accept
button. Both captures have later completed GPU/physics work under the same
world/body. At 640×480 the card occupied x=41–344, y=184.289–346.961; the action
occupied x=57–230.492, y=297.117–333.961, inside the panel clip x=25–360,
y=25–455. These are measured bounds, not screenshot or visual-quality approval.

The single real click was followed by a 15-second wait; the job remained
`available`, session revision stayed zero, and starter ownership/stock remained
unchanged. The card still showed an enabled Accept action. `browser/r01/startup.json.gz`
retains the original outer report: `browserErrors=[]`, `sample.errors=[]`, and
no error-level console entries. The runner log is retained under `checks/`.
The browser attempt remains **failed**, and its following mechanism journey
was not reached.

Read-only source and final-state review found no change in permission or
geometry that explains the failure. This evidence does not establish whether
the physical click was lost or the original action was refused before a state
change. Root ran the same single click again with bounded passive event/target
tracing on the identical frozen web package. No product change, retry within
the driver or programmatic substitute was introduced.

The second attempt, `browser/r02/summary.json`, **passed all five stages in
3.033 seconds**. A trusted objective click reached the correct unchanged button
and forwarded exactly one ordinary DOM click to the original Accept control.
The job became accepted; B opened Workshop and hid card/action, then B closed
Workshop and restored Board guidance. The player was still away from the
boarding prompt, so the restored guidance correctly had no enabled Board
button. All five states retain the same world, incarnation, build/root/body,
11 parts, 1035 kg, no paid IDs, 48 material and zero machinery. Every state has
later actual GPU/physics completion proof and passes the driver's uncaptured
GPU error assertion.

The traced driver is pinned by `source-review/traced-driver-sha256.json` and
its exact patch. The event trace proves successful routing in this attempt;
its added read-only observations also changed event timing. Because r01 has no
comparable event trace, **the first missed click remains unresolved**. This is
not evidence of a product fix. See `source-review/input-trace-review.md` for the
precise event interpretation. The combined outer browser run exited successfully and reports `status: passed`,
`browserErrors: []`, `sample.errors: []`, and no error-level console entries.
Its following mechanism journey passed 28 stages in 84.663 seconds. Those raw
records are archived once in the shared [outer report](../../LOOK-01/driven-cove-r01/browser/accepted-combined-r02/startup.json),
[mechanism summary](../../LOOK-01/driven-cove-r01/browser/accepted-combined-r02/summary.json)
and [runner log](../../LOOK-01/driven-cove-r01/checks/combined-browser-journey-r02.log).
See the [mechanism evidence](../../LOOK-01/driven-cove-r01/README.md) for its
separate scope and earlier failed attempts. The final joint required suite passed with **2,096 passed, three skipped and
four disabled** Voxy cases; terrain import reported **ten passed and one
skipped** from cache. The [exact result](../../checkpoints/driven-cove-r01/checks/summary.json)
records 1,132.896 seconds of Bazel elapsed time and preserves the earlier
interrupted invocation separately. The [joint checkpoint](../../checkpoints/2026-09-12-driven-cove.md)
owns publication. This scoped card success does not complete the broader
UX-01 gate.
