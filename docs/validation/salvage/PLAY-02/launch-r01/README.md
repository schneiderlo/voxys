# PLAY-02: launch and sail the edited starter

Date: 2026-09-09. Owner: root. This is a **passed live-launch checkpoint**. Both application builds, 154 native checks, 12 UI lifecycle cases and a fourteen-stage actual browser journey pass. Four physical replacements, moved-winch towing, two safe launch refusals, observed Reset and drained Leave are verified without screenshots. This does not complete PLAY-02 or a full game gate. Preserve LEGO terrain and the owner's instruction against repeated image inspection.

## Player behavior

At the starting dock, **B / Workshop** opens the current boat design. Select a part, move/rotate/snap/remove it, and **Keep** valid changes. **Launch / Enter** rebuilds the physical boat at its berth and returns the player to the dock. The new mass, collision, buoyancy, propulsion, helm and winch frames come from that accepted design.

**Undo launch / I** and **Redo launch / O** replace the physical craft through the same authoritative path. These are separate from **Undo / U**, which reverses unlaunched design edits. Launch history controls require a clean editor so they cannot silently discard a kept or pending edit. Keyboard shortcuts work with the game canvas focused; focused menu controls retain their keys.

An easy first change is **Next part → Cargo cradle → Remove part → Keep change → Launch**. This changes the starter from eleven parts / 1,035 kg to ten parts / 945 kg. To move the winch onto that freed socket, reopen Workshop, select Winch, use **Move right** twice, then Keep and Launch. The new tow position changes the distance to the generator: sail into hook range before pressing F.

The ordinary sailing controls still apply: WASD walks; E boards/uses/leaves the helm; W/S throttle; A/D steer; F hooks/releases; Q reels; Z pays out. **Reset / R** returns the edited craft and player to the berth. **Leave** drains owned scene resources. Edits/history are local RAM state and disappear on Leave; this is not a durable save or campaign.

## Implementation and ownership

- `prepareCoveLaunchDesign` projects the authoritative build into the original admitted scene slots. Its trusted bootstrap bindings map exact owned IDs to installed assets, including dormant starter-loan parts restored by Undo. It never guesses an identity from a mesh, creates a new entitlement, or renumbers the scenery/cargo slots.
- `CovePlayer::initialize` receives the original boat slot set. Removed slots are absent from walking collision, rather than becoming fixed scenery. The render loop uses the same membership rule. Helm standing offsets follow a moved/rotated helm's authored part frame. Launch rejects missing support or obstructed required standing space.
- `CovePreparationAdapter` now supports changed builds alongside the existing cargo-delivery preparation. A single retained candidate owns a new scene, compiled canonical boat, player collision, clean workshop, water cells and water/tow descriptors. CPU work completes before an owned GPU shape upload begins.
- Staging waits for the actual physics frontier, boat observation and event stream to meet. The player must be on shore in the open starting workshop; the old boat must be upright and within four metres of its berth, with no attached rope or cargo hand-off. Wave-driven velocity/rocking does not disqualify an otherwise docked, unoccupied boat. Launch explicitly resets the candidate to its authored berth and dry draft; it does not move the salvage cargo.
- At the next tick, stage spawns/configures the candidate before destroying the old body. Configuration allocation/capacity failure cancels the unexecuted new spawn and leaves the old body alone. One helm and one propeller are required; at most one winch is supported. Removing the winch disables towing; moved winches use recomputed root and body-space anchors.
- Confirmation requires a real observed candidate pose/shape, old-body death, matching world incarnation and completed events at/after the staged tick. Only then does GameSession publish its new revision and the adapter swap accepted handles/pointers. Publication performs no new upload, physics command or allocation. The previous compiled objects and shape remain owned until actual retirement completes; a new Launch is blocked during that cleanup.
- Ordinary Leave defers session closure while execution is in flight, then removes the accepted boat. Before staging, closure cancels preparation and retires its upload. Fatal shutdown abandons the entire physics world; it never certifies success. The local-session destructor closes admission while the captured asset still exists.
- Controls 79/80/81 submit Launch/Undo/Redo through GameSession with fresh request sequence and current session/build/history revisions. The editor is frozen during preparation/execution/retirement. Boat interaction and tow input cannot run through pending authority work. Blueprint Keep never changes the live craft directly.

## Resolved findings

The nearest snap for the original winch is structurally connected but blocks the helm's standing room. Launch correctly refuses it and names the obstructed point. Removing propulsion is another valid structural design that Launch refuses without changing the accepted craft.

Early berth checks incorrectly treated water heave and angular velocity as player travel. They could reject the idle starter while it rocked on waves. The final workshop rule uses berth location, orientation, occupancy and cable state. This is an intentional shore relaunch, not an in-water hot swap of a moving occupied vessel.

The first extended winch journey assumed the relocated tow point would remain within eight metres of the generator. Its new position was about 9.7 m away at a wave crest. The corrected journey uses actual steering to enter range. No hook-range increase, cargo teleport or success injection is used.

## Remaining work / next agent

Do not repeat passed image-free journeys without a relevant source change or unresolved failure. Read `results.json` for exact final evidence, and keep historical failed runs identified as failures.

- Finish the rest of PLAY-02: add-part/configuration UI and its owned-slot/content admission, named/duplicated durable designs after SAVE, pointer picking, controller actions and full camera controls. The current retained-slot bridge deliberately refuses unknown/new IDs.
- Add focused live adversarial coverage for Leave during preparation/execution, GPU capacity/failure and repeated replacement resource bounds. The generic session boundary and shape lifetime suites are existing coverage; normal browser Leave alone is not evidence for every timing edge.
- The propeller/helm/winch consumer supports the starter profile; multiple drives, arbitrary actuators, latch/damage/flooding, cargo banking completion and campaign persistence remain their plan tasks.
- Native application compilation and shared native tests do not replace a native human/controller playthrough. No visual-quality, full-performance, multiplayer or AAA-completion claim follows from this checkpoint.
- No full gate is completed by this work. Commit only when a complete gate passes, including the repository's required pre-commit suites.

## Evidence and reproduction

See `results.json`, `browser-journey.json`, `browser-startup.json`, the build/test logs, `source-sha256.json` and `package-sha256.json`. `prior-trials.json` preserves the narrower successful first run and the failed extended trials; they are not the final acceptance evidence. The final result is browser run r05, native build r06/tests r05, WASM build r05. Native checks use the actual cooked starter and include removed-slot collision, exact restored bindings and standing-space refusal.

```sh
nix-shell --run 'bazel build -c opt //:voxy_native //tests:voxy_tests'
nix-shell --run 'bazel-bin/tests/voxy_tests --gtest_filter="GameSession*:SessionJournal*:SessionEvent*:CoveMovement*:CoveNavigation*"'
nix-shell --run 'EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j8'
node scripts/test_salvage_preview.mjs
# Assemble a local web package from web/* and build-lego-wasm/bin/voxy_wasm.{js,wasm,data}.
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_REPORT=/tmp/launch-browser.json \
  VOXY_SMOKE_COVE_LAUNCH=/tmp/launch-journey \
  node scripts/smoke_integrated_wasm.mjs /tmp/voxys-launch-web-r03 salvage-cove
```

The final package is `/tmp/voxys-launch-web-r03`, served for the owner at `http://127.0.0.1:38195/?experience=salvage-cove`. The earlier design-only preview at port 38194 is historical. Run `python3 tools/serve_wasm.py --directory /tmp/voxys-launch-web-r03 --port 38195` if the new server has stopped. Browser checks used hardware WebGPU on the local Radeon 890M / RADV STRIX1, not a performance acceptance workload.
