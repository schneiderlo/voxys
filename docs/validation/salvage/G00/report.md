# G00 — baseline checkpoint

Status: launch matrix, BOOT tasks, independent source/evidence review and
mandatory repository checks passed. Checkpoint commit is being prepared;
its result will be recorded after the enabled hook runs.
Base revision: `7563f61fd536df7de5d209ff35d3cd2099ebcbb6`.
Branch: `codex/salvage-implementation`.

The owner requested a fresh upstream check during implementation.
`git fetch origin main` completed successfully: HEAD, local main, origin/main
and FETCH_HEAD all equal the base revision above; ahead/behind is 0/0.
[Upstream audit](upstream-check.json) records the check. No merge was necessary
and no working changes were discarded.

## Start here

Read root `AGENTS.md`, root `README.md` and all of
[GAME_IMPLEMENTATION_TODO.md](../../../../GAME_IMPLEMENTATION_TODO.md).
That plan is the authoritative self-contained game contract, task dependency
list, acceptance criteria and current execution ledger. Claim an unchecked task
with completed prerequisites before editing. Root coordinates shared build,
application and schema changes. The owner needs short, readable communication.

The new route is a **static authored cove preview** with 32 owned scenery
bodies. It supplies launch/reset/leave lifecycle only. The future construction,
missions, inventory, animation and co-op game are not implemented by G00.
DATA-01/02 and ASSET-01 are independently validated foundations where their
plan checkboxes are complete; their acceptance does not imply downstream gates.

The owner reviewed the primitive cove on 2026-09-07 and reported that it looks
substantially worse than the original prototype. Root agrees that its large
plain boxes, flat appearance and weak framing do not demonstrate the intended
game. It is **not visually approved**. The plan now requires LOOK-01 during the
asset phase and includes it in G02, moving a serious in-engine visual proof
ahead of broad gameplay/content expansion. Final VIS-03/VIS-05/G08 requirements
remain intact. Functional BOOT-05/G00 acceptance cannot substitute for those
visual requirements.

## Reproduce builds and launch

Use the project's Nix shell and bounded parallel builds. Do not delete existing
build trees or replace hand-authored assets. The hook is installed through
`./scripts/setup-hooks.sh` and must not be bypassed.

```sh
nix-shell
bazel build -c opt --jobs=4 //:voxy_native
bazel run -c opt //:voxy_native -- --config salvage.cfg
bazel build -c opt --config=wasm --jobs=4 //:voxy_wasm //tools:serve_wasm
bazel run -c opt --config=wasm //tools:serve_wasm
```

Open the printed local browser server URL with `?experience=salvage`. Reset
with R or the browser Reset button. Browser Leave waits for actual physics
removal before returning to LEGO World. Native Escape releases capture first,
then exits when pressed uncaptured. Movement uses the existing character path;
boarding/swimming/rescue are later game work.

| Route | Native selection | Browser selection |
|---|---|---|
| Smooth terrain | `--config voxy.cfg` or native default | `?experience=terrain` |
| LEGO landscape | `--config lego_world.cfg` | `/` or `?experience=lego-world` |
| LEGO shore | `--config lego_shore.cfg` | `?experience=lego` |
| RIDGEBREAK | `--config ridgebreak.cfg` | `?experience=ridgebreak` |
| Cove preview | `--config salvage.cfg` | `?experience=salvage` |

WRECKWATER remains a separate native graphical bootstrap with explicit
loopback/server configuration. Its [actual graphical launch and local recipe](../BOOT-05/native-route-report.md)
use fresh ephemeral loopback credentials. The window initialized and rendered;
OS screenshot capture was unavailable, harness termination was required, and
783 rejected character frames remain unexplained. This is launch evidence with
known limitations, not healthy gameplay or a completed match.

[BOOT-02](../BOOT-02/report.md) contains the CMake, fresh WASM staging and Nix
recipes, versions, source/asset checks and retained failed attempts. A missing
config may fall back under the legacy loader, so confirm the actual selected
route, asset dimensions and UI; process exit alone does not prove selection.
Bazel and CMake both include the added game modules. The browser package must
include all five route configs, required static files and raw shader sources.

## Evidence and known limits

| Item | Evidence |
|---|---|
| Starting code and architecture inventory | [BOOT-01](../BOOT-01/report.md) |
| Native/WASM build systems and package fixes | [BOOT-02](../BOOT-02/report.md) |
| Visible timing and separate memory | [BOOT-03](../BOOT-03/report.md) |
| Product, ownership and hardware matrix | [BOOT-04](../BOOT-04/report.md) |
| New preview ownership, reset and route regression | [BOOT-05](../BOOT-05/implementation.md), [final summary](../BOOT-05/final-summary.json) |
| Canonical units/frames/IDs/codec | [DATA-01](../DATA-01/README.md) |
| Immutable starter part catalog | [DATA-02](../DATA-02/README.md) |
| Reproducible Blender authoring fixture | [ASSET-01](../ASSET-01/report.md) |
| Final native/WASM Bazel builds and real HTTP package checks | [Final integration build](final-build/README.md) |
| UI, JS codec, memory sampler and shader synchronization | [CPU/tooling checks](cpu-tools/README.md) |

The visible legacy browser baseline missed the 16.67 ms presentation target:
movement RAF p95 19.70 ms; full-pool p95 23.70 ms. Per-phase completed GPU
sample counts are too small for robust tail certification. Native/browser
memory is sampled, partially observed and non-additive across domains.
Staging/pending retirement and some historical runtime provenance are absent.
The report preserves these findings; none is a passing final performance gate.

Only this Linux/Radeon host is currently measured. Windows, additional GPUs,
controllers, actual human playtests, final art/audio, real production staffing
and external release infrastructure remain later explicit requirements. No
release date, funding, external publication or human approval is invented.

## Checkpoint rule

After every task passes, update its checkbox and evidence in the plan. After
each gate passes, run the required checks and commit its completed work. Keep
unrelated work and unfinished tasks out of the checkpoint commit. Record the
commit identity in the execution ledger; never bypass hooks after a failure.

For this gate the mandatory command is:

```sh
bazel test //tests:voxy_tests //tools:terrain_diffusion_import_test
```

The actual pre-commit hook runs this same command. The
[first complete-suite attempt](first-full-check/README.md) timed out at 300.1
seconds and found two obsolete structural shader expectations. Both unchanged
shaders preserve the intended normal-data contract. The
[corrected focused checks](shader-contract-check/README.md) pass; only the
combined target's time budget was raised to allow all cases to finish. This
does not make the incomplete run a pass. The
[complete retry](final-full-check/README.md) subsequently **passed**: 1,495 C++
cases passed, three skipped, four pre-existing cases disabled, zero failures;
the importer target passed from cache with one reported Python skip. The
combined target completed in 326.9 seconds. Logs, XML, source hashes and skip
details are retained. The [independent review](review.md) approves the final
source and launch evidence. The enabled-hook checkpoint commit remains the
last step; unfinished DATA-03, ASSET-02 and LOOK-01 prework are excluded.
