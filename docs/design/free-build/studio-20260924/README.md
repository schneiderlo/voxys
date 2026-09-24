# Toy construction HUD

Historical browser prototype. The current implementation and current PNGs are in [Shared game HUD](../shared-hud-20260924/README.md); it supersedes this design.

A redesign of the default creative-building browser interface, following the September 24 request for a more polished and intuitive UI. The previous minimal-UI document remains the historical brief; the follow-up prioritizes gameplay space with a compact HUD and an authoritative C++ game state.

## Design

A blue moulded-plastic, 108-pixel-tall desktop hotbar leaves the world visible. Small icon controls replace the branding, labelled navigation and three-row studio dock. The selected piece name and placement hint sit just above the hotbar. Larger panels open on demand. Locally hosted DM Sans supplies consistent typography. No backdrop blur or continuous UI animation.

- Upright piece thumbnails, a clear selected state, hover names, accessible labels, scroll buttons and keyboard navigation.
- Visible rotate, undo and catalogue actions; contextual placement guidance.
- Seven vector two-stud paint bricks with top/side shading, named labels, hover/focus feedback and a selected checkmark. Original shows mixed materials. Colour applies to new pieces.
- A studded blue parts box with recessed compartments, large model thumbnails, raised category keys and yellow selection states. No full-scene dimming. Empty states and runtime row identities are preserved.
- Consistent pause, settings, help and control dialogs, plus clear Save/Saved states.
- 44-pixel controls, modal focus containment, inactive background controls, restored popup focus, reduced motion, high contrast and enlarged text.
- Responsive desktop, portrait and short landscape layouts.

The shared browser/native thumbnail generator now uses an upright camera basis. Physics, save schema, piece geometry and gameplay permissions are unchanged. Existing Preact and Lucide dependencies are reused; no runtime CDN is needed. `ui/src/bridge.js` continues to own the interface to accepted game state.

The earlier Tiny Glade-inspired pass was too subtle. This revision uses a toy construction-set material language throughout the HUD: moulded plastic rims, studs, shaded paint bricks, recessed bins and press-down feedback. Tiny Glade informed the world-visible interaction; the visual direction now follows the user’s request for a LEGO-like feel. Catalogue thumbnails still show actual Voxys geometry. Paint samples are small original SVG components. No new renderer, runtime dependency or animation loop is used.

## Review images

These show the actual compiled components in Chromium with an **explicitly synthetic engine fixture and a simple backdrop**. They are UI previews, not screenshots proving live gameplay.

- [Parts box close-up — latest toy design](toybox-catalog.png)
- [Paint bricks close-up — latest toy design](toybox-palette.png)
- [Desktop dock](desktop.png)
- [Colour palette](palette.png)
- [Brick catalogue](catalog.png)
- [Structure catalogue](catalog-structure.png)
- [Mobile catalogue](catalog-mobile.png)

## Verification

- Production UI bundle and source/output manifest rebuilt.
- Two camera-orientation regression tests pass; all 15 browser/native thumbnail outputs match the generator. The native HUD test recipe was updated, but its Bazel rerun was cancelled while waiting for another ongoing build to release the server.
- 14 component/bridge tests cover input ownership, pending actions, stale menu intents, save protection, piece/colour commands, cannon/motorbike states, search, focus and accessibility preferences.
- 14 Chromium layouts plus interaction checks: desktop, phone, 320-pixel width at 150% text/high contrast, and short landscape. Checked the desktop hotbar stays within 680 × 115 pixels, viewport containment, keyboard piece selection, Save/Saved, search identity, empty results, inert background controls, popup focus restoration, unobstructed paint-brick touch targets, colour selection, hover names and the full structure tray at enlarged text size. [Browser report](browser-report.json).
- The separate live-game attempt reached 22 rendered frames, then failed with Intel D3D12 `DXGI_ERROR_DEVICE_HUNG`. This same driver error occurred before the UI redesign. The attempt is **not** a passing end-to-end gameplay check, and no FPS claim is made.

Repeatable commands are in [ui/README.md](../../../../ui/README.md). Keep source and generated assets together when publishing.
