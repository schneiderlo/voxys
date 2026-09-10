# PLAY-02: connected design commands and recovery

Date: 2026-09-09. Owner: root. This is a **passed CPU/session checkpoint**, not a live launch or a completed PLAY-02/G01 gate. No screenshots were taken. Preserve the restored LEGO terrain and prioritize launching and sailing an edited craft next.

## What now works

- One `RefitBuild` command replaces the complete welded design atomically. It can retain/move/rotate/configure/paint parts, remove connected parts, add paid parts, and change weld endpoints. Intermediate disconnected edits never become accepted state.
- Retained parts keep their physical IDs, definition, health and paid/loan provenance. New parts and changed weld pairs receive fresh authority IDs. Existing welds keep their exact identity and condition. Rope, hinge and latch edits are refused by this starter transaction.
- Costs and salvage yields come from the admitted catalog. The two resource balances are netted independently. Starter loans give zero dismantling yield. Undo/redo restore exact part and weld identities and reverse the actual balance delta.
- Stale, invalid, unaffordable, oversized, canceled and adapter-rejected requests preserve accepted geometry, inventory and history. Allocated proposal IDs remain burned even if preparation fails. Repeated requests compare owned design values, not pointer addresses.
- Recovery captures owned requests and deltas. Replay verifies their exact predecessors, new ID ranges, costs and recorded outcomes. Covered refits are reconstructed backward from accepted builds to validate even overwritten requests. Dormant loan retirement also invalidates weld-only history referencing the removed part.
- The existing execution boundary accepts a refit only after its staged future tick has been confirmed. A fake-backend test covers this contract; it does **not** prove a real GPU replacement.
- `prepareCoveRefit` converts the actual workshop design into retained canonical parts and render-placement bindings. The real cooked starter passes both moved-winch and removed-cradle scenarios through GameSession and the hull compiler. Mass, part identity and loan ownership agree. These are CPU compiler/adapter tests.

## Important limits

The live app still uses the original sailing body. `CovePreparationAdapter::begin` still rejects a changed build. The workshop's Keep/Undo controls still edit a separate design. No new browser preview was opened for this checkpoint; the delivered workshop preview remains available.

The generic authority supports new paid parts and configuration. The current editor/`prepareCoveRefit` bridge only retains or removes placements already on the live craft. It rejects attempts to revive a removed physical part from an old design. Added-part UI, authoritative launch undo, named durable designs, controller/picking/camera completion and shipping save bytes remain unfinished.

The journal/checkpoint are typed volatile RAM models. Shared ownership does not provide durability. The schema has no shipping serialization claim; SAVE still supplies authenticated canonical bytes and storage.

## Implementation map

| Source | Responsibility |
|---|---|
| `src/game/construction/build_refit.hpp/.cpp` | Immutable bounded request, changed-part/weld delta, exact delta application and whole-design preparation |
| `src/game/expedition/session_transactions.hpp/.cpp` | Refit intent/value equality, directed build transition, contiguous assigned-ID range, owned journal byte accounting and non-throwing publication copies |
| `src/game/expedition/game_session.cpp` | Authorization, ID leases, net inventory reservations, candidate preparation, reversible history/escrow and execution publication |
| `src/game/expedition/session_recovery.hpp/.cpp` | Owned capture, history and covered-journal validation, replay, pending cancellation, restored lineage and loan retirement |
| `src/game/expedition/cove_build.hpp/.cpp` | Actual workshop-to-canonical request and placement mapping |
| `tests/test_game_session.cpp` | Eight refit scenarios plus existing session/recovery/observation regressions |
| `tests/test_cove_player.cpp` | Actual cooked starter compilation for a moved winch and removed cradle |
| `scripts/validate_session_transactions_wasm.py` | Same 133 session/journal/event cases with shipping JS exceptions and Asyncify; includes new source/header and explicit expected count |

A nonzero `RefitPart::source` retains an active owned part. Zero requests a new paid part. Design ordinals are contiguous from 1; welds reference these ordinals and authored socket IDs. Only authority supplies IDs, health and provenance. Factories privately own their vectors and delete copy construction/assignment to prevent mutable aliases behind a const shared pointer.

`BuildRefitDelta` binds one build ID and stores only changed part/weld records. `BuildTransition::refitForward` selects the actual direction for compensation. `visitPartChanges`, `referencesPart` and `removedPartCount` keep legacy and connected history accounting consistent; connection endpoints also count as retained references.

Refit preparation reserves all required allocator leases **before supplying any IDs**. Each lease still covers at most 64 counters. This lets replay stop at any record boundary without mistaking the start of a multi-lease request for previously owned IDs. The admission carries the complete contiguous supplied range, including IDs from rejected preparation.

## Bounds and publication

| Budget | Rule |
|---|---|
| Request | At most 256 parts/1,024 welds **and** 16 KiB owned payload; byte cap can reject before count caps |
| Delta | At most 24 KiB owned payload, checked before allocating changed-record vectors |
| Journal record | Slot + request + delta must fit 32 KiB together; individual request/delta limits do not override this cap |
| Journal transport | 64 slots / 1 MiB; reservation, append, replay compaction and prefix release account for payload bytes |
| Receipts | At most 64, additionally capped at 256 KiB of owned request payload; oldest receipts are evicted as a contiguous prefix |
| History | 32 entries / 256 KiB, including owned deltas; 64 dormant parts / 8 dormant builds |
| Candidate | Existing 512 KiB admission budget also charges owned history/request/refit storage |
| Recovery | Existing 2 MiB owned-image budget includes history, pending, receipts and retained journal payloads |

Every retained reference is charged conservatively, even when it shares an immutable allocation with another record. These are logical owned-payload budgets, not allocator overhead or serialized file sizes. A new edit that cannot remain reversible is refused; its history is never silently dropped to force admission.

Publication copies only fixed fields and immutable shared handles. Explicit non-throwing journal constructors/copies satisfy both compilers. The final native and WASM allocation probes intercept ordinary, array and aligned allocation. Refit, undo, redo, pending cancellation/closure and exact prefix release all make zero allocation attempts.

## Verified results

| Check | Result |
|---|---|
| Optimized native application and combined tests build | Pass |
| Native focused run | **154 passed**, no skipped/disabled cases: 133 session/journal/event + 21 cove/navigation |
| Strict WASM shared tests | **133 passed**, zero skipped/disabled, JS exceptions + Asyncify, fixed 64 MiB heap / 1 MiB stack; source hashes unchanged throughout |
| Shipping browser application via CMake | Pass; existing unrelated warnings remain |
| Native publication probe with undefined-behavior and float-cast-overflow checks | Three activations, zero allocation attempts; cancellation/closure/prefix checks pass |
| WASM publication probe | Same three activations and zero allocation attempts; same exception/heap/stack settings |
| Whitespace validation | Pass |

The first strict WASM test build rejected an implicitly generated journal copy constructor after its assignment operator became explicit. Explicit non-throwing copy/move constructors resolved the warning. Both applications, all 154 native cases, all 133 WASM cases and both final allocation probes were then rebuilt and passed. The failed compiler log is retained alongside final results.

See `results.json`, `source-sha256.json`, the checked-in final logs, `wasm-tests-manifest.json`, `no-allocation.cpp` and `run-no-allocation.py`. Large executables remain in `/tmp`; commands below recreate them from the repository.

## Reproduce from the repository root

```sh
nix-shell --run 'bazel build -c opt //:voxy_native //tests:voxy_tests'
nix-shell --run 'bazel-bin/tests/voxy_tests --gtest_filter="GameSession*:SessionJournal.*:SessionEvent*:CoveMovement.*:CoveNavigation.*"'
nix-shell --run 'python3 scripts/validate_session_transactions_wasm.py --sdk /tmp/voxys-emsdk/upstream --node /home/modkin/.nix-profile/bin/node --output /tmp/voxys-refit-wasm-tests-recheck --exception-mode js --asyncify --expected-tests 133'
nix-shell --run 'EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j 8'
nix-shell --run 'python3 docs/validation/salvage/PLAY-02/refit-r01/run-no-allocation.py'
git diff --check
```

The WASM test output directory must be new. SDK/runtime paths are the installed local paths, not downloaded by these commands. No browser captures are needed for this checkpoint.

## Next: real edited-craft launch

1. Add an explicit workshop Launch action using `prepareCoveRefit`, current session/build revisions and the next request sequence. Freeze the submitted design and its mapping while pending. Keep accepted boat/render/player state separate from design preview.
2. Extend the scene preparation adapter to compile the canonical candidate, prepare the player/navigation and functional frames, and reserve a replacement shape/body while the old body remains usable. Refuse unsupported/missing helm/propulsion/flotation or unavailable capacity before destructive work.
3. Define a dock launch/recovery pose using the new assembly root, COM and equilibrium draft. Require the player safely on the dock and the old craft recovered/eligible there. Refuse an attached cable/cargo or incompatible pending job. Do not teleport cargo to manufacture a completed job.
4. Use the existing owned execution domain and drained scheduled/encoded/completed frontier. Stage replacement for a future tick. Before destroying the old body, ensure replacement admission cannot fail; preserve the existing craft on every preparatory refusal.
5. Observe the old body dead, the new body pose, and matching event completion at/after that staged tick. Only then publish GameSession plus render, boat, navigation/player and functional bindings. Track retiring shape/body resources until actual completion; never reuse an in-flight handle.
6. Reset and re-entry must use the accepted edited boat. Removed placements must disappear from sailing render/collision. Refresh the editor from the accepted design; do not let old local undo manufacture a removed loan. Add a deliberate authoritative launch-undo path using the existing reversible transition.
7. Run one real no-screenshot journey: open workshop, make/keep a visible meaningful edit, launch, board and sail the changed craft, reset it, then drain Leave. Assert changed canonical revision, part/weld counts, mass and pose-driven render mapping. Test refusal and repeat launch for no duplicate bodies or charges. Stop rerunning unchanged journeys once they pass.

Full PLAY-02 and all existing parent review/quality/hardware gates remain unchecked. Mark only completed scoped deliverables. Commit only after a full gate passes and its required repository checks pass. Do not replace independent/human or real-GPU evidence with these CPU tests.
