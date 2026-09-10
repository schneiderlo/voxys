# PLAY-02: working part settings

Date: 2026-09-09. Owner: root. This **module-settings checkpoint passes**. Full PLAY-02 and all pending gates remain open. Both application builds, 166 native tests, fourteen UI lifecycle cases and one nine-stage real browser journey pass. No screenshots were taken.

## Player behavior

At the dock, open **Workshop / B** and select a propeller, helm or winch. The settings buttons show its current values. With the canvas focused, **X** toggles the module, **L** cycles its output limit, and **N** reverses propeller drive. **Keep**, then **Launch / Enter** applies the settings to the sailing boat.

- Propeller: enabled/disabled, thrust limit and forward/reversed drive.
- Helm: enabled/disabled steering and steering-angle limit. Boarding and throttle input remain available when steering is disabled.
- Winch: enabled/disabled. A disabled winch cannot hook cargo. Launch already requires the existing cable to be released.
- Limits cycle 100%, 75%, 50%, 25%, 0%, then 100%. Zero thrust still permits wave-driven motion; zero steering disables commanded steering.

Settings have no material cost. Cancel, local Undo, Undo launch, Redo launch and physical Reset preserve the intended design history. Selecting another part discards unkept changes, as with placement edits. A new paid part begins with its authored defaults. Changes still disappear when this RAM world is left; durable saves are next.

Engine drive networks, per-channel input binding, automatic rope payout and winch force/speed tuning are **not** implemented by these controls. Engine/passive parts show no adjustable controls. The current consumer still supports one propeller, one helm and at most one winch. This does not complete all typed module settings or general machine simulation.

## Implementation and ownership

`FixturePartPlacement::settings` is an optional runtime workshop overlay. Installed registry schemas remain closed and do not parse that field. Absence selects authored defaults during inspection compilation. `CoveWorkshop` compares effective values, keeps the overlay in its bounded registry history, and exposes only supported controls. Equivalent explicit defaults and absent overrides compare equal, so freshly launched boats do not appear spuriously edited.

`CoveBoatAssembly::compileMembers` sends the override through the existing typed canonical validator and function compiler. `prepareCoveRefit` carries the selected settings for retained parts while preserving their ID, paint and provenance; an absent overlay preserves an already-owned part's canonical settings. New paid parts use their compiled defaults/edits. `prepareCoveExpandedLaunchDesign` fills each active slot from the accepted canonical part, so reopening the workshop and later edits retain settings after launch and after authority undo/redo.

The existing GameSession refit transaction reserves and confirms the operation. No canonical schema, journal format, ID allocation policy, loan entitlement or price logic changed. Settings-only edits keep all part identities, welds, mass, displacement and stock. The actual app still publishes only after the prepared physical replacement and old-body removal are confirmed. The focused native adapter test exercises the CPU session/compiler contract; it is not evidence of a native app playthrough.

Both initial physical setup and replacement preparation consume `coveModuleOutput`: enabled state multiplied by the typed limit for propulsion/steering, and enabled state for winch operation. Propeller reversal flips its authored thrust vector; it never supplies a forbidden negative thrust ceiling. The applied nonnegative ceilings go to `AuthoredWaterBodyDesc`, which is uploaded into the real water driver. Winch enable controls its live reel/interaction availability. Read-only HUD diagnostics expose the accepted driver's ceilings and direction for verification. These diagnostics do not mutate the simulation.

## Verified results

- Native app and shipping WASM app build successfully. Existing unrelated WASM warnings remain.
- **166 native tests, zero skips**: session, journal, event, cove/navigation and fixture-registry suites. The added case covers cancellation, local undo, supported-control restrictions, limit wraparound, free canonical refit, unchanged identities/provenance/mass, effective output, scene mapping, reopened editor, subsequent refit preservation, authority undo and redo. Existing paid-part and real native GPU cove tests pass as part of this run.
- **14 UI lifecycle cases** pass. New controls hide where unsupported, cannot submit while pending and show on/off, limits and direction correctly. Existing purchase, exact-price, launch and lifecycle cases pass.
- **One real browser journey, nine recorded stages, four physical replacements** passes on hardware WebGPU (Chrome 152.0.7977.82; device details in `browser-startup.json`). It cancels and locally undoes winch settings, keeps uncharged settings while the live driver stays unchanged, launches zero thrust/zero steering/disabled winch, boards and holds throttle/steering for 180 player ticks with less than 1 m of horizontal movement, and verifies the Hook button is disabled. It then launches 75% reversed thrust and 50% steering with the winch enabled. Applied ceilings are **1,875 N** and **0.3 radians**, versus original **2,500 N** and **0.6 radians**. Forward input for 150 ticks physically drives backward by more than 1 m. Reopened settings, observed physical Reset, undo to zero output, redo to the tuned output and drained Leave pass. Stock stays 48 and the eleven-part boat stays 1,035 kg.

The journey uses actual mouse/keyboard input and read-only JSON. It does not inject commands, poses, inventory or simulation ticks. Output/functional acceptance is proven; a matched turning-radius study, general hydrodynamics, visual acceptance and performance gates are not claimed.

The first native test compilation rejected two unbraced GTest assertions under `-Werror`; braces fixed them before the successful build/test run. That failed log is retained. An initial UI fixture edit accidentally changed one element lookup into a comma expression; its lookup was corrected and the fourteen-case run passed. No gameplay failure or repeated browser capture loop was needed.

## Reproduction and handoff

```sh
nix-shell --run 'bazel build -c opt //:voxy_native //tests:voxy_tests'
nix-shell --run 'bazel-bin/tests/voxy_tests --gtest_filter="GameSession*:SessionJournal*:SessionEvent*:CoveMovement*:CoveNavigation*:FixtureRegistry*"'
nix-shell --run 'EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j8'
/home/modkin/.nix-profile/bin/node scripts/test_salvage_preview.mjs
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_REPORT=/tmp/voxys-settings-browser-r01.json VOXY_SMOKE_COVE_SETTINGS=/tmp/voxys-settings-journey-r01 /home/modkin/.nix-profile/bin/node scripts/smoke_integrated_wasm.mjs /tmp/voxys-settings-web-r01 salvage-cove
python3 tools/serve_wasm.py --directory /tmp/voxys-settings-web-r01 --port 38197
```

The package contains the `web/` files and the shipping `voxy_wasm.js`, `.wasm` and `.data` outputs. The verified preview is `http://127.0.0.1:38197/?experience=salvage-cove`. Earlier preview packages remain historical. See `source.sha256`, `package.sha256`, `results.json`, full native logs and real browser reports. No full gate passed, so no gate commit was made.

Next implement durable named/duplicated designs through the SAVE contracts, without recreating the initial material grant on load or losing canonical settings/paid identities. Picking/controller/full camera, remaining functional settings, and targeted live cancellation/failure/capacity coverage still belong to PLAY-02 and its dependencies. Continue actual playable progress under D20/D21; do not resume screenshot loops or repeat this passed journey without a relevant change/failure.
