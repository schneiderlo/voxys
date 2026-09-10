# Actual cove archive capture and restoration preflight

2026-09-09. SAVE-04 component progress. SVCE v1 now captures a real paused
LEGO expedition, joining physical motion, player, cable and water to the
existing logical ownership/economy/journal checkpoint. Its reader rebuilds the
saved boat and validates a temporary restored player before any live mutation.
No screenshots. No full task/gate completion or gate commit.

[API, byte layout, identity and lineage contract](../../../../salvage-cove-save-format.md).

## Verified

- Both native and shipping WASM applications build.
- 263 affected native tests pass, zero skips. Includes seven new archive cases,
  existing session/recovery/codec, cove, fixture and matching save regressions.
- All 150 shared tests pass in strict WASM with JS exceptions, Asyncify, fixed
  64 MiB heap and 1 MiB stack. The frozen large-counter archive matches on both
  runtimes: `46757880e6db9f473cab1e3ba0b2a31cb08cb30e50a3cdb3405a66195c5f366d`.
- The archive tests cover towing/loose/broken/banked distinctions, large u64s,
  root velocities, source identity, physical/ownership mismatches, malformed
  re-signed fields, nested lengths/checksums, trailing/oversized/truncated input,
  output preservation, exact retired-parent linkage and rejection of an old
  token after SessionRecovery. Synthetic banked state is a codec fixture;
  actual in-game banking is not certified by it.
- One actual hardware Chrome journey boards, uses the helm, hooks and reels,
  pauses through controls and captures the live tow at tick **467**. The
  4,994-byte archive contains a 4.1724725 m cable, moving boat and independent
  cargo, helm-local player, elapsed water and unchanged 48 material. The reader
  reconstructs the saved boat/player successfully. Repeated capture produces
  identical bytes; corrupt input refuses; accepted state is unchanged. The
  ordinary journey continues through Resume, sailing, release, Reset and Leave.
- A short second hardware journey purchases **two pontoons in one Launch**,
  then pauses the resulting **13-part / 1,275 kg** boat. Its **12,435-byte**
  archive retains paid IDs **35 and 36**, and **0 material**. Reconstruction
  succeeds from original content, without copying current paid render slots.
  The actual payload is then published through the browser mirrored store
  using the real application validator. Closing/reopening the database owner
  returns generation **1**, identical payload bytes, and no repair required.
  Leave drains from pause. This is database connection reopen, not page reload.

Exact runtime reports, archive bytes, hashes and source/package identities are
beside this file. Both browser fixtures use private profiles/servers, preserve
existing user previews, and close when finished. Gameplay uses CDP mouse/key
input; the archive APIs and isolated storage fixture are explicitly invoked
for preparation verification. There is no GameSession mutation or success
injection. No visual or performance acceptance is claimed.

## Retained failures

Initial native attempts corrected the fixture's missing event incarnation,
unsupported aggregate comparisons, and an unsigned-byte/span conversion. The
first golden attempt deliberately printed the new hash before freezing it.
These are not passing manifests. Final native r06 and WASM r02 contain the
shipping sources. The first paid-boat storage test correctly stored/reopened
its archive but incorrectly read `publish`'s `{generation}` result as a scalar.
The corrected test reads `.generation`; the short final paid journey passes.
No product code changed to make that test pass.

## Immediate next work

Finish the actual save/load feature; do not return to visual inspection loops.

1. Introduce explicit world-slot selection before scene/session creation, with
   trusted expected world identity and selected content. Native storage has a
   locked slot directory; browser storage has a locked world key. A missing
   known world must not trigger the fresh 48-material grant. No silent import
   replacement or fallback to an older semantically invalid world.
2. Extract the action-2 reconstruction into an owning load candidate, retaining
   admitted archive, rebuilt scene/boat/player and independently compiled cargo.
   Validate everything possible before retiring the live owner. The original
   loan binding is content/profile-derived; paid slots are newly assigned.
   Never revive loans or pay for existing parts during reconstruction.
3. Provide a fresh-backend initial logical tick. Current GPU backend starts at
   zero while `SessionRecovery::restore` retains the saved tick. Binding requires
   equality with the owned GPU base. Set a validated base before any commands
   are queued; do not reset/backdate logical history or relabel in-flight work.
   Verify large u64/low32 GPU counter behavior, overflow refusal, and first real
   submitted/completed/body/event tick after restore.
4. Admit restored authored-root motion through the mass-frame/body path; derive
   water cells and neutral helm from the rebuilt modules. Restore water clock
   and settings before the first physical tick. Restore loose/towed/broken or
   secured static cargo exactly once. Derive tow anchors/limits from accepted
   winch and cargo definitions and apply saved target length with motor zero.
   Restore the player only against that boat transform and physical scene.
5. Connect native serialized storage work and browser transactions to a bounded
   save coordinator and real controls. Acknowledge only durable publication.
   On recovery, retire the old owner and use SessionRecovery's fresh token,
   initial checkpoint and finalized parent. Publish the paired lineage in one
   generation before admitting new durable commands. Preserve uncertainty on
   post-publication failure; do not use `releaseModelWrittenPrefix` as real I/O.
6. Run actual save→page/process reload→resume using the shipped controls while
   towing, after release, with paid edits and after banking. Prove same identities,
   costs/rewards, motion and controlled settling; old commands cannot pay again.
   Latching, fixed-tick WaterField, native Windows, quota/device-loss/power-loss
   and the remaining parent SAVE acceptance remain open.

## Reproduction

Use repository Nix shell and Node 22. Build the applications normally, then
package `web/` with `build-lego-wasm/bin/voxy_wasm.{js,wasm,data}` in a fresh
private directory. The tested package is `/tmp/voxys-cove-capture-web-r01`.

```sh
bazel build -c opt //tests:voxy_tests //:voxy_native
bazel-bin/tests/voxy_tests --gtest_filter='CoveSave.*:*Cove*:*FixtureRegistry*:*Session*:SaveGeneration.*'
python3 scripts/validate_session_transactions_wasm.py --sdk /tmp/voxys-emsdk/upstream --node /home/modkin/.nix-profile/bin/node --output /tmp/new-archive-wasm --exception-mode js --asyncify --expected-tests 150
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_COVE_PAUSE=1 VOXY_SMOKE_COVE_ARCHIVE=1 VOXY_SMOKE_REPORT=/tmp/new-tow.json VOXY_SMOKE_COVE_PLAYER=/tmp/new-tow-journey node scripts/smoke_integrated_wasm.mjs /path/to/package salvage-cove
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_REPORT=/tmp/new-paid.json VOXY_SMOKE_COVE_ARCHIVE_ONLY=/tmp/new-paid-journey node scripts/smoke_integrated_wasm.mjs /path/to/package salvage-cove
```

The current towing reproduction also exercises the subsequently added storage
fixture. The recorded towing r01 predates that addition and proves capture /
preflight only; the final paid r02 proves actual archive storage and reopen.
