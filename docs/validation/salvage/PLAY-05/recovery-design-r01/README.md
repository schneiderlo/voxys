# PLAY-05: protected recovery designs

Recorded 2026-09-10 on `codex/salvage-implementation`, based on G00 commit
`7f28fabfabf3d63726f6cfa1001c3ce1ed557911` plus the existing implementation tree.
This is a scoped component. Full PLAY-05, SAVE-04 and their gates remain open.
No screenshot was captured or reviewed. Terrain and authored LEGO assets are
unchanged; actual journey observations require `terrainSurface == "lego"`.

## Player behavior

At the dock workshop, **Rebuild starter / H** now protects the accepted custom
boat design before replacing its physical parts. Its fitted paid parts enter
owned storage exactly as in the prior starter-recovery component. The backup
and boat replacement are saved together before Resume is unlocked.

**Load recovered design / K** brings the selected layout into the editor. Launch
uses existing starter pieces and stored paid parts, charging normally for any
missing paid replacements. It grants no free parts. The existing design library
can save/export the loaded layout. **Next recovery / J** cycles backups; browser
also has Previous. **Remove recovery design / F** removes just the selected
backup, pauses and saves. Owned parts and separate library entries are unaffected.
Removal is unavailable while the editor differs from the accepted boat.

Keep up to four distinct custom designs. The installed original starter is not
added to this list. Rebuilding it repeatedly retains previous custom designs.
Rebuilding a recovered custom boat deduplicates its normalized design even after
new starter IDs have been issued. A fifth distinct design refuses the rebuild;
it never silently replaces the oldest backup. Load/export and explicitly remove
an unwanted backup before trying again. The selection itself is transient;
reload selects the newest retained design.

## Implementation and authority boundary

- `cove_save.*` normalizes SVBP design bytes by placement, definition, paint and
  settings, then remaps/sorts weld endpoints. Limits are four designs, 32 KiB
  each, 32 parts and 64 enabled welds. Validation checks canonical re-encoding,
  selected content and duplicate bytes. Design bytes contain no live IDs,
  health, ownership, inventory, entitlement or physics.
- SVCE schema 3 inserts a recovery count and length-delimited designs after the
  22-byte harbor section. Profile 0/1 remains explicit. No backups emits the
  unchanged v1/v2 encoding. SVSC/SVJB and outer SVSG versions are unchanged by
  this component. See [complete field order](../../../../salvage-cove-save-format.md).
- `Application::configureCoveLaunch` stages the backup alongside the actual
  replacement. Capacity rejection happens before shape upload/body replacement.
  Successful publication swaps the prepared vector without allocation. Aborted
  preparation leaves the accepted vector untouched.
- Restore admits the archive before copying backups into the new live owner.
  Loading delegates to `CoveWorkshop::loadBlueprint`; ordinary quote/refit and
  trusted ownership determine which parts can be used. Removal requires a ready
  storage owner and an editor matching the accepted scene, then enters the same
  joined pause and exact whole-archive acknowledgment path as starter rebuild.
- Browser UI and native workshop keys expose these actions. The existing browser
  save coordinator's input limit includes the bounded backup section. Failed
  storage keeps Resume blocked for explicit retry; no mission reward is paid.

## Verification

Native journey r01 passes **12 recorded stages** using real keys sent only to
its own X11 game window. It copies the actual paid-boat save from
`../rescue-r01/native-r05/final-slot` into an isolated writable root. That source
remains unchanged. No checkpoint bytes or game state are synthesized.

The source boat has 13 parts, mass 1275 kg, paid IDs 35 and 100, zero remaining
material and an accepted unbanked job. The journey verifies:

1. Rebuild protects one design and stores both purchased pontoons.
2. Actual directory permission failure prevents save publication and freezes
   Resume/rebuild/movement; restoring permission and F10 complete the same save.
3. A process restart restores the backup, stored identities and mission state.
4. Rebuilding the original starter retains the previous backup unchanged.
5. K loads the 13-part/1275 kg layout with zero charge/refund. F cannot close this
   unlaunched design. Launch reuses paid IDs 35/100 and empties storage.
6. Restart retains that boat and its backup. Rebuilding the recovered boat yields
   the exact same normalized digest: layout, welds, paint and settings match.
7. Explicit backup removal saves and survives another process restart; both
   stored paid parts, materials and the unique mission cargo remain unchanged.

Browser r01 passes **10 stages plus drained Leave** through actual buttons and
IndexedDB reload. It starts from an isolated clone of a real older pre-hoist
save (`d29b3950f727db3cfbc79490c752187f`), with paid beam ID 35, 96 material and
banked cargo. The same protect/rebuild/reload/load/launch/deduplicate/remove/reload
sequence preserves the banked generator, money and paid identity. It checks
one recovery observation tick after each reload.

Fresh browser r02 passes **28 recorded stages plus drained Leave**. It starts
from a new world, constructs a paid boat, accepts the generator job, hooks and
hauls the actual cargo, banks it, saves/reloads, then completes the same protected
design recovery sequence. Paid beam ID 35, 96 material and the unique banked
generator survive every rebuild, design launch and reload.

Focused checks pass:

- Bazel: **13 CoveSave**, **37 cove mapping/movement**, **128 GameSession** cases.
  GameSession and final CoveSave runs are cache hits after earlier passing runs;
  cove mapping/movement ran against the final test source, including hardware
  physics regressions. Raw logs retain the actual cache status.
- CMake: both application and cove test targets build. Three focused CPU cases
  rerun and pass: replacement starter mapping, exact stored part reuse and
  normalized recovery with paint/settings/welds and original paid condition.
- Node: **17 save-coordinator** cases and **21 preview UI** cases pass. The new UI
  case checks backup selection text, disabled removal for an unlaunched design,
  pending controls and empty-list behavior. Node reports two script groups.
- Bazel native, CMake native and configured WASM applications build. The actual
  native journey uses the CMake binary; browser journeys use `web-r01` below.

Capacity/deduplication and malformed/duplicate archive refusal are covered by
codec tests. The 12-part physical design test loads through the real editor and
refit/compiler path with purchased part health/paint/settings preserved. These
checks do not claim a live four-slot capacity journey or general fracture.

## Reproduce without screenshots

From the repository root, with the existing Nix dependencies and GPU available:

```sh
nix-shell --run 'bazel test -c opt //tests:cove_save //tests:cove_player //tests:game_session'
nix-shell --run 'bazel build -c opt //:voxy_native'
nix-shell --run 'cmake --build build-native-save-host --target voxy_native cove_player_tests -j8'
/home/modkin/.nix-profile/bin/node --test scripts/test_cove_saves.mjs scripts/test_salvage_preview.mjs
```

Native actual journey (use new output/root names each run):

```sh
nix-shell --run 'python3 scripts/validate_native_cove_recovery_design.py --binary build-native-save-host/bin/voxy_native --source-slot docs/validation/salvage/PLAY-05/rescue-r01/native-r05/final-slot --storage-root build-new-recovery-saves --output build-new-recovery-journey --permission-failure'
```

Browser package for this run:
`build-recovery-design-snhtkn8t/web-r01`, containing current web files and the
configured `build-lego-wasm/bin/voxy_wasm.{js,wasm,data}`. The directory is ignored
scratch; `manifest.json` records its bytes and matching built binaries. To
rebuild, use the existing Emscripten configuration and a workspace TMPDIR inside
`nix-shell`, then package web files and the three output files together.

Fresh browser journey:

```sh
TMPDIR=/tmp VOXY_TEST_CHROME=/usr/bin/google-chrome VOXY_SMOKE_GPU=hardware \
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_COVE_RECOVERY_DESIGN=/absolute/new-evidence-dir \
VOXY_SMOKE_REPORT=/absolute/new-report.json \
/home/modkin/.nix-profile/bin/node scripts/smoke_integrated_wasm.mjs \
/absolute/packaged-web-dir salvage-cove
```

The legacy check additionally uses an isolated copied smoke profile, fixed port
38210 and `VOXY_SMOKE_RESUME_WORLD=d29b3950f727db3cfbc79490c752187f`. Never use a
personal browser profile. Run GPU journeys sequentially with GPU unit tests.
Drivers use ordinary controls and read-only state, never runtime setters.

## Retained failures and limits

- `codec-r01.log`: strict compiler warning on unchecked vector indexing in a
  fault test; changed to checked access.
- `tests-r02/r03.log`: former unknown-version test used 3, now an admitted schema;
  it now uses 4. The first recovery-mapping test incorrectly expected no created
  IDs at all, despite a legitimately new weld. The final assertion checks every
  physical part against exact previously owned IDs. Layout/configuration and
  paid condition checks passed independently. No runtime behavior was weakened.
- Existing clang warnings about `WorldPosition` comparison are visible in the
  build log; this component does not change that type.
- No complete loss-at-every-stage certification, abandoned-fragment accounting,
  cutting recovery, second job, periodic autosave, multiplayer, controller,
  Windows storage, performance target or visual/human acceptance is claimed.
  Export uses the existing library; this journey does not retest file downloads.
- Continue the authoritative `GAME_IMPLEMENTATION_TODO.md`, especially the full
  PLAY-05 loss/exploit matrix and MECH cut-fragment prerequisites. Do not mark
  PLAY-05 or a gate complete from this scoped report. Commit only when the whole
  gate and required repository checks pass. Prior evidence manifests are frozen.
