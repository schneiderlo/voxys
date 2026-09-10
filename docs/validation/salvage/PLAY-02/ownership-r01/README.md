# PLAY-02: the real starter craft belongs to GameSession

2026-09-09, root, `codex/salvage-implementation`, after G00 `7f28fab`.
This removes the identity gap between the live boat and the session. It is a
verified prerequisite to workshop launch, not a completed launch transaction.

## Current implementation

`cove_build.*` prepares the trusted initial cove build before GameSession owns
its world allocator. It uses the actual admitted cooked catalog and the exact
starter craft selected by navigation. The returned bootstrap contains one loan
entitlement, one build, eleven physical parts and seventeen welds, all with
unique world-scoped identities and a checked allocator high-water. The helper
cannot be used as a blueprint import or to restart a running authority.

In the current cove, participant/token/job/cargo already occupy counters 1–4.
The starter entitlement is 5, build 6, parts 7–17 and welds 18–34. Every starter
part is marked as a loan tied to entitlement 5; deleting a loan cannot pay the
ordinary paid-part salvage yield. These numbers describe this trusted bootstrap,
not IDs that UI callers are authorized to manufacture.

`CoveBoatAssembly::compileBuild` accepts the canonical build and explicit
part-to-scene mappings. It rejects missing/duplicate parts and duplicate scene
slots. The source build is retained alongside its derived assembly. Physical
compilation keeps mass, hull, flotation and exact part frames. Initial body
admission now uses this canonical assembly. Dock and independent cargo remain
separate. The original transient compile entry points remain for inspections
and design preview.

`Application::initSalvagePreview` installs the catalog, build, loan entitlement
and ID high-water together when creating the session. No GPU boat exists at
this initial replacement boundary. Read-only telemetry now reports one session
build, eleven build parts, seventeen build connections, and the live boat's
build ID/revision. Reset retains the same build and IDs. Leave still drains the
physical scene; reopening creates a new local session as before.

## Verification

- [Native build](native-build.log) and [WASM build](wasm-build.log) pass.
- [Twenty native cove cases](native-tests.log) pass. The two new cases verify
  the actual canonical starter's ownership and loan entitlement, exact unchanged
  mass/flotation/draft and root-to-part frames, missing-entitlement refusal,
  exhausted identities, and ambiguous render mapping refusal. Existing workshop,
  skiff/dock, actual brick grounding, flotation and player movement cases pass.
- [Thirteen-stage hardware-browser journey](browser-journey.json) passes:
  boarding, helm, real hooking/reeling/payout, sailing, release, steering,
  moving-deck walking, Reset and resize, followed by drained Leave. Every
  recorded stage verifies the owned build counts and matching build ID/revision.
  LEGO terrain stays enabled and no GPU errors are reported. The actual cable
  remains intact through towing.
- The initial native compile failed on unchecked vector indexing in the new
  negative mapping test under `-Werror=null-dereference`. Bounded `.at()` access
  fixed the test; production code already checks mapping counts and indices.

Source digests are in [sources.sha256](sources.sha256). Hardware is AMD Radeon
890M / RADV STRIX1 and Chrome 152 WebGPU. No screenshots, simulation setters,
direct success injection or full gate acceptance are involved. The fixture/header
comment clarifying canonical versus inspection compilation changed after the
successful build; executable logic did not.

Reproduction:

```sh
nix-shell --run 'bazel build -c opt //:voxy_native //tests:voxy_tests'
nix-shell --run 'bazel-bin/tests/voxy_tests --gtest_filter="CoveMovement.*:CoveNavigation.*"'
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_COVE_TERRAIN=1 \
VOXY_SMOKE_REPORT=/tmp/owned-cove-browser.json \
VOXY_SMOKE_COVE_PLAYER=/tmp/owned-cove-journey \
node scripts/smoke_integrated_wasm.mjs /path/to/staged-web salvage-cove
```

The tested package is `/tmp/voxys-cove-ownership-web-r01`. Existing owner-facing
workshop preview server at port 38194 remains undisturbed; it is the previous
design-editor checkpoint, before this ownership change.

## Next: atomic connected edits and live launch

The session now owns the real starter. The workshop still edits a separate RAM
design, and its Keep command does not launch it. `CovePreparationAdapter::begin`
still rejects changed builds; no rebuild activation is claimed. Continue the
remaining requirements in [the launch handoff](../workshop-r01/README.md#required-next-implementation).

The next transaction must support a whole connected edit, including changed
welds, removed/added parts and module configuration. A disconnected intermediate
state must not be published merely to reuse the existing single-part commands.
Existing `BuildModel::replace` correctly forbids retargeting an old connection
ID; a new socket pair requires a newly allocated connection identity.

Important existing boundaries to update together:

- `session_transactions.hpp`: command intent, exact object transition and
  admission-assigned identities; structural value equality for repeat requests.
- `game_session.cpp`: debit/credit reservation, stale lease/revision revalidation,
  checked identity allocation, preparation, history/undo/redo and future execution.
- `session_recovery.*`: owned checkpoints, admission/decision replay, identity
  high-waters, removed-part escrow and exact semantic replay. Volatile receipts
  remain volatile until SAVE work implements real storage.
- `session_events.*`: coherent build observations after the confirmed transaction.
- The record/history budgets are deliberately bounded: current journal records
  have a 32 KiB ceiling, total journal payload 1 MiB and logical recovery 2 MiB.
  Do not append maximum whole-build arrays to every fixed record without checking
  those totals. Use bounded owned payloads and complete accounting if the record
  representation changes; retain allocation-free canonical publication.
- The existing execution adapter stages changes at a genuinely future tick,
  joins actual completion/body/event evidence and then publishes. New hull,
  water/propulsion settings, player collision/helm and render mappings must use
  the same accepted revision. Shape retirement follows completed GPU use.

No catalog economics, identity validation, save requirements, controller scope
or launch-quality gate is waived by this preparatory integration.
