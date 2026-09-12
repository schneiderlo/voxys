# Native first-recovery guidance — D41

**Focused checks and the final native mission journey passed.** Six new
mission/pause formatter cases and two existing readable-layout cases passed.
The final combined native application completed **42 recorded stages**, including
actual delivery, durable save/restart and harbor power. Both combined native/WASM
builds passed. Final combined browser objective/machinery acceptance also passed. The [joint D39–D44 required result](../../checkpoints/driven-cove-r01/checks/summary.json) passed: **2,096 native cases passed, 3 skipped and 4 disabled**; terrain import **10 passed / 1 skipped** from cache. Bazel elapsed **1,132.896 seconds**; native test execution **1,125.548 seconds**. Publication remains pending. This does not complete UX-01 or the game plan.

The native panel now explains the existing first recovery: **J** accepts,
nearby **E** boards, **F/Q/Z** handle the generator cable, **H** delivers, and
**K** powers the harbor after delivery is saved. Cargo distance, height, speed
and rotation explain actual unmet delivery conditions. Nearby interaction,
movement, building, painting, pause and rescue controls retain their roles.
No gameplay action, input binding, inventory or save field was added.

## Actual focused results

| Attempt | Result |
|---|---|
| `checks/focused-r01.log` | Compile failed at the new JSON string writer's implicit char-to-unsigned-char conversion under `-Werror=sign-conversion`; **zero tests ran** |
| `checks/focused-r02.log` | **8 passed, 0 failures, 0 skips**: six new guidance cases plus two existing HUD/layout cases; target reported 0.1 seconds |

The corrected writer iterates a `char` byte and explicitly casts it to
`unsigned char` before escaping controls, quotes and backslashes. This preserves
JSON byte semantics and satisfies the strict warning policy. The original
candidate and application record remain under `preparation/`; the additional
`json-byte-cast-fix.patch` records this exact correction. The passing XML is
`checks/cove_hud-test.xml`; `checks/results.json` is a parsed index, not a new run.
The complete test/build log is retained, including the original failed attempt.

The six new cases verify J on foot; authoritative H permission even when numeric
conditions already pass; inclusive distance/height/full-speed/spin limits;
missing cargo/winch/root/tow observations; banked or installed state without a
durable save; pending installation and manual-save precedence; and every
mission/pause text layout. Layouts include 640×480 and 960×540, preserve at least
20-pixel body text and the existing 768-quad / 167,936-byte HUD allocation.
These are formatter/layout checks, not a new GPU rendering or visual approval.

## Authority and source scope

Application supplies read-only facts from the current session and actual action
guards. It finds the exact job/cargo identity and calls the existing delivery
eligibility callback. Numeric text cannot grant permission. Current installed
limits are cargo-root distance at most 3.9 m, height at least −1 m, full 3D speed
at most 0.8 m/s and angular speed at most 1 rad/s. It adds no mandatory helm,
cable or whole-load-above-water requirement.

Securing, banked-but-saving and durable completion stay separate. K requires
actual saved delivery, storage, absence of a prior harbor installation, a valid
boat and the real off-boat three-dimensional dock range. Installation pending
before its checkpoint never advertises Resume. Current manual save progress or
failure takes precedence over historical delivery success. Real parking refusal
and storage revocation remain visible. Independent review found and corrected
both initial paused-state precedence defects; `preparation/review.md` records
the completed source review and its limits.

`nativeHud` JSON observes renderer-owned cached content sampled before rendering;
it does not recalculate readiness or mutate gameplay. Its quad count still
means encoded output, not independently completed HUD work. The actual journey
requires later GPU/physics completion for captured observations.

`preparation/base-sha256.json`, `candidate-sha256.json` and `applied.json` identify
historical candidate/application stages. The candidate was rebased equally on
both sides for the prior rope-baseline and F9 routing corrections. Root then
fixed the JSON cast and integrated D42/D43 in other Application areas.
`source-scope.json` is a labeled documentation-time snapshot, **not** a claim
that all later Application changes were covered by the eight focused cases.
The reviewed formatter and HUD test payloads remain the same.

## Completed native mission journey

The final frozen `build-cove-guidance-r01/native-r01/voxy_native` completed one
actual control journey with `--objectives-and-harbor`: **42 recorded stages**,
**16 observed objective step names**, and two stopped-process scans with zero
error lines. The raw summary does not record total wall-clock elapsed time;
this report makes no inferred duration claim.

The player built the paid lifting rig, accepted J, boarded and hooked the actual
generator, reeled/returned/hoisted it and used H only when eligible. The observed
panel moved through accept, workshop, board, hook, return, lift, slow, deliver
and delivered. Delivery saved, an actual new process reopened that world, and
attempting duplicate delivery paid nothing. The same flow used the existing
Rescue-to-dock route, actual K permission and K input to install and durably save
harbor power. The final panel showed powered only with installed/durable harbor,
no installation pending and a paused expedition.

The accepted final boat retained **11 parts / 1,035 kg**, paid part ID **35**,
**96 material** and zero special machinery. The generator stayed banked.
The final powered physical archive is schema 4, generation 4, tick 2100,
21,471 bytes, SHA-256
`2686543d1046c74df149a0effd949749d3f33cbdd5d0308a27e9573447157cd7`.
The delivery and power archives, both process logs, both observations and the
unchanged full summary are in `native/accepted-r01/`. Every recorded stage
requires later actual GPU/physics completion under the same observed body,
build/topology and incarnation. The native HUD is readable and encoded with
current seven-part presentation and scene-shadow assertions throughout.

This run did **not** use `--permission-failure`, repeat the native mechanism
matrix, or restart again after power. It proves durable power publication in
the second process, not an additional powered-world restart. Brief pending
screens may cross between HUD samples; the focused CPU cases cover their
semantics without delaying gameplay to capture them. All 16 observed names are
preserved in `objectiveSteps` in the raw summary.

## Final package and remaining checks

Both combined builds passed; raw logs and `checks/package-r01.json` retain
all package entries. Principal identities are:

| File | Bytes | SHA-256 |
|---|---:|---|
| Native executable | 47,223,048 | `f069239c46e25b545c258fa1f03c461b7d8e919cb5a8875bb34bd7b5f3ee9936` |
| WASM module | 7,279,865 | `a47b46fa6bbf547db3d52b65e579e90215ee2699edafcddb9512830f09714600` |
| WASM data | 87,364,450 | `e68fd82d3ea55260343834c7c15ec00b4b8e302784bef2e26eb996b64b8f7010` |

`preparation/integrated-review-r01.md` pins the reviewed combined source,
including Application, formatter, launch helper and driver. Its pre-journey
pending wording is historical. The original preparation handoff and failed
compile also remain unchanged. The combined browser objective/machinery journey subsequently passed five
objective stages and 28 machinery stages; its exact evidence is in the
[browser objective](../objective-card-r01/README.md) and
[driven Cove](../../LOOK-01/driven-cove-r01/README.md) records. The [joint D39–D44 required result](../../checkpoints/driven-cove-r01/checks/summary.json)
now passes against final nine-presentation content; publication remains pending. No screenshots or image generation were used.
