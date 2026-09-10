# MECH-05: cut authority, history and logical recovery

Recorded 2026-09-10 on `codex/salvage-implementation`, based on G00 commit
`7f28fabfabf3d63726f6cfa1001c3ce1ed557911` and the existing implementation tree.
This completes the scoped cut authority component under D32. MECH-05, PLAY-05
and their gates remain open. The playable cove still refuses cutting until
multi-root physical publication and archives are integrated. No screenshot,
player cutting journey, physical fragment reload or human acceptance is claimed.

## Implemented behavior

- `BuildModel::cutWelds` is the shared canonical mutation used by GameSession and
  the previously implemented immutable AssemblyFracturePlan. It accepts a bounded
  nonempty list of exact enabled Weld IDs, preserves each record except for
  enabled=false/damage=10000, validates the result, and increments topology once.
  Failed preparation leaves the old model untouched.
- `CutWeld` is command intent tag 10, containing only BuildTarget and connection
  ID. GameSession authenticates caller/token/epoch/revisions/lease, prepares a
  candidate, and reserves both journal records and the complete retained weld
  payload before calling the adapter. No IDs, refunds, stock transfers or free
  loan grants are introduced. The receipt identifies the cut connection and
  resulting build revision.
- `WeldCutTransition` owns the complete before-connection through an immutable
  shared value; its after-image is derived, with no redundant mutable payload.
  It is exclusive with part images, refit and storage transitions. Refit deltas
  retain their existing insertion/removal semantics for connections.
- HistoryAction Service removes only entries for the cut build, including its
  previous launch history. Other builds retain usable Undo. A cut itself has no
  undo/redo entry. This prevents reconnecting fragments for free through launch
  Undo. Evicted entry IDs must have been possible before the transition.
- PreparationRequest borrows the CutWeld during begin. Field cuts are allowed
  outside workshop mode, but the trusted adapter must enforce tool/reach and
  reserve all physical resources. The current cove adapter explicitly rejects
  cut requests before changing any live body, including cuts that leave an
  alternate welded path connected.
- Execution-bound sessions stage only after lease revalidation at the future
  tick. Accepted ownership publishes after the adapter certifies execution.
  Pending crash restoration cancels cuts without invoking preparation; committed
  cuts preserve disabled bonds and every part under a fresh authority writer.
- Logical SVSC/SVJB envelope 3 retains cut commands and transitions. Existing
  content remains canonical envelope 1 or 2 when no new fields are needed.
  Physical SVCE is unchanged. Native and WASM preserve the same frozen old and
  new save bytes; see [the format](../../../../salvage-session-save-format.md).

Forward replay compares the retained bond against the actual predecessor, runs
the same canonical cut, and verifies unchanged economics/allocator and exact
history eviction. Covered-journal admission matches the resulting bond, reverses
the transition, validates that predecessor, and reruns the mutation. Checksums
do not authenticate history: a plausible prior damage value erased by a cut
requires an independently trusted predecessor to verify. This remains a trusted
host save format, never a player-owned checkpoint import command.

## Verification

| Check | Result |
| --- | --- |
| Bazel focused regression targets | 250 cases pass; no skips |
| CMake authority, transaction and fracture targets | 156 cases pass; no skips |
| Actual JS-EH/Asyncify authority/save WASM | 179 cases pass; no skips |
| Actual JS-EH/Asyncify construction/fracture WASM | 88 cases pass; no skips |
| Bazel and CMake native applications | Build successfully |
| Configured WASM application | Builds successfully |

Bazel targets are game_session (136), session_transactions (12), session_events
(15), save_generation (3), cove_save (13), build_model (25), assembly_fracture (8)
and cove_player (38). CMake runs game_session (136), session_transactions (12)
and assembly_fracture (8). Each WASM runner uses the shipping JS exception and
Asyncify configuration, fixed 64 MiB heap and 1 MiB stack, and records exact
source/compiler/runtime hashes. Native cove regression tests include GPU-backed
existing movement/physics and actual paid-pontoon shape preparation; they do not
claim live cut publication. Authority tests use a deliberately fake adapter.

Eight new GameSessionCut cases cover:

1. Field cutting waits for confirmed execution; parts, condition, paint,
   loan/paid provenance, stock, kits, money, cargo/jobs and allocator remain exact.
   Pending/committed exact retries do not activate twice; another cut of the same
   disabled weld refuses.
2. Cut-build history is removed; unrelated build Undo still works. A retained
   old launch Undo cannot reconnect the cut build.
3. Eleven admission/cancellation/backend cases preserve accepted ownership:
   unknown/disabled weld, wrong caller, stale build/session, expired lease,
   rejected begin/activation/poll, cancellation and allocation exception.
4. Future-tick lease expiry prevents staging. A pending crash restores the
   uncut build, removes the old lease and retains all owned values.
5. Thirteen forged bond/balance/history/part/payload/assigned-ID cases refuse
   both covered admission and forward replay. A plausible altered old damage
   value also fails replay against the held predecessor.
6. Every cut journal boundary round-trips through bytes and replays exactly.
   Version downgrade refuses; compacted fresh-writer recovery retains fragments.
7. Exact journal capacity succeeds, including reserved authority closure; one
   byte less refuses before adapter or journal mutation. Recovery byte limits
   also account for the cut payload.
8. Two cuts separate three canonical parts. Rebuild retires both old loan parts,
   stores the paid part with exact ID/condition/paint, and a second rebuild
   cannot duplicate it or add resources. This is logical ownership recovery,
   not the still-pending physical collection of remote fragments.

The new frozen whole-envelope fixtures are 2674 bytes with SHA-256
`49704f0e7cfdc07f0d3015eccabd20d8ac7773b77724973996dafc0647d56e17`
and 957 bytes with SHA-256
`100155e57d6ae7b491e269af2cf0277bf39abc9271e41e2cf3785089e0689452`.
Both native and WASM assert these exact values. Existing three envelope-1 golden
fixtures remain byte-for-byte identical; envelope-2 kit/stock recovery passes.

## Reproduction and retained failures

```sh
nix-shell --run 'bazel test -c opt --jobs=8 //tests:game_session //tests:session_transactions //tests:session_events //tests:save_generation //tests:cove_save //tests:build_model //tests:assembly_fracture //tests:cove_player'
nix-shell --run 'bazel build -c opt --jobs=8 //:voxy_native'
nix-shell --run 'cmake --build build-native-save-host --target voxy_native game_session_tests session_transactions_tests assembly_fracture_tests -j8'
nix-shell --run 'ctest --test-dir build-native-save-host --output-on-failure -R "^(game_session|session_transactions|assembly_fracture)\."'
```

Run the two checked-in WASM validators with a new output directory and
`--sdk /tmp/voxys-emsdk/upstream --node /home/modkin/.nix-profile/bin/node
--exception-mode js --asyncify`. For `scripts/validate_session_transactions_wasm.py`
also pass `--expected-tests 179`; the compiler/fracture runner expects 88.
Set TMPDIR to an existing workspace scratch directory inside nix-shell.
`wasm-r01/manifest.json` and `wasm-fracture-r01/manifest.json` retain the complete
executed command lines, checks, versions, byte limits and source hashes.

Earlier attempts are retained. Initial test compilation used nonexistent fixture
accessors and lacked macro braces; corrected to public APIs and strict syntax.
The first cut tests found that journal reservation omitted the new before-bond
payload. Production now reserves it before preparation. Forged history testing
also identified impossible evicted IDs, now refused. Other test corrections used
the existing HistoryConflict result, a valid wrong-caller fixture, and included
the journal's mandatory closure reservation in exact-capacity expectations.
No test was disabled. CMake's standalone authority targets had stale source
lists after earlier refit/save additions; build_refit and session_save are now
included where required. The final build/test logs supersede these failures.

## Required next integration

1. Generalize `CoveBoatAssembly`, Application's live boat owner and Launch from
   one body/shape to all canonical roots. Preserve one owning build and every
   placement/part binding. Primary control must follow the helm/control part,
   not whichever root sorts first. Explicitly bound shape, body and water-driver
   demand including existing cargo/scenery/hoist and temporary replacement slots.
2. Feed AssemblyFracturePlan certified post-solve root motion at a held joined
   tick. Prepare every shape and inherit the parent's velocity field. Retire old
   bodies and create all children under the existing execution/publication
   boundary; never simulate both generations or reapply an impact impulse.
3. Retarget ropes, winches, harbor slings, player support and rendering by stable
   part/socket membership. Clear old collision/cache mappings, handle every
   failure/retirement path, and refuse insufficient capacity before mutation.
4. Extend physical SVCE with bounded root IDs/revisions/motions and validate the
   exact compiled root set. Keep all old archive bytes and lineage rules valid;
   restore every fragment before making the save playable. Logical envelope 3
   is already implemented and must not be confused with physical SVCE v3.
5. Protect the last intact design before a first cut. Current backup normalization
   refuses disabled bonds. Preserve existing backup capacity/explicit removal
   behavior; do not silently re-weld a broken build while importing its design.
6. Add reachable cutter controls and trusted tool/reach admission, then real
   native/browser cut, sail, save/restart, rescue and repeated-rebuild journeys.
   Logical rebuilding must correspond to retiring all actual loan fragments and
   recovering paid parts exactly once, even away from the primary hull.
7. Continue repair, load/damage feedback and the remaining loss/exploit matrix.
   Preserve the LEGO terrain. No screenshots or image-comparison loops. Keep
   MECH-05, PLAY-05 and the gates open until their complete criteria pass; only
   then run the repository gate checks and commit that completed gate.

The last playable preview remains the frozen recovery-design package at
`http://127.0.0.1:38206/?experience=salvage-cove&telemetry=0`. Do not overwrite it
with a partial fragment implementation or reinterpret this component as a new
player-visible cutter release.
