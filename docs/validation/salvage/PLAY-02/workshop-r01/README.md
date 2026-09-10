# PLAY-02: starter workshop design editing

2026-09-09, root under D21. Branch `codex/salvage-implementation`, after G00
`7f28fab`. This is a scoped design editor checkpoint. PLAY-02 and its parent
quality gates remain open; DATA-05's independent review is still outstanding.

## Player-facing behavior

At the cove's starting dock, choose **Workshop** or press **B**. Select a part,
move it by one stud or plate, rotate it, snap it to a compatible free socket,
keep a valid change, remove a part, or undo. The view highlights the selected
part green when the candidate compiles and red when it does not. The short
message gives connection/clearance feedback. Orbit and zoom use buttons or
A/D and W/S. B returns to the dock. The workshop retains its design while it
is closed and reopened in the same scene.

**This edits a design only.** The UI explicitly says that sailing still uses the
starter craft and that edits disappear when leaving the scene. No paid parts,
physical identities, inventory, saved progress or live launch are created by
this component. It must not be described as a finished player-built boat loop.

## Source and ownership

- `src/game/expedition/cove_workshop.*`: the bounded design command controller.
  The trusted loaded cove supplies shared immutable cooked assets. Accepted
  design and current candidate are separate copies of registry design data.
  Rejected placement never changes the accepted design. Up to 32 accepted
  registry snapshots support undo; revisions refuse overflow.
- Each selected part uses its actual authored sockets. Reconnection removes
  that part's old welds, then requires matching connector types, exact lattice
  position, opposing socket frames and available capacity. The real
  `CoveBoatAssembly` compiler validates occupancy/clearance, a single welded
  component, mass, hull and flotation. Snapping searches at most 8,192 socket
  pairs and 256 unique candidate transforms. It does not invent a weld across
  empty space. Remove/undo recompute the actual design's mass and connections.
- `src/app/application.cpp`: actions 60–78 connect the editor to the cove.
  Action 60 opens/closes; 61–74 map in order to `CoveWorkshop::Action`;
  75/76 orbit and 77/78 change view distance. Entry requires the player within
  3 m of the starting dock spawn, on foot, with no pending scene/job control.
  Workshop input takes precedence over walking, propulsion, reset and winching.
  Leave remains available and retains the existing GPU ownership drain.
- Design display uses the same cooked meshes as the real boat, presented 4 m
  above its authored local placement for a dry editing view. The selected
  part's tint is bounded and validated by `SalvageAssetFixture`; removed design
  parts are omitted. The running physical boat/cargo are not edited. Their
  original display returns when the workshop closes.
- `web/salvage_preview.js`, `web/index.html`, `web/salvage_preview.css`: a compact
  workshop panel, validity text, keep/undo gating and camera controls. Focused
  workshop buttons retain their keyboard events; key-up still reaches the
  engine. Clicking the accessible game canvas restores keyboard focus without
  pointer lock. The panel leaves an unobstructed game view beside it.
- Both Bazel and CMake include the new shared component. Native keyboard input
  uses the same application actions as the browser controls.

## Verification

**Passed:** three actual-craft workshop command cases plus the existing fixture
initialization-refusal case in the [native test log](native-tests.log); native
application and WASM application builds; eleven JavaScript UI lifecycle cases;
and the [eleven-stage real browser journey](browser-journey.json), followed by
drained Leave. The browser reports no uncaptured GPU errors. All checkpoints
retain the original live boat's 11 parts/1,035 kg and unchanged session inventory.
Keyboard rotation, an unobstructed focusable game view, no workshop pointer lock,
stationary player during editing, and walking after close are explicitly checked.

GPU: AMD Radeon 890M / RADV STRIX1, Chrome 152 WebGPU. Source digests are in
[sources.sha256](sources.sha256). The [three resolved trial failures](resolved-failures.json)
record a click/refresh race, missing canvas focus and delayed keyboard ownership
release. The final run passed after their fixes. No images, screenshot matrices,
pose setters, direct command injection or simulated success are used.

The verified local package is `/tmp/voxys-workshop-web-r03`, served at
`http://127.0.0.1:38194/?experience=salvage-cove`. Open **Workshop** at the starting
dock. Do not repeat the successful journey unless a new change or failure
justifies it.

The focused native tests exercise an actual authored winch snapping to another
free socket, disconnected-placement refusal, unchanged input scene, exact undo,
and cradle removal/restored mass. Browser UI lifecycle coverage includes
invalid keep gating, focused keyboard ownership and complete listener cleanup.
The actual browser journey covers opening, invalid placement, snap, keep, undo,
cradle removal/restoration, keyboard rotation, camera orbit, close/reopen,
resumed walking and drained Leave.

Reproduction:

```sh
nix-shell --run 'bazel build -c opt //:voxy_native //tests:voxy_tests'
nix-shell --run 'bazel-bin/tests/voxy_tests --gtest_filter="CoveMovement.Workshop*:SalvageAssetFixture*"'
node scripts/test_salvage_preview.mjs
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_REPORT=/tmp/workshop-browser.json \
VOXY_SMOKE_COVE_WORKSHOP=/tmp/workshop-journey \
node scripts/smoke_integrated_wasm.mjs /path/to/staged-web salvage-cove
```

## Required next implementation

1. Bring the accepted starter design into a real owned GameSession build.
   Registry placement ordinals and the compiler's transient cove IDs are not
   durable inventory authority. Add a bounded transaction for whole connected
   design edits; the current Add/Move/Remove intents alone cannot atomically
   replace several welds. Preserve exact part identity, provenance, inventory,
   lease, revision, undo/redo and journal/recovery rules. Blueprint designs must
   not mint physical ownership.
2. Prepare the edited assembly and reserve shape/body/render resources before
   launch. Validate that navigation, helm, propulsion, tow frames, dry draft and
   boarding support still exist. A geometrically connected draft is not itself
   a seaworthy/launchable craft. Reject missing capabilities with readable text.
3. Use the existing owned GPU submission and future execution transaction
   protocol to replace the live craft. Confirm old-body retirement and new-body
   pose before canonical publication; retain old resources through completed
   use. Update the player collision/helm, root-to-part render mapping, winch
   endpoints and physics shape together. Do not just change render placements
   or backdate a model tick. Define workshop recovery/placement as an explicit
   launch policy; never silently teleport cargo to claim job success.
4. Demonstrate a player keeping a different valid layout, launching it, boarding,
   sailing and towing with the actual changed geometry and mass. Verify invalid
   launch preserves the previous craft and exact resources. Do this once through
   real controls; do not return to repeated image reviews.
5. Complete the remaining PLAY-02 scope: catalog part addition, module settings,
   click picking, named/duplicated designs, durable save, controller bindings and
   full camera controls. This checkpoint does not waive them or any parent gate.
