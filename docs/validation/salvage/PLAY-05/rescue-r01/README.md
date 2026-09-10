# PLAY-05: durable recovery of the accepted boat

This is a verified component of PLAY-05/SAVE-04. Full PLAY-05 and the gates
remain open. No screenshots, injected game-state setters or reward shortcuts
were used. This checkpoint preserves the LEGO environment.

## Player behavior

Press **Rescue / R** outside the workshop while playing. The game stops input,
releases every towing/harbor cable, returns the existing boat and player to the
dock, and saves automatically. Every fitted part stays on the boat with the
same ownership. Undelivered generator cargo returns to its authored recovery
site. A delivered generator stays secured, including on the powered harbor pier.
Resume unlocks after successful save publication. Save failures remain paused;
retry with **F10** on Linux or **Save expedition** in the browser.

Rescue costs no material and grants none. It preserves the accepted job phase,
paid parts, inventory, fitted loan provenance, build history and saved designs.
The existing boat and cargo bodies are moved; rescue spawns no duplicate craft,
cargo, part or entitlement. Workshop drafts/major edits must be closed before
rescue. Native R retains its workshop rotation meaning while editing.

This component recovers fitted starter loans. It does **not** replenish loans
deliberately removed in the workshop, recover future cut fragments or implement
general repair. Those are required outstanding PLAY-05 criteria.

## Ownership and physics implementation

`src/app/application.cpp` owns a bounded rescue phase:

1. `Requested`: obtain the normal paused join of the physics frontier, actual
   boat/cargo observations, session execution and ordered events.
2. `Releasing`: destroy the towing attachment and release all harbor slings.
   Retain the old tow handle in `rescueRope` until a post-command snapshot
   observes its retired slot. Dead GPU slots report generation zero; a live
   mismatched generation is refused. Harbor runtime retains its own four handles.
3. `Moving`: only after the release join, reuse the accepted boat's existing
   spawn recipe and actual wave height. Queue upright boat/cargo poses and zero
   velocities, reset the player and cross one more neutral tick.
4. `Saving`: require actual observed roots within 0.25 m of the requested
   relocation, then capture the integrated result, not queued transforms.
   Normal gravity/water can produce small residual velocities during that tick.
5. Return to `None` only after exact archive publication acknowledgment.

All gameplay controls, Resume, repeated Rescue and Leave are blocked while the
operation owns the world. No intermediate release/move checkpoint can be saved
manually. Each neutral tick uses the existing bounded snapshot/event path;
there is no additional recurring GPU readback or screenshot instrumentation.

The generic barrier is now `checkpointPending`/`checkpointDigest`.
`salvageCheckpointNeedsSave()` is the native host query. Delivery durability
continues to depend on banked cargo; an unbanked rescue cannot manufacture it.
Both hosts attempt each pending save once and require explicit retry after a
failure. Host action 7 acknowledges the SHA-256 of the exact frozen archive.
Storage closure/uncertain publication retains the existing fencing rules.

SVCE v1/v2, canonical session, save envelope, inventory and entitlement formats
are unchanged. `rescue.completed` in the read-only JSON is a transient UI
acknowledgment counter; a new process resets it. It is not persistent progression.

## Validation

The linked reports contain every recorded stage, actual saves and runtime
hashes. Final results are listed in `results.json`. Native control drivers use
physical GLFW scancodes directed only to their own child window. Browser checks
use real pointer/keyboard controls, read-only state, actual IndexedDB publication
and page reload. Neither uses direct gameplay action injection.

- Native banked/suspended source: existing 1,035-kg boat, paid ID 35, 96 material,
  four live harbor slings and generator at `[7,1.925,-47]`. Actual save-directory
  permission failure prevents publication. Resume/R/W stay frozen; restoring
  permission and F10 retry commits. Two rescues/restarts and sailing pass.
- Native unbanked source: existing 1,275-kg boat, paid IDs 35 and 100, zero spare
  material. Rescue while the job is available preserves that phase. After actual
  job acceptance, boarding, hooking and winch input, another rescue retires the
  real cargo cable and returns the single generator to its recovery site.
  Two process restarts and sailing pass without buying or granting parts.
- Browser focused source: the actual saved pre-lift world from harbor-live-r01.
  Two rescues/reloads release the four slings, preserve the installed generator,
  paid ID and inventory, and allow sailing and drained Leave.
- Browser fresh journey: paid construction, actual hook/lift/steered delivery,
  automatic delivery save, reload, refusal of duplicate reward, two rescues,
  two further reloads, sailing and drained Leave. This covers a banked generator
  before harbor installation as well as the installed case above.
- Native Bazel and CMake application builds pass; the WASM application builds.
  Movement/archive regressions: **42 cases**. Session ownership, entitlement,
  transaction and recovery regressions: **113 cases**.
- Browser save coordinator: **16 named cases**; preview controls: **20 cases**.
  The local Node test reporter groups these as two file tests. New cases cover
  unbanked rescue capture timing, exact acknowledgment, storage failure/manual
  retry, closure during publication and Resume/Leave control fencing.

Browser r02 used `web-r02`; preview `web-r03` has the same compiled JS/WASM/data
and adds the final one-line UI lock disabling Leave while a rescue is pending.
That UI change passes the final coordinator/control cases. The engine already
refused Leave in both actual journeys. No extra full gameplay replay was done
for that isolated UI change. `manifest.json` records both packages explicitly.

## Retained unsuccessful attempts

- Native r01: command-line invocation omitted the required binary argument; the
  game never started.
- Native r02: rescue, real write failure/retry and both restarts succeeded, but
  the final scripted walking route crossed the harbor corner post at approximately
  `(4.5,-50)`. The corrected route goes via `(5.5,-51)`; no collision was weakened.
- Native r03 physically passed, but its test baseline dictionary was aliased to
  the first report record. Updating the expected job phase after actual acceptance
  also mislabeled that earlier record. The final native unbanked rerun copies
  the expectation and preserves the original available-state record. Retain r03
  as a harness defect, not the final evidence for that scenario.
- `ui-final.log`: the new UI test omitted the required job phase when creating
  a fake state, causing a missing-distance error. The fixture was corrected;
  `ui-final-r02.log` is the final passing result. Runtime job data was complete.

## Reproduce without screenshots

Use the repo Nix shell. Start from an unused isolated output/save root. The
source slots below are copies of real earlier gameplay archives, not synthesized
checkpoints. The driver checks their hashes and never modifies them.

```bash
nix-shell --run 'bazel build -c opt --jobs=8 //:voxy_native'
nix-shell --run 'python3 scripts/validate_native_cove_rescue.py --binary bazel-bin/voxy_native --source-slot docs/validation/salvage/PLAY-05/rescue-r01/sources/unbanked --storage-root /tmp/voxys-rescue-new-unbanked --output /tmp/voxys-rescue-new-unbanked-report --hook-before-second-rescue'
nix-shell --run 'python3 scripts/validate_native_cove_rescue.py --binary bazel-bin/voxy_native --source-slot docs/validation/salvage/PLAY-05/rescue-r01/sources/suspended --storage-root /tmp/voxys-rescue-new-suspended --output /tmp/voxys-rescue-new-suspended-report --permission-failure'
nix-shell --run 'bazel-bin/tests/voxy_tests --gtest_filter=CoveMovement.*:CoveSave.*:GameSession*'
node --test scripts/test_cove_saves.mjs scripts/test_salvage_preview.mjs
```

Build WASM using the configured local SDK and copy the current `web/` tree plus
matching `voxy_wasm.js`, `.wasm` and `.data` into a new package directory. Then:

```bash
VOXY_TEST_CHROME=/usr/bin/google-chrome VOXY_SMOKE_GPU=hardware \
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_COVE_RESCUE=/tmp/voxys-rescue-new-browser \
VOXY_SMOKE_REPORT=/tmp/voxys-rescue-new-browser-report.json \
node scripts/smoke_integrated_wasm.mjs /absolute/matching-web-package salvage-cove
```

For the focused browser scenario, clone only the isolated frozen
`build-harbor-live-13t9ivnw/browser-prelift-profile-r01` profile. Keep its
`voxys-smoke-profile` marker, set `VOXY_SMOKE_PROFILE` to that new clone,
`VOXY_SMOKE_PORT=38210`, and
`VOXY_SMOKE_RESUME_WORLD=d29b3950f727db3cfbc79490c752187f`.
Never point the driver at a personal browser profile. The native-compatible
source envelope is also retained under `sources/browser-prelift`.

## Continue the goal

Finish the remaining PLAY-05 recovery policy before checking its parent:
atomic starter replenishment after deliberate removal, paid-part storage and
blueprint retention across that replacement, bounded abandoned/cut-object
recovery, and repeated rescue/dismantle/bank/reload exploit coverage. Reuse the
existing trusted entitlement-retirement, canonical transaction and archive
boundaries; do not re-bootstrap a running world or silently revive a retired
loan ID. Then implement PLAY-06's heavier/awkward second job and useful revision
reward. No full gate or gate commit is claimed by this component. All broader
fault cases, craft/berth coverage, Windows/hardware/human acceptance and unrelated
prerequisite gates retain their existing unmet status.
