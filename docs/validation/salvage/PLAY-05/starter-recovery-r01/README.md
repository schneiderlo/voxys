# PLAY-05: starter rebuild and exact paid-part storage

Component verified: 2026-09-10. Owner: root, `codex/salvage-implementation`, under D20/D21/D29/D30.
This is a component of PLAY-05. Full protected recovery, cutting, the remaining
loss/exploit matrix and parent gates stay open. No full-gate commit is claimed.
No screenshots, image captures or state setters were used for these journeys.

## Player behavior

- Dock workshop: **Rebuild starter / H** restores the installed original starter,
  including removed loaned pieces. Keep or discard unfinished design edits first.
- The original entitlement remains. Every current loan instance retires and the
  replacement receives fresh IDs; no retired part identity is revived.
- All fitted purchased parts move into bounded owned storage, preserving exact
  IDs, health, paint and settings. There is no material refund.
- **Add part** offers matching stored stock first, with a zero price. Launch
  consumes that exact part once. A later ordinary dismantle gives its catalog
  yield once; loaned parts never acquire sale value.
- Rebuild and stored-part withdrawal clear old launch undo for that build.
  They do not delete existing saved design-library entries. The service does
  not create an automatic blueprint backup of an unsaved current design.
- Real body replacement and retirement finish before publication. The service
  then pauses and publishes a whole expedition archive before unlocking Resume.
  A failed save keeps the previous disk generation and freezes controls for
  explicit F10 / Save expedition retry.
- Ordinary Rescue/R retains the current accepted design. Cargo, job, delivered
  generator, existing harbor progress and materials are not paid out or reset
  by the starter service. Terrain remains in LEGO mode.

## Architecture and continuation contract

`build_refit.*` accepts trusted stock/entitlement context separately from the
player design. It plans deposits, withdrawals and zero-cost loan replacement
without changing the input. `GameSession` stages the candidate build, stock,
inventory, grant bindings and selective history eviction together; cancellation,
capacity refusal and adapter failure retain the accepted world.

A trusted `StarterKit` registration binds immutable content policy to the existing
build and entitlement. Legacy cove saves register the original installed recipe
on their first explicit rebuild. New cove sessions register it in their initial
bootstrap. Neither path re-bootstraps an existing world or adds resources.
`RebuildStarter` carries only BuildTarget. `RefitBuild` may reference stored paid
source IDs, which never enter the newly allocated ID range.

`PartStorageTransition` retains exact deposited/withdrawn images and the previous
starter binding set. `HistoryAction::Service` invalidates only the target build's
history and creates no reversible ownership transfer. Exact replay checks the
planner's stock, IDs, economy and eviction list. Reverse covered-journal validation
reconstructs stock and build predecessors and checks old active loans against the
previous grant. A forged previous binding is refused even when registration is
outside journal coverage.

SVSC/SVJB envelope 2 appends kit/storage state and the bounded transfer payload.
Legacy state continues to emit exact envelope-1 bytes. SVCE physical archives and
SVSG disk envelopes retain their existing versions. Full byte order and invariants:
`docs/salvage-session-save-format.md` and `docs/salvage-cove-save-format.md`.

Cove launch/restore compares the registered immutable recipe against installed
content and maps fresh loan IDs to original render slots by recipe ordinal.
Stored paid parts have no live body. Reuse enters the same compiler and shape/body
publication boundary as ordinary paid launch. Candidate grant/transfer pointers
are borrowed only during `PreparationAdapter::begin`.

## Verification

- Final authority suite: **128 cases**. Includes planner/ownership/registration,
  repeated rebuild→reuse→dismantle, same-command retry, paid condition, stale undo,
  byte/part capacity, adapter rejection, cancellation, fresh writer and four
  tampered transfer cases through both checkpoint admission and exact replay.
- Cove movement/real asset mapping: **36 cases**, Bazel and CMake binaries.
- Cove archive validation: **9 cases**. Browser save coordinator: **17 named
  cases**, plus existing preview UI suite (Node reports two file-level groups).
- Native application builds through Bazel and CMake; actual configured WASM
  builds successfully. Existing unrelated GLM comparison warnings remain scoped
  to their compiler logs.
- Final native journey `native-r03`: **9 stages**, real saved 13-part paid boat,
  zero material, IDs 35/100. Explicit rebuild→actual unwritable-directory failure
  →frozen controls→F10 retry→process restart→free pontoon reuse→restart→second
  rebuild→restart. Exact paid identity stays divided between live boat and stored
  stock, never duplicated. Same unique cargo and accepted job remain.
- Native r01 reached saved reuse but its driver sorted string IDs lexicographically
  (100 before 35). The runtime selected numeric ID 35 correctly. Fixed the driver,
  reran r02, then reran r03 using the final CMake runtime after the additional
  predecessor grant check.
- Browser r01: **7 stages plus drained Leave**, isolated copy of the real legacy
  delivered save. Rebuild restores removed starter pieces, paid beam storage and
  free reuse survive three page reloads; banked cargo/materials stay unchanged.
- Browser r02: **23 stages plus drained Leave**, fresh real construction, paid
  beam, physical hook/lift/haul/delivery, no duplicate reward, then starter rebuild,
  stored beam reuse and three reloads. This preceded the additional predecessor
  validation guard. Final browser r03, using the corrected runtime, passes **28 stages plus drained Leave** through the same complete fresh expedition and recovery sequence. All three post-service reloads pass; paid ID/material/cargo checks stay exact.

The first registration-capacity fixture did not actually fill its byte budget;
corrected it to exercise a genuine failed reservation. Initial strict-warning
compile failures and the adversarial old-grant failure are retained in `checks/`.
They were fixed, not waived. Earlier journey logs are historical diagnostics;
`manifest.json` identifies the final sources, binaries and exact evidence.

## Reproduction

From the repository root, with existing Nix dependencies:

```sh
nix-shell --run 'bazel test -c opt --jobs=8 //tests:game_session //tests:cove_save'
nix-shell --run 'bazel build -c opt --jobs=8 //:voxy_native //tests:cove_player'
nix-shell --run 'bazel-bin/tests/cove_player'
node --test scripts/test_cove_saves.mjs scripts/test_salvage_preview.mjs
```

Use a new output and isolated storage directory for each actual native run:

```sh
nix-shell --run 'python3 scripts/validate_native_cove_starter.py --binary build-native-save-host/bin/voxy_native --source-slot docs/validation/salvage/PLAY-05/rescue-r01/native-r05/final-slot --storage-root /tmp/voxys-starter-new-saves --output /tmp/voxys-starter-new-native --permission-failure'
```

For a fresh actual browser construction/haul/rebuild journey, use the matching
web package and a new report directory. This creates an isolated Chrome profile:

```sh
TMPDIR=/tmp VOXY_TEST_CHROME=/usr/bin/google-chrome VOXY_SMOKE_GPU=hardware \
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_COVE_STARTER=/tmp/voxys-starter-new-browser \
VOXY_SMOKE_REPORT=/tmp/voxys-starter-new-browser-report.json \
node scripts/smoke_integrated_wasm.mjs /absolute/matching-web-package salvage-cove
```

For legacy focused browser replay, copy the preserved isolated pre-lift profile
`build-harbor-live-13t9ivnw/browser-prelift-profile-r01` to a new directory. Retain
its `voxys-smoke-profile` marker; remove only that clone's stale browser socket/
DevToolsActivePort files. Set `VOXY_SMOKE_PROFILE` to the clone,
`VOXY_SMOKE_PORT=38210`, and
`VOXY_SMOKE_RESUME_WORLD=d29b3950f727db3cfbc79490c752187f`. Never use a personal profile.

## Remaining work

Keep PLAY-05 and gates unchecked until recovery covers the complete specified
loss matrix, actual future cut fragments, bounded abandoned objects, and repeated
cut/repair/dismantle/bank/reload exploit cycles. Design-library persistence itself
is covered by its separate earlier storage work; this service preserves entries
but does not certify all blueprint-loss paths. Continue into the missing recovery
work, then PLAY-06's second job and useful sidegrades. Do not replace required
physical gameplay with pure planner or screenshot evidence. Commit only after a
full gate and required repository checks pass.

## Local preview

`http://127.0.0.1:38205/?experience=salvage-cove&telemetry=0`
serves `build-starter-recovery-pnO88sAB/web-r02`, the exact final browser-r03
package. Earlier preview origins keep their own saved worlds. Use Workshop / B,
then Rebuild starter / H. The plan remains open for the remaining full recovery
requirements and later game work.
