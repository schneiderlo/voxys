# DATA-03 independent correctness review

**Review result: no remaining correctness defect identified in the bounded
DATA-03 implementation. The refreshed strict standalone build and all 22 tests
pass.** One ambiguous winch field was clarified during review, with root's
approval and a regression assertion. Shared optimized/CMake/Bazel integration
remains root's responsibility; this review does not check off the task.

Reviewer: `simulation_production`, 2026-09-07. Reviewed the complete
`build_model.hpp`, `build_model.cpp`, `test_build_model.cpp` and task report;
checked DATA-03's full deliver/pass criteria and architecture contracts A–C in
`GAME_IMPLEMENTATION_TODO.md`. Consulted the DATA-01 counter/transform helpers
and DATA-02 catalog validation, sorting and socket-matching contracts.

## Resolved contract issue

The original `ModuleSettings.lineLengthMillimetres` and
`Connection.restLengthMillimetres` both appeared to be the cable-length authority.
The accepted rope fixture intentionally combined a 500 mm module value with a
5,000 mm connection value, leaving a future adapter uncertain whether applying
module configuration should shorten an attached cable.

The author renamed the module field to `defaultLineLengthMillimetres` and
documented that it applies before attachment; an existing connection's rest
length takes precedence. See `build_model.hpp:32` and the report's configuration
section. The additional assertions at `tests/test_build_model.cpp:191` verify
both independent values survive encoding/decoding. The wire position and schema
are unchanged. This resolves the ambiguity without silently changing an existing
cable target or asserting that this data module already executes a rope solver.

## Correctness findings

- **Exact geometry:** signed-permutation transforms keep extrema exact without
  a floating-point conversion (`build_model.cpp:312`). DATA-01 checked arithmetic
  rejects invalid rotations and coordinate overflow. Socket frame composition,
  local-X half-turn mating and enabled-weld insertion regions match the catalog
  contract. Clearance exceptions cannot exempt solid-solid interpenetration or
  third-party intrusion (`build_model.cpp:176–225`). Rope/latch records remain
  authored intent and do not certify live capture or merged mass.
- **Bounded decoding:** `decodeBuild` bounds total bytes and fixed header length,
  then bounds part/connection counts and requires the exact expected record size
  before count-driven reserves (`build_model.cpp:407–425`). Counts are bounded
  before their fixed-size products, so malformed u32 counts cannot overflow the
  size computation or request unbounded allocation. Reader calls use fixed
  integer widths and checked remaining bytes. Invalid booleans, unused lease
  payloads, enums, field values, ordering, duplicate IDs, trailing data and
  truncation cannot publish a model.
- **Atomicity:** validation builds owned local canonical and derived vectors.
  `replace` checks both revisions, revision exhaustion, build identity and
  surviving part/connection identities before moving in the replacement
  (`build_model.cpp:337–369`). Encoding and decoding also publish only after
  validation. Standard allocator vector/optional moves used at publication do
  not allocate. Tests cover logical failure atomicity; allocation-fault
  injection was not executed in this review.
- **Identity and revisions:** physical build/part/connection IDs are unique in
  each snapshot; parts and connections share the build namespace. Exact content
  versions resolve through the immutable catalog. Existing parts cannot change
  definition/owning-build/provenance, existing links cannot change endpoints or
  kind, and same-edit part/connection ID reuse is rejected. Initial revision 0
  is valid by DATA-01; UINT64_MAX is readable but cannot advance. Historical ID
  reuse, participant authentication, owner/lease authorization, entitlement
  issuance and inventory debit are explicitly deferred to GameSession rather
  than claimed as guarantees of the model API.
- **Canonical and design data:** records/endpoints are sorted before encoding;
  decoding rejects noncanonical order. Four finite positive connection strength
  values are bounded by each socket and definition. Design duplication returns
  separate ordinal-based records without physical IDs, condition, provenance,
  entitlement, owner, lease or revision, and accepts no allocator/inventory.
- **Work bounds:** parts, connections, derived proxies/sockets and candidate
  checks each have independent bounds. A sorted X sweep skips separated solids;
  no dense .02 m cell allocation is present. These are authoring rejection
  budgets, not measured solver or shipping performance guarantees.

## Non-blocking test recommendation

Before persisted schema-1 files become a public compatibility commitment, add
one complete independently authored golden byte fixture covering a lease,
nondefault module settings, loan provenance and a rope connection. Current
tests at `test_build_model.cpp:377–443` check round trips, several literal byte
offsets and malformed inputs, but a coordinated encoder/decoder field-order
change could still preserve their round trip. A full external fixture would
protect the documented wire layout itself. No encoding mismatch was found by
this source review; this is additional compatibility coverage, not a current
functional blocker.

## Executed validation and identity

The final independent run uses both the winch-default clarification and root's
checked-pointer catalog correction. It does not substitute the earlier run
against the old dependency.

| File/artifact | SHA-256 |
|---|---|
| `build_model.hpp` | `e99cf8d8b6a409276195a49d1887d6ee4518683b0cc4fa5550af08a7d3746f19` |
| `build_model.cpp` | `5307ee045a24f3974ec109f914eb5f8d5a4c65755a32f7286ff6308be9cd508f` |
| `tests/test_build_model.cpp` | `8de3c9ea0c3c3c5bd8ecca390dbe44160c83af77e0bbde1f45000eefd88aacdf` |
| `part_catalog.cpp` dependency | `24869c7c346d49421b2fe20123181e4e5738d6e6ddc366339292d3f12044bcf3` |
| `/tmp/salvage-build-model-independent-review` | `ff5fd14d1cd6db6a96bad436f60fd19bdd289f878ca72033fa37c9fbf92366a4` |

From the repository root:

```sh
SALVAGE_CXX=/nix/store/788mx070y81zjlg5ipcl0cra3afviw9k-gcc-wrapper-15.2.0/bin/g++ \
  SALVAGE_TEST_BINARY=/tmp/salvage-build-model-independent-review \
  bash docs/validation/salvage/DATA-03/build-standalone.sh \
  > docs/validation/salvage/DATA-03/review-compile.log 2>&1
/tmp/salvage-build-model-independent-review \
  > docs/validation/salvage/DATA-03/review-tests.log 2>&1
```

The script applies the full repository GCC warning profile, `-Werror`, `-O1`,
undefined-behavior/float-cast-overflow sanitizers and immediate sanitizer failure.
[Compile log](review-compile.log): exit 0, no diagnostics.
[Test log](review-tests.log): 22/22 pass, no sanitizer diagnostics.
[Earlier test output](review-before-clarification-tests.log) is retained separately.
No CMake/Bazel build, GPU test, Windows/browser test, command-authentication test,
inventory execution, save-journal recovery or performance measurement was run
by this reviewer. No shared build files, model source, task checkbox or commit
was changed by the reviewer.
