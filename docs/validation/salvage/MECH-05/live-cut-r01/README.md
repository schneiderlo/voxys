# Live cutter, section saves and protected rebuilding

Status: bounded native/browser integration verified. This is preparatory MECH-05 / PLAY-05 work under D32. No parent task or gate is accepted by this report.

## Player controls

Outside the workshop, **C** or **Cut weld** cuts the named nearby connection. The target is the closest enabled weld within two metres of the player's hand, measured from its observed physical socket. The exact selected weld is rechecked at the joined execution boundary. An active tow/harbor cable, swimming, jumping, pending save or concurrent build operation prevents cutting.

A cut preserves all part IDs, stock, settings, condition and cargo ownership. It may leave one rigid body when another welded path still connects the parts. A separating cut creates every resulting section. The player stays with the part supporting their feet; separating the helm leaves the player walking on the retained deck. Propulsion stays with its actual module and receives controls only on the helm's root.

The first cut protects the intact custom design, or uses the existing built-in starter design. The four-entry library never overwrites a backup. Further cuts verify that the matching intact design already exists. The temporary all-weld copy used to identify this backup never changes accepted bonds. Missing protection refuses the operation. Backup removal is blocked while the accepted build has cut bonds.

Cut publication automatically saves and pauses. Resume unlocks after durable acknowledgment. **Rescue / R** returns all sections; it does not repair cut welds. At the home workshop, **Rebuild starter / H** retires every old section, recreates loan parts and puts exact paid parts into owned storage. **Load recovered design / K**, then **Launch / Enter**, uses that stock with normal costs. Rebuilding requires every section within four metres horizontally of its authored home anchor. Rescue remains the explicit way to return remote sections.

## Implementation

- `CoveRigidRoots::reachableWeld` uses joined root observations and exact socket mappings. `transferCutPlayer` prepares a new player against the inherited root poses; no live player is mutated during preparation.
- The existing GameSession `CutWeld` transaction supplies the accepted disabled-weld build. Application prepares its `AssemblyFracturePlan`, all scene/shape/player/workshop data and a staged recovery library before taking GPU ownership.
- Upload every shape, spawn every child and configure each required water driver before reserving parent retirement. Cancel all unexecuted children on a refusal. A checked joined-boundary mutation retires the entire parent set. Every child observation and parent retirement must complete before the no-fail session/scene/root swap.
- Child origin velocities inherit the parent's post-solve velocity field, including angular motion; no second impact impulse is added. Rendering binds each part to its actual body, and winch/helm/player consumers use their corresponding sections.
- A prepared replacement freezes input. Once the parent is scheduled for retirement, it is retained only for observation/cleanup and cannot receive helm commands. New children start with neutral controls.
- The workshop can inspect validated separated scenes while keeping an unconnected draft ineligible for ordinary Launch. It can load the protected intact design or rebuild the starter. Displayed boat/workshop mass sums every section.
- Existing physical SVCE v4 captures and restores exact root keys/motions, control and rider bindings, and the protected design. No archive schema change or old-byte rewrite was introduced.

## Verification

Native `native-journey-r02` passes 14 recorded stages with frozen `native-cut-r02`. A real 13-part / 1275 kg save sails more than eight metres from home. Cut 1 disables a redundant weld and retains one body; cut 2 creates roots 7 and 15. Helm 15 separates; the player stays on root 7 in Walking mode. Both roots and the protected design survive process restart, Rescue and another restart. Rebuild stores paid IDs 35 and 100 exactly once; loading the protected design consumes those same parts for zero charge; a second rebuild/restart returns the same two IDs to storage. Material stays zero and the unique unbanked generator is retained.

Native `native-journey-r03` extends that same journey to **15 stages**. It accepts the unique generator, hooks a real cable, presses C and verifies that no cut/body/design change occurs. After releasing the cable, cuts, restarts, Rescue, design reuse and repeated rebuilding all pass with the accepted generator retained.

Browser `browser-journey-r01` passes **14 recorded stages plus drained Leave** using frozen `web-r01`. Cuts save at ticks 450 and 454; reload restores both roots at 455. Rescue saves both at 459 and reload restores them at 460. Rebuilding, protected-design stock reuse, a second rebuild and reload finish at tick 477 with exact paid IDs 35/100 in storage, zero material and the accepted unique generator preserved. Browser errors and every recorded uncaptured-GPU-error check are empty. These are execution/correctness results, not performance or visual-polish acceptance.

| Verification | Result |
|---|---|
| Bazel cove regression | 54 passed |
| Bazel physical archive regression | 21 passed |
| CMake CPU movement/build/mapping cases | 46 passed |
| CMake GPU cove cases | 8 passed |
| Browser UI lifecycle | 20 passed |
| Bazel native, CMake native, WASM application builds | Passed |

The GPU capacity case fills either the body pool or the water-driver pool after the first child is admitted. Three refused attempts cancel every unexecuted child while preserving all 15 original live bodies. After releasing one filler, both children acquire the recovered slots successfully. Shape ownership drains to Closed. Other GPU cases exercise real split motion, distant recovery, actual dock/seabed collisions, water phase and harbor lift behavior. CPU cases also cover full/missing design protection, broken-workshop loading, exact socket reach/disabled/stale refusal, detached support transfer and malformed scene rejection.


## Retained failures and fixes

- `native-build-r01.log`: the libraries/tests compiled, but the requested CMake executable name `voxy` did not exist. Correct target: `voxy_native`.
- `cut-focused-r01`: two test setup defects. Synthetic shape handles omitted the required pool identity; the rider fixture picked a pontoon surface obstructed by the intact deck. Corrected to valid owner records and the actual boarding deck.
- `cut-focused-r02`: rider transfer differed by 0.915 micrometres because each root stores float sector-local coordinates. The original nanometre assertion exceeded that representation's precision. The final 50-micrometre tolerance remains below gameplay collision/skin tolerances; no physics or reach rule changed.
- `native-journey-r01`: the game closed on the first cut. The pending-input path attempted to neutralize a parent body after its retirement was scheduled. The new staging guard avoids commands to that retired handle; the unchanged journey passes in r02. No archive from that failed cut was acknowledged.

## Artifacts and reproduction

Scratch: `build-cove-live-cut-ubvsrtrj`. Preserve its failed logs, all older evidence/saves and the owner's port-38206 preview. No screenshots were taken. GPU jobs execute sequentially. CPU builds may overlap.

`source-r02/` and `checks/native-r02-sources.json` identify the passing native integration before the extra test/driver coverage. Final source/artifact identities are in `artifacts-r01.sha256.json`. `native-cut-r01` is the failing before-fix binary; `native-cut-r02` is the passing binary. Browser package `web-r01/` contains its own matching `voxy_wasm.js`, `.wasm` and `.data`. The isolated browser profile `/tmp/voxys-cut-1cc8ttgz` was copied from `/tmp/voxys-startup-jLGqhu`; the original profile was not changed. Browser origin is port 41289, saved world `00a297a0ef96a7134f245d2d48080acf`.

From the repository's Nix shell:

```sh
cmake --build build-native-save-host --target voxy_native cove_player_tests -j6
EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j6
bazel test -c opt //tests:cove_player //tests:cove_save --test_output=errors
python3 scripts/validate_native_cove_cut.py --binary <frozen-native> --source-slot build-cove-root-resume-_csjbh9y/native-saves-r01/c708a658908c35223b4ed6be5999becf --storage-root <new-root> --output <new-report>
```

The browser harness is `scripts/smoke_integrated_wasm.mjs <frozen-web> salvage-cove`, with `VOXY_SMOKE_COVE_CUT=<new-report>`, the isolated profile/port/world, `VOXY_SMOKE_GPU=gaming-x11`, `VOXY_SMOKE_NO_SCREENSHOT=1` and an adequate journey timeout. It uses real buttons/key events and Page.reload, plus read-only state. It never injects a cut, archive, body pose or successful receipt.

Completed checklist items: exact physical fragment archives, bounded live cove consumer mapping, and protected cut/recovery/rebuild integration. General fracture damage, arbitrary joint retargeting, full allocation/fault coverage, wider loss/exploit cases, full MECH-05/PLAY-05 and all unfinished gates remain open. Continue the full implementation goal from those unchecked requirements. No complete gate passed, so no gate commit is due.

For the owner to try the change, a separate local server serves the verified frozen package at `http://127.0.0.1:36787/index.html?experience=salvage-cove`. Its process is recorded in `build-cove-live-cut-ubvsrtrj/checks/playable-preview.json`; do not stop or replace an unrelated preview. The application panel open request was queued by Codex. This new origin has its own save storage.
