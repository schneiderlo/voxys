# Native builder and named designs

The native acceptance chain is complete: **12 successful stages in r04**, followed by **9 successful stages in the final native r05 continuation**. The continuation passed in **67.456 seconds**. All recorded stages bind a submitted frame and encoded physics tick to later actual completion on the same runtime owner. No screenshots were captured.

The final boat has **12 parts, 969 kg, 44 material and paid IDs35/36**. Its 10 retained starter loans and two purchased bricks preserve exact accepted identity, health, paint, settings and connection records across the saved-world restart. Imported designs do not supply physical IDs, loans or stock.

The library survived process restart at generation6: **TUG revision3**, **SPARE revision2**, **TRANSFER revision1**. TUG and SPARE retain prior-version backups. The imported and reloaded blueprint matches the r04 TRANSFER export byte for byte.

## What ran

| Attempt | Result | Scope and limit |
| --- | --- | --- |
| r01 | Failed scenario; 44.183 s | Three completed stages and real HULL export. The intact starter cradle blocked the proposed brick snap. No brick was kept or bought. The driver was corrected to remove the cradle through ordinary controls. |
| r02 | Missed Keep; 29.626 s | The 90 ms Confirm press did not Keep a valid removal. No authoritative refusal or GPU/process error appeared. The cause remains unproven. |
| r03 | Driver probe failure; 5.250 s | A separate windowless mapping client reported released buttons while the game accepted Confirm. This auxiliary assertion was removed completely. It does not show a game defect. |
| r04 | 12 stages passed, then real UI failure; 174.252 s | Actual controller pan/zoom, cradle removal, two bricks, held-confirm/focus protection, grouped paint/Undo/Redo, OSK Save/Copy/Rename, Update/backup Restore/Load and export passed. Corrupt import was correctly refused; its full Imports folder path triggered HUD truncation. The renderer now allows two subtitle lines, with an actual-path CPU regression. |
| continuation-r01 | Passed; 9 stages, 67.456 s | Final r05 native: real Load, visible corrupt refusal, valid import, purchase, joined durable save, one exact-world restart, library Load/export comparison and final unchanged save. Both processes exited by requested SIGTERM, without force kill or error lines. |

The final continuation used one deliberate 200 ms press per non-repeating button. It did not retry commands or relax any success condition. The previous 90 ms failure remains unexplained.

## Persistence distinction

The r04 world had **not been checkpointed** when the UI assertion stopped it. Its named library and exported designs were already durable. The continuation started a fresh world using that same library directory, then loaded TUG through the actual native menu. This reused the successful design work without repeating the construction and naming sequence. It did not restore the unsaved r04 world or inject runtime state.

The continuation report records both world namespaces, the exact r04 source-report hash, the TRANSFER file/blueprint hashes and the unchanged generation5 library rows before import. Import advanced the library to generation6. The first real Launch purchased the two bricks for four material. The subsequent restart used the newly saved **same** world and preserved all accepted physical ownership.

[The final report](continuation-r01/summary.json) contains the stage/completion proofs and exact owned-record hashes. [Archive identities](continuation-r01/archives.json) bind the paired final library/world generations and all exchanged files. HULL contains the 10 retained starter parts; TRANSFER, LOADED and REOPEN contain the same final 12-part blueprint. Copies are read-only evidence; original save resources were not edited.

The final native binary SHA-256 is `1249fb516605b706d5e903b0679ca34f40f832b0b21be01ee08f0203b8278781`. The main checkpoint record owns the mandatory-suite and publication result.

## Reproduction

Run inside the project Nix environment with X11 access and permission for the temporary Linux uinput controller. The driver opens only its own game window at960×800 and releases the controller/processes on exit. Use new output and storage directories for a fresh complete run:

```sh
python3 scripts/validate_native_builder_tools.py --binary build-native-save-host/bin/voxy_native --output /absolute/new/native-builder --storage-root /absolute/new/native-storage
```

The exact bounded continuation command executed was:

```sh
python3 scripts/validate_native_builder_tools.py --binary build-native-save-host/bin/voxy_native --output build-workshop-tools-r01/native-builder-continuation-r01 --storage-root build-workshop-tools-r01/native-storage-r04 --continue-from build-workshop-tools-r01/native-builder-r04
```

That continuation requires the retained library at generation5 and no world checkpoint yet. Its successful run advanced those files, so the current live directory is final evidence rather than an unused continuation input. The driver deliberately refuses to replay it as though it were still generation5. The full fresh command recreates the controls sequence without altering the accepted evidence.
