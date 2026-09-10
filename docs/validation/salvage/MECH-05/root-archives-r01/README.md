# Physical fragment archive codec

This completes the portable codec component under D32. It does not enable
cutting, physical fragment capture or multi-body restoration. MECH-05, PLAY-05
and all affected gates remain open. No screenshots or gate commit.

## Implemented contract

`CovePhysicalSave` now has an explicit `boatRoots` vector, stable `controlPart`
helm ID and `playerRoot` key. The physical SVCE v4 extension stores every
root's authored-origin motion. `AssemblyMassPlan` compiles the accepted build's
enabled weld graph to verify the exact root keys and canonical order. It does
not trust the archive's count or merely check that IDs belong to the world.

The root key is the least member part ID, scoped by the accepted build and
revision. The controlling helm may belong to a root other than zero. The
legacy `boatMotion` field must match its root-table entry exactly. The rider
may walk on another fragment, but Helm mode requires the enabled controlling
helm's root. Ashore state requires a zero rider root. Aboard feet remain in
authored **build** coordinates; live restoration must use the selected root's
transform and inverse `buildFromRoot` transform, then validate actual support.

All root motions share the existing completed-tick/logical checkpoint join.
The saved winch part resolves its own root and may be separate from the helm.
No GPU handles, pointers, guessed root ordinals, forces or impulses are stored.
The current four-line harbor profile requires detached lines for multi-root
archives; installed-but-detached service is valid.

An explicit table selects v4, even for a single root. Without a table, the
unchanged v1-v3 writer paths require one compiled root and zero new binding IDs.
Old playable archives retain their bytes. A fragmented checkpoint cannot be
encoded or re-signed as a legacy single-motion archive. Counts and remaining
bytes bound allocation before reading any table. Thirty-two root records fit
the existing 4096-byte physical allowance; the whole-archive size ceiling,
logical SVSC/SVJB formats and outer storage SVSG v1 remain unchanged.

See [the complete wire format](../../../../salvage-cove-save-format.md) for
field order, offsets, validation and host responsibilities.

## Evidence

- 224 selected Bazel cases pass: 21 cove archives, 11 assembly compiler,
  12 transactions, 136 session, 3 save generations and 41 cove/player cases.
  The 151 unchanged session/transaction/generation results were valid Bazel
  cache hits; 73 cases executed in this run. Raw XML/logs are in `checks/`.
- All 21 archive cases pass in the CMake build. Eight new cases cover actual
  retained cuts, remote roots, a winch separate from the controlling helm,
  all three legacy downgrade attacks, 17 invalid root/control/rider/motion
  cases, off-helm riders, disabled controls, all 32 roots plus four backups,
  resigned counts/duplicate records, a non-minimal member key and fresh-writer
  recovery with an exact retired parent. No adapter calls occur.
- All 187 shared authority/save cases pass in actual JS-EH/Asyncify WASM,
  with a fixed 64 MiB heap and 1 MiB stack. `wasm-r01/manifest.json` records
  the executed runner, commands, toolchain and source hashes.
- The full native application builds in Bazel and CMake; the configured WASM
  application builds. Ordinary gameplay still emits v1-v3 because its live
  owner remains single-root.

Native and WASM assert the same complete-envelope fixtures:

| Fixture | Bytes | SHA-256 |
|---|---:|---|
| Two cut roots, remote winch, controlling helm on root one | 1878 | `dd3edb534176d1444915ba6b981e1a3404c32f003c6915c60623c31c00293882` |
| 32 roots, four recovery designs, installed detached harbor | 8862 | `e909ab20940e0e820cec4ff2f3c44f451b49678c9a5277ac83f4e57fb4772bda` |

The existing v1 golden `46757880e6db9f473cab1e3ba0b2a31cb08cb30e50a3cdb3405a66195c5f366d`
still passes. Existing harbor/recovery archive cases and actual-content cove
restore preparation pass. These codec tests do not attest to disk durability,
GPU activation or playable fragmented reload.

## Reproduction

Run from the repository root:

```sh
nix-shell --run 'bazel test -c opt --jobs=8 //tests:cove_save //tests:assembly_compiler //tests:session_transactions //tests:game_session //tests:save_generation //tests:cove_player'
nix-shell --run 'cmake -S . -B build-native-save-host'
nix-shell --run 'cmake --build build-native-save-host --target voxy_native cove_save_tests cove_player_tests -j8'
nix-shell --run 'ctest --test-dir build-native-save-host --output-on-failure -R "^cove_save\."'
```

For WASM use `scripts/validate_session_transactions_wasm.py` with a **new**
`--output` directory, `--sdk /tmp/voxys-emsdk/upstream`,
`--node /home/modkin/.nix-profile/bin/node`, `--exception-mode js --asyncify`
and `--expected-tests 187`. Set TMPDIR to an existing workspace scratch
directory inside nix-shell. The executed runner and exact command list in
`wasm-r01/` are authoritative; the script's historical default count is stale.

The working build directory was `build-cove-root-save-3zd0h75i`. Application
WASM uses `/tmp/voxys-emsdk/.emscripten` and the configured `build-lego-wasm`
CMake tree. No live user preview or storage slot was replaced for this change.

## Retained failures

Initial test compilation omitted the catalog argument to the existing cut API
and used the wrong aggregate field order for harbor state; both were corrected.
The new CMake target required reconfiguration before it could be built.
GCC's strict optimized bounds warning rejected the corruption test's unchecked
iterator copy; the test now uses checked `.at()` accesses. No warning was
disabled and no acceptance condition was removed. Early logs remain in `checks/`.

## Next required work

1. The scoped wave-clock correction and 29-stage browser tow/hoist/reload now
   pass; see [the correction](../../SIM-05/cove-clock-r01/README.md). The old
   terrain/cable failures remain retained. General WaterField and performance
   acceptance stay open. Continue physical root ownership below.
2. Give the cove a bounded owning collection of live root shapes, bodies and
   joined observations. Reserve/admit all children before retiring parents;
   publish the cut only after all actual execution evidence arrives.
3. Generalize `CoveRestoreCandidate`, player/deck transforms, part rendering,
   winch/harbor endpoints and rescue to the complete root set. The current
   candidate still uses `compileBuild` and rejects multi-root builds; startup
   still rejects physical v4. Do not merely relax that version check.
4. Capture every root at one paused joined tick into this table and restore
   every motion through its compiled mass frame. Validate rider geometry before
   admission, preserve the exact retired-parent transaction, and require durable
   owner publication before activating any restored body.
5. Enable v4 host admission and cutter controls only with the corresponding
   owner/capture/restore paths, remote-fragment recovery and native/browser
   cut/save/reload/rescue journeys. Preserve every paid/loan identity once;
   archive codec acceptance cannot stand in for those live tests.
