# Continue saved Cove — D45

**The real browser Leave → Continue journey passed**, along with integrated source checks and both independent reviews. The five-file candidate was applied after D39–D44 was published at `8a5998d39417d4516c2002f8568c9964a3abc5f0`. The required repository check also passes from its valid cache; the containing commit records normal-hook publication. This is a separate bounded SAVE-04/UX-01 checkpoint, not completion of the full save or game gates.

The LEGO Landscape and Shore navigation gains one **Continue saved Cove** link. After a confirmed save or resume, it returns to that world on the same browser profile and origin. The existing resume path starts paused. Leaving still waits for the existing game removal acknowledgment and does not autosave. The bookmark option remains available.

## Authority stays with the save store

Only a disposable navigation hint is added: localStorage key `voxys.cove.last-confirmed-world.v1`, containing a lowercase, nonzero 32-hex world ID. The hint is written after successful durable publication and the existing required owner/digest acknowledgment. Pending, failed, closed or unacknowledged operations retain the previous hint.

The hint cannot contain a URL or save payload. Continue constructs a same-origin link using the current pathname, only `experience=salvage-cove` and the validated world, with the old query/hash cleared. Missing, corrupt, oversized or unavailable local metadata hides this link; storage getter/setter exceptions cannot break a real save. A valid but stale hint still reaches the existing missing/damaged/conflicting/busy-world refusal, with no fresh-world fallback.

IndexedDB generations, paired current/mirror transactions, exclusive locks, save bytes, C++ state and native behavior are unchanged. The link uses native anchor semantics, existing visible keyboard focus and a minimum 44 px height. It remains available under the narrow-screen rule that hides other navigation links. It initializes only on the two LEGO routes.

## Candidate and checks

The [source manifest](source-manifest.json) records exact original/candidate hashes for five files: `web/cove_saves.js`, `web/index.html`, `scripts/test_cove_saves.mjs`, the new `scripts/validate_cove_continue.mjs`, and optional dispatch in `scripts/smoke_integrated_wasm.mjs`. The [patch](candidate.patch) and [implementation handoff](handoffs/implementation.md) retain the precise pre-integration candidate and invocation.

| Check | Actual result |
|---|---|
| Standalone save coordinator/UI Node file | **26 passed, 0 failed, 0 skipped**, 93.77328 ms |
| Earlier Node test-runner invocation | One-file aggregate pass only; retained separately and not used as a case count |
| New real-control driver and optional dispatch | Syntax checked; no browser execution |
| Independent save/route and UI/driver reviews | No actionable behavior finding; two evidence wording corrections applied; reviewers ran no tests or apps |
| Initial live integration | All five applied files matched the reviewed candidate hashes; later runner-only capture guard is recorded below |
| Integrated save coordinator/UI | **26 passed, 0 failed, 0 skipped**, 97.730442 ms |
| Existing expedition store checks | **4 passed, 0 failed, 0 skipped**, 22.779805 ms |
| Existing preview UI script | **36 assertion groups passed**; its legacy printed aggregate of 18 remains incomplete |
| Frozen browser package | 25 files recorded; C++ module and data hashes unchanged from D44 |
| Actual Leave → Continue | **5 stages passed in 15.232 seconds**; outer report passed, no browser/sample/console errors |
| Required repository check | Passed in1.761s; both native/import targets reused valid cached results; zero targets re-executed. The normal commit hook stays enabled. |

[The 26-case log](checks/node-tests-r02.log) includes the existing 17 cases and nine new cases: confirmation ordering; previous pointer preservation on publication/digest/closure failures; safe same-origin URL construction; invalid and oversized metadata; unavailable storage reads/writes; stale missing-world refusal; and the semantic/mobile navigation contract. These use controlled fixtures, not real browser permission or disk-fault tests. [Source review](handoffs/source-review.md) records its scope and limits.

The [integrated Node results](checks/integrated-node-results.json) distinguish test-runner case counts from the preview script's source-counted assertion groups. Its three raw logs are retained alongside the original scratch pass. The [applied manifest](checks/applied-r01.json) binds the reviewed five files to published base `8a5998d39417d4516c2002f8568c9964a3abc5f0`; all five were rechecked by byte size and hash after application. This is the historical executed manifest; the final runner-only guard has a separate source binding below. The [25-file package manifest](checks/package-r01.json) records the exact browser package. WASM remains SHA256 `a47b46fa6bbf547db3d52b65e579e90215ee2699edafcddb9512830f09714600`; data remains `a4036c2e3d91b77de399365353a0a9ea5eb7bba2de8545205ca3a7c7efa9cec0`. Only the selected JS/HTML and driver/tests differ.

## Bounded real browser acceptance

The prepared driver uses the retained D44 profile `/tmp/voxys-startup-ZgT8Up`, origin `http://127.0.0.1:46039`, world `d00074effd95777b146c7c069ba23997` and the earlier passing combined report as its exact ownership/blueprint baseline. Root must use the same origin/profile and a fresh output directory.

The hint must match the successfully resumed old world; it may already exist and be refreshed by this run. The driver clicks Leave, waits for the existing drained navigation, verifies a visible native Continue link and its exact same-origin URL, then clicks it. The same world must return paused with its owned IDs, stock and logical structure unchanged. P/B opens the design only to compare exact blueprint bytes, including part placement, paint, connections and settings; B/P closes and pauses without an additional manual save action. Normal resume still republishes its required durable lineage. Each game stage requires a later submitted frame and actual GPU/physics completion bound to that page's live owner. The complete journey is capped at 120 seconds.

The five-stage journey uses no hauling/building matrix, save injection or screenshots. The outer startup runner did capture one automatic image, as recorded below. The metadata link's absence is harmless when localStorage is unavailable; the existing saved URL remains the fallback.

## Actual retained-save result

[The real journey](browser/r01/summary.json) passed five stages in **15.232 seconds**: confirmed old save; real Leave to the LEGO page and visible Continue link; same saved world restored paused; unchanged design/settings/stock checked; game left paused without another manual Save action. Each normal resume still performed its existing required durable lineage publication.

The physical Continue click used a visible native anchor measuring **160.390625 × 44 px**, with `tabIndex=0`, whose hit target and same-origin URL were checked. The URL selected the same existing world explicitly. This proves that control at the tested desktop viewport; narrow-screen visibility and keyboard semantics have source/unit evidence, not a separate mobile or keyboard-only runtime pass. The pointer matched the confirmed world; the report does not claim this run newly created the metadata key.

The restored boat keeps **11 parts, 17 connections, 1,035 kg, 48 material, zero machinery and no paid part IDs**. World/build/topology/root identities stay exact. The exported blueprint is byte-equal to the earlier accepted save, including placement, paint, connections and disabled/reversed propeller settings: SHA256 `c9b5b257833842138048f27f4d3cc6113434e6ec7d4a2705c1d3e3fee85e2e2d`. A fresh page observation incarnation is confirmed after Continue. Each game stage waits for a later submitted frame and actual GPU/physics completion against that page's current body owner; the report preserves those serials/ticks.

The [outer report](browser/r01/startup.json.gz) passed with empty browser/sample errors and zero console error entries. The runner process exited 0. The [raw runner log](checks/browser-r01.log.gz) and [compression/error manifest](browser/r01/archive-manifest.json) preserve exact original byte hashes; compression round-trip was verified. The five-stage journey used no additional hauling/building matrix, refit, manual Save action or screenshot. The outer startup capture is documented below. [Required result](checks/required-summary.json): the unchanged native/import inputs reuse the completed D39–D44 suite (2,096 game passes, three skips, four disabled; terrain ten passes, one skip). The containing commit is created only through the normal enabled hook.


## Automatic startup capture correction

The executed outer r01 invocation omitted `VOXY_SMOKE_NO_SCREENSHOT=1`. Before the image-free five-stage journey, the existing startup runner therefore created **one** `startup-salvage-cove.png` (395,763 bytes) and ran its existing image checker. No assistant opened or visually reviewed that image. It is not final-art evidence and is not copied into this archive. The original outer report already records the capture; the earlier broad no-screenshot description was incorrect and is corrected here. No new run was made to replace or hide this event.

[Capture correction](checks/capture-correction-r01.json) preserves the image hash and exact executed/final runner hashes. The executed runner was `57d13685011b5af191d28cff746a14df9b3c3f845b6d64d4716f3d185a16b939`. The final runner is `742e4b7ad9c0820c9e4522ae6bcbe3a2e5d433ee6c67c750657bab610b23a809`: its capture guard now also skips startup images whenever the Continue journey is selected. [The one-condition patch](checks/capture-guard-final.patch) and [final source binding](checks/final-source-r01.json) retain this distinction. Root syntax-checked the final runner; game code, served package and the passing journey are unchanged, and no runtime rerun is claimed. The [bounded guard review](handoffs/capture-guard-review.md) confirms that the whole screenshot/write/checker/report block is skipped while startup checks and the journey remain unchanged. Root checked the earlier D39–D44 outer reports separately; this capture mistake is limited to D45 r01.

The candidate handoff is a historical pre-run snapshot. This explicit invocation supersedes its example for future reproduction; use fresh output paths and a prepared final package, with the same retained profile/origin:

```sh
VOXY_SMOKE_NO_SCREENSHOT=1 \
VOXY_SMOKE_PROFILE=/tmp/voxys-startup-ZgT8Up \
VOXY_SMOKE_PORT=46039 \
VOXY_SMOKE_RESUME_WORLD=d00074effd95777b146c7c069ba23997 \
VOXY_SMOKE_COVE_CONTINUE=build-cove-continue-repeat-r01/browser \
VOXY_SMOKE_CONTINUE_BASELINE=build-cove-guidance-r01/browser-startup-r02.json \
VOXY_SMOKE_PRESENTATION_PARTS=9 \
VOXY_SMOKE_REPORT=build-cove-continue-repeat-r01/startup.json \
node scripts/smoke_integrated_wasm.mjs <final-web-package> salvage-cove
```
