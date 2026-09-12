# Native builder and named design tools

Ten new source/test/driver files are listed and hashed in `native-tools-hashes.json`. Root owns Application, rendering, input, shared build registration, app runs and publication. Parked two-job files are unchanged.

## Library boundary

`game::expedition` provides the strict UTF-8 name validator, browser-compatible three-field exchange envelope and checksummed native library collection. The library holds at most 32 current rows and one prior version per row. Names are at most 96 UTF-8 bytes. IDs and revisions belong only to this library; no physical ownership or inventory is imported.

`platform::NativeDesignLibrary` owns one worker and one bounded pending operation. `open()` is asynchronous. The application calls `poll()` on its main thread, including while the menu is hidden. `saveNew`, `update`, `duplicate`, `rename`, `restore`, `remove`, `refreshImports`, `importFile` and `exportFile` return admission only. Published rows/generation advance only after the actual durable store acknowledgment. Full blueprint/catalog validation calls the host callback on the main thread, never the worker. Update/read actions capture row ID and expected revision.

The native host reuses the save store lock, generation CAS, atomic publication and paired recovery semantics in a separate `Designs/Library` namespace. Normal disk refusal keeps the previous collection. Conflict, uncertain durable state and recovery-required failure close mutation authority until game restart. `isClosed`, `failed`, `ready`, `busy`, `storageIssue`, `message`, completion operation/success and `selectedId` remain inspectable.

Exports use an explicit deterministic file under `Designs/Exports`. Imports list at most 64 regular single-link `.voxy-design.json` files, bounded to 1024 directory entries, with no-follow reads and strict basename validation. Existing exports are never overwritten. Linux is implemented; other native platforms report unsupported storage explicitly.

## Menu boundary

`NativeWorkshopMenu` stays at Application lifetime. `tick` consumes opening, modal and closing frames; the host must gate gameplay/global save input and reset held input at ownership transitions. It exposes `pageName`, `menuContent` and read-only library state. It accepts framebuffer width/height and a logical-to-framebuffer pointer ratio, defaulting to 1×.

F2 or controller Menu opens the tools. Native pages expose selection/grouping, moves/rotations, chosen catalogue part and explicit Add, paint, module settings, camera, draft/launch history and all library operations. Naming accepts UTF-8 character events and a 40-key controller-operated letter grid; shoulders switch between letters and Done/Backspace/Cancel. No naming key dispatches game actions. Import does not load; Load is explicit, and Launch retains the authoritative ownership/cost transaction.

## Verification and limits

The shared/native library targets passed 14 focused CPU cases; their independent review is `native-library-review.md`. Root reports all seven native menu CPU cases passing, including 2× pointer/layout agreement; these tests do not claim controller, font-pixel or app acceptance. The final native application is owned by root.

The native acceptance chain completed through r04's 12 successful stages and the final native r05 continuation's 9 stages (67.456 s). The real r04 subtitle clipping was fixed and verified visibly. The continuation reused its durable named library and exact TRANSFER file in a clearly recorded fresh world, then purchased the two parts, saved that world, restarted that same saved world once, and verified exact physical/library ownership. It did not restore the unsaved r04 world. The full history, original failures and final binary/archive hashes are in `docs/validation/salvage/UX-01/builder-tools-r01/native/README.md`.

The journey uses X11 keys plus one temporary Linux uinput controller, no action injection or screenshots. It checks two 1×2 bricks after explicit cradle removal; grouped paint and Undo/Redo; one pan and one zoom; held-button close/focus disarming; OSK Save/Copy/Rename/Update/backup Restore/Load; real exchange files and corruption refusal; Launch, durable save and exact restart. Every recorded stage binds its submitted frame/encoded physics tick to later completion on the same runtime owner. Unexpected exits, forced shutdown and runtime/GPU errors fail. No further app runs are planned; root owns the required full suite and publication.

Run only when root grants the GPU/focus/controller lane, using new directories:

```sh
python3 scripts/validate_native_builder_tools.py --binary /absolute/final/native/voxy_native --output /absolute/new/native-tools-journey --storage-root /absolute/new/native-tools-storage
```

The window is 960×800. The action budget is 420 seconds, followed by bounded cleanup. Expected named rows are TUG revision3, SPARE revision2 and TRANSFER revision1 at library generation6. Expected physical parts are 12, paid IDs35/36, all 10 retained starter blueprint records preserved, stock charged only for the two chosen bricks. The script derives exact mass and material price from the authoritative previews and compares complete accepted part/connection records across restart. File-transfer evidence preserves both the actual export and the read-only checkpoint payload; it never modifies a world archive.

## Native r01 retained scenario failure

The first actual native run stopped after 44.183 seconds: fresh world and both real controller camera gestures completed; HULL exported through the menu. The harness attempted to keep the intact starter cradle while auto-snapping a 1×2 brick. The game correctly retained an invalid preview with “No other free socket fits this part.” No brick was kept or purchased. The owned process terminated with requested SIGTERM (−15), no force kill and no runtime/GPU error lines. Exact report/log/executed driver remain in `build-workshop-tools-r01/native-builder-r01`.

The revised scenario removes Cargo cradle and Keeps that real edit before exporting HULL and adding two bricks. It now compares 10 retained loans plus 2 paid parts (12 total). This changes only the acceptance sequence; source rules, snapshot setters and physical archives are untouched. That revised scenario reached the later r02–r04 observations below; the final continuation completed its remaining persistence checks.

## Native r02–r04 observations

- r02 (29.626 s): a 90 ms Confirm press did not Keep a valid cradle removal. Exact cause remains unproven; no refusal or GPU/process error.
- r03 (5.25 s): an auxiliary, windowless mapping client reported released buttons while the game accepted the first Confirm. That driver-only assertion was removed completely; it provides no evidence of a game defect.
- r04 (174.252 s): one deliberate 200 ms press per non-repeating button passed the previous Keep, both bricks, held-button/focus safety, grouped paint/Undo/Redo and all Save/Copy/Rename/Update/backup Restore/Load operations. The invalid import was correctly refused with unchanged generation/rows, but the HUD flagged truncation because its full Imports path was limited to one subtitle line. Root completed the bounded renderer fix and focused layout case. This attempt stopped before import/Launch/world save/restart; the final continuation completed those stages.

All four owned processes exited by requested SIGTERM (−15), without force kill or runtime/GPU error lines. Each attempt retains its exact executed driver, report and process log under `build-workshop-tools-r01/native-builder-rNN`; reviews distinguish harness errors from the actual layout finding. No images were captured.

## Final continuation

Final r05 native passed all 9 remaining stages in 67.456 seconds using existing `native-storage-r04/Designs` and a fresh world (the r04 world had no checkpoint). It Loads the durable TUG through the actual menu, compares a new LOADED export with the exact r04 TRANSFER file, shows the correct corrupt-file refusal without clipping, imports for library generation6, and Launches for 4 material. Final state: 12 parts, 969 kg, material44, paid IDs35/36. One checkpoint/restart and final save preserve all accepted part/connection bytes; TUG3/SPARE2/TRANSFER1 and backups reload exactly. Both processes close by requested SIGTERM−15 without force kill/error lines. No unbroken one-process/world continuity is claimed across r04 and this continuation.

Docs-only explicit inventory: `build-workshop-tools-r01/native-docs-stage-candidates-r01.json`. The final native binary SHA is `1249fb516605b706d5e903b0679ca34f40f832b0b21be01ee08f0203b8278781`. The combined mandatory suite and final publication are tracked by root.
