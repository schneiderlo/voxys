# Playable browser expedition save and reload

2026-09-09. SAVE-04 component complete. The browser cove now saves a paused
expedition and reloads its actual physical world. This is implementation
progress, not completion of SAVE-04 or G04. No full gate passed; no gate commit.
No screenshots or visual/performance acceptance.

## What the player can do

Open `?experience=salvage-cove`, build and launch a boat, Pause, then use
**Save expedition**. Success appears after the browser storage transaction
commits. Bookmark the resulting address, which contains the world ID. Reload
that address on the same browser profile and origin to load the last checkpoint.
The loaded game starts paused; Resume continues play. Leaving does not autosave.
A selected missing/damaged/incompatible world fails visibly instead of creating
a fresh world and granting 48 material. Native save controls are not connected.

## Implementation contract

- `CoveRestoreCandidate::prepare` owns the validated SVCE archive, reconstructed
  boat/scene/player, independent cargo and derived tow configuration. It uses
  original installed content, trusted original loan bindings and the catalog;
  paid render slots and transient handles are reconstructed. No backend or
  authority is created by candidate preparation. The catalog outlives it.
- `Application::stageCoveResume` stages bounded source bytes before init. Browser
  `voxy_stage_cove_resume` and Emscripten `noInitialRun`/`callMain` prevent a fresh
  game from starting before the selected world is validated. Its saved tick
  initializes a fresh WebGPU world; live rebasing and nonzero CPU fallback refuse.
- `SessionRecovery::restore` creates a new owner/token/epoch and retires the old
  lineage. Action 3 returns the paired initial/retired archive. The browser host
  publishes it under its lifetime exclusive world lock, waits for transaction
  completion, then acknowledges its exact whole-archive SHA through action 4.
  Action 5 revokes ownership. No input/physics time advances while waiting.
- The application reconstructs the saved boat and cargo motion through actual
  mass frames, water settings/time, player/camera, and a neutral saved-length
  rope. Banked cargo is static with no buoyancy driver; broken tow is inert.
  Exactly one neutral tick admits the bodies/rope, and actual body/rope/event/
  session completion joins before the world reports Ready and Paused.
- Every cove construction and job command now uses the current recovered epoch.
  Continuing to use Command's epoch-1 default caused a real post-load launch
  refusal; the successful journey exercises the fix through epoch 4.
- `web/cove_saves.js` owns startup recovery and manual saving, with one pending
  publication, late-completion/page-close fencing and explicit error text.
  Existing `expedition_store.js` supplies strict current/mirror transactions.

[Full byte layout, host APIs and lifecycle](../../../../salvage-cove-save-format.md).

## Executed checks

- Native and shipping WASM applications build. **311 affected native tests**
  pass, zero skips: cove, save/recovery/session, fixtures, storage generation and
  real GPU authored shapes. These include owning restore preparation and fresh
  initial tick behavior across low32 wrap and JavaScript's precise-u64 boundary.
  The large-tick test crosses `2^53` on the GPU; a live rebase and near-u64-max
  initialization are rejected. Normal zero-base initialization remains usable.
- **8 coordinator/UI tests** pass: delayed paired-lineage acknowledgment,
  publication failure, missing/invalid selected worlds, page closure, pause-only
  manual save, bounded pending operation, and failed/closing save status.
  These use controlled storage fixtures; they are not real disk fault tests.
- **16 existing preview UI tests** pass.
- One real hardware Chrome journey passes **17 recorded stages**, **three page
  reloads**, and drained Leave. Actual button/key input drives construction,
  jobs, boarding, helm, hook, reel, save and release. Read-only JSON verifies
  results; there are no game-state setters, injected successes or images.
  Browser errors and uncaptured GPU errors are empty.

| Checkpoint | Saved tick → loaded tick | Epoch | Boat | Paid IDs | Material | Cable |
|---|---|---|---|---|---|---|
| First pontoon | 195 → 196 | 1 → 2 | 12 parts / 1,155 kg | 35 | 24 | Loose |
| Purchase after load | 208 → 209 | 2 → 3 | 13 parts / 1,275 kg | 35, 100 | 0 | Loose |
| Accepted job, towing | 461 → 462 | 3 → 4 | 13 parts / 1,275 kg | 35, 100 | 0 | 5.26532 m |

The different second paid ID proves the recovered allocator does not reuse the
old reserved range. All reloads preserve world, logical revision, inventory,
paid identities, cargo/job state and water time; each gets a fresh observation
incarnation and exactly one settling tick. Boat/cargo displacement stays below
0.2 m in the tested snapshots. Towing reload retains the aboard player at the
helm, neutral motor, and cable length. Actual reeling afterward shortens it to
4.73199 m. Release works, and the restored boat sails at 2.52685 m/s before Leave.
An earlier successful two-reload journey is retained separately.

The browser runs Chrome 152.0.7977.82 with hardware Vulkan on this Linux host;
native GPU logs identify AMD Radeon 890M / RADV STRIX1. These are headless
functional runs, not visible frame-time or art acceptance. Each uses a private
browser profile/server and closes afterward; no existing user preview changes.
Test-world addresses are evidence, not persistent player saves.

`manifest.json` identifies source files, tested package and retained evidence.
Only player-facing help text and its optional visibility changed after the final
browser journey; the final UI/coordinator tests include that behavior. The
shared SVCE/SVSC codec did not change: the prior 150-case strict WASM codec run
remains historical evidence and was not misreported as a new run here.

## Retained failures

- Browser r01 loaded correctly, but its driver clicked Resume before startup UI
  became available. The driver now waits for a visible, enabled, unobstructed
  real button before clicking.
- Browser r02 exposed the actual recovered-epoch command bug. Source commands
  now carry the active epoch for construction, undo/redo and jobs. r03 passes
  purchases through two reloads; r04 adds real accepted-job/towed-load recovery.
- The restricted-sandbox Node test invocation reported only a file-level case,
  without executing the named test workers. It is not counted. The retained
  coordinator log comes from the permitted unrestricted Node runner and lists
  all eight executed cases.

## Next work; full save gates remain open

1. Connect the existing native Linux store to a serialized host worker and actual
   controls/startup selection. Reuse this paired lineage protocol; never release
   the world lock or claim a save before fsync/publication completes. Native
   main currently has no connected save-slot UI/CLI. Preserve pre-init staging
   and keep failure recovery distinct from fresh-game creation.
2. Add usable browser world listing/selection and validated world export/import.
   Current selection is the saved URL; keep the 8-world bound. Missing selected
   slots must stay errors. Imports must validate full content/roles/lineage and
   physical candidate before replacing any stored generation. Do not substitute
   the blueprint library: it stores design intent, not expedition progress.
3. Integrate durable delivery acknowledgment. Current harbor delivery can commit
   a **volatile** game transaction and says “delivered”; it is not yet a durable
   reward. Join the physical secured-cargo tick, publish the accepted state and
   receipt, and only then display durable success. Preserve a clear unsaved
   state on failure and prevent duplicate rewards across restart/retry. Never
   call `releaseModelWrittenPrefix` as a disk acknowledgment; it is a RAM fixture.
4. Verify actual physical delivery/banked reload, broken-tow reload, release
   checkpoint, storage-full/interruption/competing-tab behavior with the live
   application and native process restart. Unit/synthetic banked fixtures and
   older isolated IndexedDB fault tests do not certify these live paths.
5. Implement missing latching and fixed-tick WaterField dependencies, Windows
   persistence, journal policy/compaction and remaining recovery acceptance.
   Do not mark SAVE-02/03/04 or G04 complete based only on this browser component.

Stay on playable construction/exploration/salvage work. Do not return to repeated
visual captures. Update the plan as concrete components pass. Run repository
commit checks and commit only when an entire declared gate succeeds.
