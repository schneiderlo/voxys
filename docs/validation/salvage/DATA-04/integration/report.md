# DATA-04 integrated acceptance

**Complete for the local authority and fake-preparation scope.** The session is
now part of both game builds and the real cove lifecycle. This work follows G00
commit `7f28fab` and awaits the next passed gate commit. It does not implement
boat physics, construction UI, persistence or a finished-looking game.

## Verified behavior

GameSession owns the accepted builds, resources, cargo, jobs and request
receipts. Player intents cannot inject balances or authoritative snapshots.
Preparation keeps accepted state intact; failed or canceled work releases its
reservation. The fixed-tick test driver alone can publish a ready transaction.
The real preview adapter rejects machine preparations, and the scenic preview
never advances that fake driver or grants inventory.

The cove now creates its own session and keeps it through Reset. Controls queue
typed scenic intents; the frame boundary applies them. Leave closes admission
and supersedes queued Reset before retirement polling, preventing an unwanted
respawn. The read-only status exposes lossless string counters and no authority
tokens. A fresh entry creates a new namespace/session.

Independent core review reproduced and fixed a bootstrap defect: numeric IDs
within the allocator range were accepted as owners/lease holders even when they
were not the one admitted participant. All eight invalid role/ID combinations
now reject. [Core review](../review.md) retains the failing and corrected
reproductions. [Application review](../app-integration-review.md) checks
dependency lifetime, control dispatch, failure closure and namespace generation.

## Executed checks

| Check | Observed result | Evidence |
|---|---|---|
| Optimized Bazel session target | 21 passed, zero skipped | [Cases](final-bazel-session.log), [XML](final-bazel-session.xml) |
| CMake standalone session | 21 passed, zero skipped; native app rebuilt | [Command](final-cmake-command.log) |
| Both mandatory combined C++ targets, focused filter | 21 session + 8 preview cases passed in each | [Bazel](combined-bazel.log), [CMake](combined-cmake-command.log) |
| Strict native standalone | 21 passed with GCC warnings as errors and UB/float-cast sanitizers | [Author evidence](../README.md) |
| Actual WASM session execution | 21 passed with JS EH, Asyncify, fixed heap and unchanged source hashes | [WASM proof](../wasm-exceptions/README.md) |
| Real fixed-heap allocation failures | Throwing new produces catchable bad_alloc; nothrow returns null with selected flags | [WASM allocation proof](../wasm-exceptions/README.md) |
| Native and WASM application compilation | Final native CMake and WASM CMake/Bazel builds pass | [CMake](final-cmake-command.log), [Bazel](final-bazel-command.log) |
| Native GPU retirement | One real GPU case passes; unrelated body remains valid | [Case](gpu-retirement.log), [XML](gpu-retirement.xml) |
| Native graphical launches | Cove and original LEGO World launch, render and exit naturally | [Reports/screenshots](../native-routes/native-route-summary.json) |
| Browser real interaction | Walking, Reset button, R with held movement, pointer lock, Leave, legacy-world navigation and re-entry pass | [Journey](../browser-journey/summary.json), [Startup](../browser-salvage.json) |
| Browser control ordering | Queued Reset/Leave/Reset returns 1/1/0; actual retirement completes without respawn | [Journey controlPriority](../browser-journey/summary.json) |
| Other browser routes | LEGO shore, terrain and RIDGEBREAK each retire rendered frames with no reported GPU loss/validation errors | [Commands](../browser-regression/commands.json) |
| Package validation | Source-matching preloads/exports and six negative package controls pass | [Checks](package.log) |

The focused combined checks are not a full-suite claim. The required repository
hook remains enabled and will run at the next gate commit. Native captures are
launch/natural-exit evidence, not native keyboard interaction tests; the real
interactive journey was executed in Chrome. The supplemental control-ordering
case invokes the public intent API directly and is identified separately from
the mouse/keyboard UI checks. It never injects world state or completion.

## Browser policy and failures retained

[The selected policy](browser-failure-policy.md) enables `-fexceptions` at compile
and link time, plus `-sABORTING_MALLOC=0`, retaining a fixed heap and Asyncify.
The initial disabled-exception build and the superseded native-EH/Asyncify
warning are retained. Actual allocator probes retain both aborting baselines;
injected exceptions alone were insufficient to prove this behavior.

The first graphical Chrome attempt failed before application initialization: its
GPU subprocess repeatedly exited139 when launched inside the Nix development
shell. [That failure](../browser-first-nix-environment/report.json) is retained.
The identical packaged app then passed in the normal host environment used by
the established browser recipe. This distinguishes the failing launch context;
it does not identify the exact crashing system library. Run builds/native tools
in Nix, and run the installed Chrome browser harness outside that shell.

## Identity, display and limits

[Summary](summary.json) pins final module/build/harness sources, executable and
WASM hashes, XML case counts and runtime report identities. Native capture
SHA-256 is `84d8af6a47e0cf682110e4fc0248dfb4ddb8754e524b73f88cf9576e0a7e7652`.
The two native windows used Wayland, a 1600×900 framebuffer and the AMD Radeon
890M RADV STRIX1 adapter. Chrome152 used a visible 960×540 game canvas on the
AMD RDNA-3 adapter, fallback=false with timestamp queries. Those captures are
not matched-resolution visual comparisons or frame-time acceptance.

Root inspected the actual cove and legacy images. The cove still contains
large flat placeholder boxes and lacks the intended material/shape detail.
Its appearance remains unapproved; LOOK-01 remains unchecked. Other native
legacy routes and WRECKWATER retain their earlier, explicitly limited G00
evidence; this task does not re-certify a multiplayer match.

Receipts, processed markers, inventory and jobs remain memory-only. This
single-participant profile has one pending preparation; resource compensation,
undo/redo, durable marker export and event queues remain DATA-05/06 work. No
accepted game feature, save guarantee or benchmark is inferred from this stage.
