# D42 browser objective acceptance driver

Prepared source only. No browser, GPU, screenshots or gameplay checks were run by the author. Root runs this once against the final integrated package.

Install `overlay/scripts/validate_cove_objectives.mjs`. Apply only `smoke-dispatch.patch` to the current smoke runner, or compare its pinned base and overlay; do not replace unrelated runner changes. `VOXY_SMOKE_COVE_OBJECTIVES` dispatch precedes `VOXY_SMOKE_COVE_MECHANISMS`, so both can share one fresh browser session.

The short driver has a 90-second polling deadline after startup. It first uses the existing physical F9 key when needed and confirms the read-only uncapped getter. Before any UI scrolling, it checks the actual visible card, label, title, detail, action and text rectangles at 1280×800, then at 640×480. It requires natural zero page/panel scroll offsets, no horizontal overflow, all those elements inside both viewport and panel clipping bounds, and an enabled unobstructed Accept control matching the existing job button's label. It does not force-scroll the card or alter CSS.

It clicks the visible objective through real CDP pointer events, requires the job to become accepted, then uses physical B to open and close the workshop. The card and action must hide during construction and restore to the Board objective afterward. Five captured states each require a later completed GPU serial and physics tick with unchanged world, incarnation, build, topology, root and body identities. Every stage requires the exact starter boat: 11 parts, 1035 kg, no paid IDs, 48 salvage material and zero machinery; presentation count is exactly the configured value (default seven). It checks uncaptured GPU errors; the outer runner retains its existing browser/runtime error scanning.

On success it leaves the player at the dock, running, with workshop closed and the accepted job; it restores the 1280×800 viewport for the optional mechanism journey. It does not refit, save, reload, sail, reset or change owned assets. Summary JSON includes measured DOM bounds and labels, original game observations and completion proof; failure records the last DOM/game observations.

Example added environment variables for the established smoke invocation:

```sh
VOXY_SMOKE_WIDTH=1280 VOXY_SMOKE_HEIGHT=800 VOXY_SMOKE_NO_SCREENSHOT=1 \
VOXY_SMOKE_PRESENTATION_PARTS=7 \
VOXY_SMOKE_COVE_OBJECTIVES=build-cove-browser-objectives-r01/browser-journey-r01 \
VOXY_SMOKE_COVE_MECHANISMS=build-cove-mechanisms-r01/browser-journey-final \
node scripts/smoke_integrated_wasm.mjs FINAL_WEB_PACKAGE salvage-cove
```

Use a fresh world/profile, actual final package path and the established GPU/display environment. Omit `VOXY_SMOKE_COVE_MECHANISMS` for the optional standalone objective check. Runtime acceptance remains pending until root supplies actual results.

Author syntax checks passed for both scripts (`checks/driver-syntax-r01.json`). Independent read-only review by `presentation_review` found no actionable issue in actual B input (including fast press/release), real click routing, natural panel/text bounds, ownership and GPU-completion proof, or compatibility with the following mechanism journey. It confirmed that the integrated F9 input fix is required. The reviewer did not run the driver or claim measured layout success.
