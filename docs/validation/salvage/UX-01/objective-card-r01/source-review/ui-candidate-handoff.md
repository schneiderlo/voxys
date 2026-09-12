# D42 browser next objective — isolated candidate

Prepared 2026-09-12. Four source files only, under `overlay/`:
`web/index.html`, `web/salvage_preview.js`, `web/salvage_preview.css`, and
`scripts/test_salvage_preview.mjs`. Matching originals remain under `base/`.
`base-sha256.json` pins those originals; `candidate-sha256.json` identifies this
candidate. Live source still matches all four originals. Root owns integration,
actual browser verification, plan/evidence updates and publication.

## Player behavior

One compact **Next objective** card sits immediately above Workshop and Interact
in the existing Cove panel. It is hidden while building and on inspection routes.
The existing recovery job/field-tools drawers remain available. No build, paint,
design-library, save or original action is removed or changed.

The card offers at most one existing action:

- Available job: promote the existing recovery acceptance button.
- Accepted job: promote a real Board/Use helm prompt, Hook, or reveal the
  existing winch controls when attached. Revealing controls opens the drawer and
  focuses Reel; it sends **no motor command**.
- Delivery: promote only the original enabled delivery control, even if no tow/
  winch/boarding is needed. Distance is never used to grant permission.
- Durably delivered job: promote the existing dock interaction or harbor-power
  control when enabled. It never announces powered state before both durable
  delivery and durable installed harbor state exist.
- Missing winch: an enabled existing Workshop action can be promoted.

Waiting, pausing, saving, rescue, launch, hidden Workshop and failure states clear
the promotion. Explicit `session.admissionOpen=false` shows an unavailable
expedition with no promoted action, even if an older original button remains
enabled or the game is paused. Missing optional admission observation preserves
older engine compatibility. A paused card leaves the existing Resume/Save controls in place.
Save waits point to the existing Save expedition status/retry area.

The card is bounded by the existing 352 px panel, with wrapping text/button and
no fixed text clipping. It adds no image, texture, engine action ID, Application
state, ABI, catalog, save schema or gameplay permission. Existing three-line
workshop status and paint controls are unchanged.

## Permission and lifecycle contract

The controller selects guidance **after** existing handlers' button state is
updated. Promotion requires that exact target button and its group to be
unhidden/enabled. Before a card click is forwarded, the controller rereads engine
observations and requires the same objective step and target, plus their current
permissions. It then calls the original button's `click()` once, reusing its
established handler. A stale Accept→Board or Hook→attached transition therefore
cannot accidentally invoke either an old or newly substituted action.

Drawer revelation only opens the existing `salvage-field-tools` disclosure and
focuses its enabled Reel control. Missing disclosure means no promotion.
Cleanup removes the card listener and hides the card; failure removes the active
target. Existing Leave acknowledgement/navigation and all original cleanup stay
unchanged.

The final button state is applied once after the target is known. An unchanged
100 ms refresh never transiently disables/hides the focused action. Unchanged
title/detail/label text is not rewritten, keeping the live region quiet.

## Executed CPU check and pending acceptance

The final reviewed-fix invocation passed, with stdout retained in
`checks/node-ui-r02.log`:

```sh
/home/modkin/.nix-profile/bin/node build-cove-browser-objectives-r01/overlay/scripts/test_salvage_preview.mjs
```

Ten new assertion groups cover available/accepted prompts, stale clicks,
unconfirmed tow, one-action forwarding, drawer focus without motor input,
authoritative delivery priority, pending/save/pause/reset states, durable harbor
power, hidden Workshop, inspection, failure, Leave and cleanup, plus stable
keyboard focus/live text and explicit revoked admission. All 26 existing
assertion groups also ran. The old console aggregate omits the final recovered-
design group; this handoff uses actual top-level assertion groups rather than
claiming that legacy label is an exact total. `checks/node-ui-r02.json` records
the final 36 groups and the reason for this rerun. The first invocation's result
is retained in `checks/node-ui-r01.json`; only the two review findings and live
text change justified repeating this focused check. No Node case was skipped.

Independent review found the transient focus loss and revoked-admission cases;
both are corrected with regressions. `presentation_review` verified the final
delta and reported **no remaining actionable finding** in those fixes, existing
handler forwarding, stale-click guard, optional tow/lift states, cleanup or
source-level narrow wrapping. The reviewer ran no duplicate checks. Source
wrapping review does not claim measured browser layout.
No game process, GPU/browser journey, screenshot, visual approval or publication
is claimed. Root should integrate only the four reviewed overlay files, then
fold one real Accept→accepted/Workshop-hidden/close-visible card check into the
next necessary browser recovery journey. Preserve exact existing engine
permissions and require the actual job/open transition; do not inject state or
repeat a separate sailing/camera matrix for this card.

Root's bounded actual browser check should use the game's existing physical F9
uncapped option after startup/reload and confirm it with the existing read-only
getter. The preceding motion run exposed an approximately one-frame-per-second
capped RAF despite low measured CPU/GPU work; this card changes no application
frame policy and claims no performance fix. Its acceptance needs only the
initial promoted Accept action plus Workshop open/close visibility, not another
full motion/delivery journey.
