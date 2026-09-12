# Native menus and safe world switching

Native acceptance is complete through **7 retained prefix records, 7 final continuation records, and 5 targeted handoff records**. These are composed results from the runs below, not a claim that the earlier failed runs passed. No screenshots were taken. The combined mandatory suite and publication are reported by the parent checkpoint.

The final two runs used native r06, SHA-256:

`a9fa9ae71143db09eec74d1a55bf25a33e694ea6a4998d7af970b674f504a40b`

| Run | Result | Time | Evidence |
| --- | --- | ---: | --- |
| Native r02 | 7 completed records; later Resume input did not transition | 87.862 s | [Summary](nativechecks/native-ui-r02/summary.json.gz), [process log](nativechecks/native-ui-r02/process-1.log.gz) |
| Continuation r04, final r06 binary | **Passed 7 records** | 39.227 s | [Summary](nativechecks/native-ui-continuation-r04/summary.json.gz), [driver log](nativechecks/native-ui-continuation-r04/driver.log.gz) |
| Targeted handoffs r01, final r06 binary | **Passed 5 records** | 25.358 s | [Summary](nativechecks/native-ui-handoffs-r01/summary.json.gz), [driver log](nativechecks/native-ui-handoffs-r01/driver.log.gz) |

The retained prefix covers actual controller Job, Map, Inventory and Help pages, 150% text, high contrast, stick speed, both deadzones, camera inversion, hold/toggle alternatives, and a controller key picker. Jump was rebound to T and exercised through a real keyboard event. The original world was then durably saved.

The final continuation resumed that exact save and settings. It used confirmed **New**, saved its own throwaway world, and used confirmed **Load** to return to the original within the same process. It then entered a second RAM-only Test, used actual **Quit → Confirm**, and exited naturally with code 0. The original saved envelope remained byte-exact. A new process restored the original owned design and preferences. Its owned cleanup used SIGTERM (-15). Both process logs had no error lines or forced kill.

The final handoff run checked **Job → Resume** while preserving the Job page. Only fresh authoritative permission enabled Accept; the job remained available and was not accepted. A real Test → Return restored the paused workshop. **Menu → Return to building** resumed it without closing it, preserving the captured workshop fields. Actual Quit exited naturally with code 0, no errors and no forced kill.

The original native boat remained **11 loan parts, 1035 kg, 48 material and 0 special machinery**. The native trial also contained 11 parts; its separate prototype identities were not purchases. The browser journey supplies the complementary edited 10-part trial. Native runtime world/build/revision/root identities are bound to the parsed saved archive after each restore; owned part, connection and settings records are compared exactly. New incarnations and runtime body handles are permitted. Successful normal resume may republish a recovered archive, so unchanged owned records and unchanged save envelopes are asserted at their appropriate boundaries.

Each recorded stage binds its observed owner and mode to later actual renderer and physics completion. Workshop captures require explicit workshop mode and zero submitted scenery/dock draws. The Blit path's `sceneSunShadows` field is a last-use flag and is not treated as current workshop presentation state.

## Retained failures and focused corrections

| Attempt | Recorded outcome and cause | Evidence |
| --- | --- | --- |
| Native r01 | Failed at startup after 2.052 s, 0 records. The actual guide title was truncated. The renderer now permits two title lines; exact world and menu text passed the large-text layout cases. | [Summary](nativechecks/native-ui-r01/summary.json.gz), [layout XML](nativechecks/actual-guidance-layout-cpu-r09.xml.gz), [correction](nativechecks/native-readability-correction-r01.json.gz) |
| Native r02 | Failed after its 7 completed records. An enabled Resume-and-build Confirm did not transition. The cause remains unexplained. Later diagnostic traces accepted actions 91 and 60; no timing or physics cause is asserted. | [Summary](nativechecks/native-ui-r02/summary.json.gz), [log](nativechecks/native-ui-r02/process-1.log.gz) |
| Continuation r01 | Failed after 9.647 s, 1 record. Test correctly used separate prototype part IDs; the driver incorrectly required ordinary expedition IDs. Only the trial oracle was corrected. | [Summary](nativechecks/native-ui-continuation-r01/summary.json.gz), [executed driver](nativechecks/native-ui-continuation-r01/executed-driver.py.gz) |
| Continuation r02 | Failed after 38.859 s, 2 records. Actual Test and physical Return completed, but the driver wrongly required the stale Blit shadow flag to be false in workshop. The retained Return snapshot proves joined physics and original ownership; it is explicitly reused without inventing a later frame proof. The final handoff run subsequently records fresh full completion. | [Summary](nativechecks/native-ui-continuation-r02/summary.json.gz), [executed driver](nativechecks/native-ui-continuation-r02/executed-driver.py.gz) |
| Continuation r03 | Failed after 7.936 s, 1 record. Confirmed New exited before authored resources drained; the host refused the undrained staged transition. This was a real product defect. | [Summary](nativechecks/native-ui-continuation-r03/summary.json.gz), [process log](nativechecks/native-ui-continuation-r03/process-1.log.gz) |

The world-switch fix reuses the existing complete asset-drain predicate for JSON, timeout, per-frame exit and the native host handoff. Logical Empty alone no longer ends the process. Fixture, roots, scenery, environment, cargo, harbor, shape operations and the submitted event frontier must finish. The [independent source review](nativechecks/native-transition-source-review.md) found no actionable issue; the final continuation then passed the previously failing real New/Load route.

## Reproduction and file identity

The executable route is [validate_native_cove_ui.py](../../../../../scripts/validate_native_cove_ui.py). The final executed copies are retained for [continuation](nativechecks/native-ui-continuation-r04/executed-driver.py.gz) and [handoffs](nativechecks/native-ui-handoffs-r01/executed-driver.py.gz). The first two attempts predate per-run script snapshots; their executed source is not retroactively claimed byte-exact.

These are the actual remaining-only commands, run from the repository with the sole GPU/controller lane. They intentionally reference retained local test storage; that storage is not published in this evidence folder.

```sh
DISPLAY=:0 XAUTHORITY=/run/user/1000/.mutter-Xwaylandauth.8NQFV3 VOXY_WINDOW_BACKEND=x11 nix-shell --run 'python3 scripts/validate_native_cove_ui.py --binary build-native-save-host/bin/voxy_native --output build-cove-ux-r01/native-ui-continuation-r04 --storage-root build-cove-ux-r01/native-storage-r02 --continue-from build-cove-ux-r01/native-ui-continuation-r03 --continue-after-return'

DISPLAY=:0 XAUTHORITY=/run/user/1000/.mutter-Xwaylandauth.8NQFV3 VOXY_WINDOW_BACKEND=x11 nix-shell --run 'python3 scripts/validate_native_cove_ui.py --binary build-native-save-host/bin/voxy_native --output build-cove-ux-r01/native-ui-handoffs-r01 --storage-root build-cove-ux-r01/native-storage-r02 --handoffs-only --world 7be90876356cf6b29ea31220a22bc8ef --seconds 180'
```

Use new output roots for a new execution and the active host's X11 authority path. For an independent full journey, omit the continuation options and use new output and storage roots; the existing evidence did not need that replay.

The [archive index](nativechecks/archive-index.json) binds every compressed original summary, log and available executed script by SHA-256. The [composed run index](nativechecks/native-ui-composed-r01.json.gz) records all seven attempts and their separate process outcomes. [Final source hashes](nativechecks/final-source-sha256.json) identify the driver, helpers, host, menu, input preferences and reviewed application integration. No raw save payloads or images are included.
