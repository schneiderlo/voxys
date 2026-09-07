# G00 — independent handoff review

Reviewer: `lego_gameplay`. Date: 2026-09-07.

**G00 acceptance is recommended. Final source, launch evidence and the complete
repository check pass. Only execution of the checkpoint commit remains.**
No remaining source defect, broken reviewed link or false completed prerequisite
was found. The final visual appearance is explicitly rejected by the owner;
this functional handoff does not approve art, gameplay or performance.

This reviewer performed source/evidence inspection and focused CPU/JS checks.
The native GPU and visible browser runs below are independently reviewed
executor evidence, not new runs performed by this reviewer. No GPU run, build,
plan edit or Git mutation was performed during this final review.

## Scope and reproducibility

Read the complete implementation plan, root instructions/README, the
[G00 handoff](report.md), BOOT-01 through BOOT-04, and BOOT-05 config,
native/WASM entrypoints, application integration, preview ownership, retained
metadata helper, browser UI, journey validator, tests and build registrations.
The earlier source review was superseded by two actual reset failures and their
corrections; only the final identities below support this recommendation.

The handoff now supplies native/browser build and launch commands, all ordinary
route selections, the exact loopback WRECKWATER harness, known failures, evidence
locations, task dependencies and the checkpoint rule. A new agent can begin a
bounded task without conversation history. Temporary output directories are
rebuildable destinations, not required historical dependencies.

Local Markdown targets in `G00/report.md`, `G00/final-build/README.md`,
`BOOT-05/implementation.md` and `BOOT-05/native-route-report.md` all resolved at
final review. Earlier BOOT-01–04 report links had also resolved. Every entry in
[final integration source hashes](final-build/source-hashes.json) matched its
current source file at this review. No complete source-tree identity is inferred
from that selected manifest.

## Independent focused checks

Executed again against the final available native test binary and JS sources:

```sh
build-salvage-native/bin/voxy_tests --gtest_filter='ConfigGameModeTest.*:ConfigFileTest.ExplicitModeRoundTripsAndMalformedModeNeverFallsBack:ApplicationSalvagePreviewTest.*:SalvagePreview.*'
node scripts/test_salvage_preview.mjs
node scripts/test_salvage_data.mjs
```

**13 CPU cases in four suites, 4 browser-UI cases and 6 JS codec cases passed.**
The CPU filter excludes `SalvagePreviewGpu.*`; invalid-config error logs are
expected rejection-fixture output. The native test binary SHA-256 was
`dda276b4f7dba3bea6cbe359935ce3e639f8d0c796fad3e9b823662b6bfce9ae`.
These checks are independent confirmation of the focused logic, not substitutes
for the visible journey, actual GPU regression or repository-wide hook.

Final reviewed identities:

| Source | SHA-256 |
|---|---|
| `src/app/application.cpp` | `21850e9e6d4d384cdffb509da5622c4548bb32790acedb1ef5cb793a05858e57` |
| `src/app/salvage_preview_readback.hpp` | `26f60cb1fa25004267b4ad31fdeac5527530fe91cdc03e188200fa07db413da3` |
| `tests/test_salvage_preview.cpp` | `53171945361032f07e0f1e6e5fed57beb277b2885e190e0ee98e52499b49ca05` |
| `src/game/construction/part_catalog.cpp` | `24869c7c346d49421b2fe20123181e4e5738d6e6ddc366339292d3f12044bcf3` |
| `scripts/validate_salvage_preview.mjs` | `b041a9e7bbf3399469021dad02abe04e21edfd74033bb31671d4d9aca3370675` |

Base revision remains `7563f61fd536df7de5d209ff35d3cd2099ebcbb6`; this is
uncommitted work on `codex/salvage-implementation`, not an already recorded
checkpoint. Root's fresh upstream audit found no newer main revision.

## Final architecture review

- **Explicit route identity.** `resolveGameMode` handles `[game] mode`, rejects
  unknown/empty values and salvage/WRECKWATER conflicts, and preserves prior
  title-based selection when absent. Native and WASM share it. Malformed quoted
  modes cannot silently select a legacy title.
- **Bounded scene ownership.** The preview owns 32 static bodies, and no
  inventory, receipts, missions or `GameSession`. Reset/Leave retire only owned
  generations and require completed GPU metadata. Accepted removals are not
  requeued; failed creation rolls back only owned bodies. CPU fixtures check
  stale snapshots, reused slots, Leave priority, repeated resets and failures.
- **Both actual reset defects addressed.** The metadata read bound now uses
  `wgpuBufferGetSize`, rather than a shrinking renderer live-index range. The
  application also retains an API reference captured while the preview exists,
  because `PhysicsRenderView` returns no buffers when resident bodies reach
  zero. Backend inspection confirms one metadata allocation at initialization,
  with no Reset/Leave replacement. The reference is released after confirmed
  Leave and before world shutdown; it does not destroy the world-owned buffer.
  GPU copy encoding follows the physics step in the same command stream.
- **A regression exercises the actual boundary.** The final GPU test calls the
  same retained-source helper as Application. It checks reduced live range,
  zero-body worlds, a second completed frame while empty, repeated resets,
  Leave, unrelated generations, rollback and release/recapture. Its retained
  executor log reports a pass; this reviewer did not run that GPU case again.
- **Input/UI cleanup.** Busy transitions reset input, successful Reset restores
  the authored camera/controller, and confirmed Leave restores the saved view
  before browser navigation. The new route isolates playground/throw controls.
  UI tests and actual browser input cover acknowledgement and listener cleanup.
  The journey's final change only waits for positive button bounds and an
  actual DOM hit test before issuing real CDP mouse input; it does not force
  application state or fake successful reset.
- **Narrow catalog correction.** The Winch validator now looks up its socket
  once, checks null/family/role, then compares force with the same valid pointer.
  Numerical-error precedence is retained. The separate
  [expanded independent review](../DATA-02/winch-review.md) supplies 16 passing
  strict sanitizer cases, including missing/wrong-role/wrong-family/weak sockets.
  Its added assertions have their own exact test identity. The final suite
  manifest and source/object/binary timestamps confirm they preceded compilation
  of the passing full-suite executable.

## Completed launch and package evidence

[BOOT-05 implementation](../BOOT-05/implementation.md) and
[final summary](../BOOT-05/final-summary.json) now replace the earlier pending
matrix. The retained final evidence supports these bounded claims:

| Check | Observed result and limit |
|---|---|
| Final native cove | Normal exit 0, 1600×900 screenshot, no application/GPU errors, native SHA `c4dc00f1…`. Launch/exit evidence; no independent native keyboard interaction claim. |
| Browser cove journey | Hardware adapter, actual walking, Reset button, R while W held, pointer release, old-control isolation, Leave acknowledgement and fresh re-entry all pass. One Reset after re-entry passes. Owned/resident bodies return from 32 to 0 on Leave. |
| Browser old routes | Default, LEGO World, shore, terrain and RIDGEBREAK each pass in a fresh visible browser. Separate `lego_patch.html?test` controls switch grouped/individual bricks and drop/reset a ball. |
| Native old routes | Five-route matrix on SHA `75f801bb…` passed before the final retained-source correction. The later change is salvage-specific; the affected cove was repeated on `c4dc00f1…`. Historical routes are not relabeled as that later binary. |
| WRECKWATER | Actual graphical window/surface initialization, authentication and continuing frame-loop logs pass. OS screenshot unavailable; harness SIGTERM exit −15; 783 rejected character frames unexplained. No inspected pixels, clean exit, healthy replication or completed-match claim. |
| Final Bazel native/WASM | Both builds pass, with source/artifact manifests. Real loopback server and byte comparisons pass: 58 preloads, five configs, 17 static references, 27 raw shaders, five actual WASM exports and 48 HTTP files; six negative package controls pass. |

The WRECKWATER report retains commands, native/server/probe hashes, random
credential generation, four-peer topology, success conditions and cleanup.
It is self-contained and does not require stored secrets. The native report's
earlier ambiguous “final binary” wording was corrected during this review.

Final browser WASM SHA is `d4b6575d…`; BOOT-05 manifests give full digests.
Final Bazel artifacts are separate identities in
[final-build](final-build/README.md). Old BOOT-02/03 timings and memory are not
presented as new-preview measurements.

## Scope and known limits remain visible

BOOT-03 preserves the missed 16.67 ms presentation target, sparse completed GPU
tails, partial/non-additive memory domains and historical capture-provenance
gaps. BOOT-04 identifies unavailable hardware, people and release infrastructure
without counting them as tested or funded. WRECKWATER's limitations remain
explicit. None of these bounded baseline tasks certifies the finished game.

The owner rejected the current plain-box cove appearance. LOOK-01 now requires
an actual authored in-engine visual proof before G02 can pass. Final quality
gates remain unchanged. This is an appropriate explicit outstanding requirement,
not a reason to relabel the technical placeholder as successful art.

Completed prerequisites are consistent: BOOT-01–05 support G00; DATA-01/02 and
ASSET-01 have separate independent foundation evidence. DATA-03 and ASSET-02
are still unfinished and excluded from this checkpoint. DATA-04 still needs
DATA-03 and BOOT-05. Later gameplay/campaign/co-op gates remain unchecked.

## Mandatory suite and checkpoint closure

The first complete attempt genuinely timed out at 300.1 seconds; it did not
pass. Its two obsolete shader literals were checked against unchanged shader
sources: the raycaster already stores normal RGB plus LEGO distance W, and both
blit paths extract and normalize RGB before consuming W. The focused corrected
assertions preserve smooth/LEGO normal selection and both consumer paths.
No shader change was needed.

Raising only the combined target's size from medium to large is justified by
the full workload: Bazel defaults those time budgets to 300 and 900 seconds.
[Bazel test-runner specification](https://bazel.build/reference/test-encyclopedia).
The hook and case list remain intact. The failed run and focused correction
are separately retained in the handoff.

The [complete retry](final-full-check/README.md) now **passes with exit 0**.
Independently inspected its Bazel output, test-log footer, XML, summary and
source hashes. Actual command:

```sh
nix-shell --run 'bazel test --jobs=4 //tests:voxy_tests //tools:terrain_diffusion_import_test'
```

The combined C++ target reports **1,495 passed, 3 skipped, zero failures** in
326.9 seconds. XML agrees: 1,502 records including four pre-existing disabled
cases, with 1,498 enabled cases. The skips are unavailable GLFW window creation
and two existing opt-in GPU benchmarks. They do not constitute passed coverage.
The separate tool target is a cached pass; its actual log reports 11 Python
cases with one skip. The retained failed attempt remains distinct.

All ten entries in the final full-check source manifest match current files.
The checked combined binary SHA is
`574bb8e969885d155d9b89764d391bec35a7739eee7f5b44726d9aa3422e5479`.
Expanded Winch assertions precede their object compilation and executable link,
as recorded in the summary. No test filter or newly disabled case hid failures.

**This independent review supports marking G00 accepted and committing its
completed work.** Root must execute the enabled pre-commit hook and record the
actual commit afterward. The checkpoint itself is not yet claimed here.
Unfinished DATA-03, ASSET-02 and visual prework remain outside that commit.
