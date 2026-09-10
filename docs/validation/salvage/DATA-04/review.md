# DATA-04 — independent session-authority review

Reviewer: `lego_gameplay`, 2026-09-07. Scope: the renderer-free session header,
implementation, twenty-case test source, DATA-03 dependencies, task acceptance
and the author's design/evidence. No product or shared-build source was edited.

**The corrected core is approved for its bounded DATA-04 scope.** The initial
bootstrap reference defect is fixed: all eight independently reproduced bad
seeds now reject, and the reviewer reran the fresh strict/sanitizer suite with
**21/21 cases passing**, exit 0. This review does not substitute for root's
integrated native/WASM build and actual Application lifecycle validation, or
for the separate Application bridge review. Keep whole-task acceptance pending
until that evidence is complete.

## Confirmed pre-fix defect: an issued number is not a participant

`validateBootstrap` in the original `game_session.cpp:96–151` used
`validReference` for build owners, edit-lease holders, cargo owners and accepted
job participants. That predicate checks a valid ID, the world namespace and
`counter <= lastIssuedId`. It does not establish that the reference names a
participant. The single-participant bootstrap has no participant registry.

As a result, all four fields accepted either the session-token ID or an
arbitrary unregistered counter below the allocator high-water mark. This
contradicts the declared checked owner/reference boundary even though the
shipping cove's empty bootstrap did not exercise it. The original foreign-owner
test also depended on acceptance of such an unregistered owner before checking
command rejection; passing that test did not prove valid seed ownership.

The bounded correction approved by root now requires those participant
references to equal the admitted `caller.participant` in this local profile.
Available jobs still require zero `acceptedBy`. A future multi-participant
bootstrap needs an explicit checked participant registry; it must not restore
the numeric-range-as-existence rule.

### Independently reproduced behavior before the fix

Copied the original session header/source into
`/tmp/salvage-data04-review-before/src/game/expedition/`, then compiled a small
CPU-only probe against that snapshot and the unchanged construction modules.
The probe constructs a valid catalog, participant ID 1, token ID 2, issued
high-water 100, build ID 3, accepted job ID 5 and cargo ID 6. For each field
below it replaces the otherwise valid participant reference with ID 2 or 77.
No command or preparation adapter is activated. All eight invalid bootstraps
were accepted, with `SessionIssue.error == None`:

```text
field=0 owner_counter=2 accepted=1 error=0
field=0 owner_counter=77 accepted=1 error=0
field=1 owner_counter=2 accepted=1 error=0
field=1 owner_counter=77 accepted=1 error=0
field=2 owner_counter=2 accepted=1 error=0
field=2 owner_counter=77 accepted=1 error=0
field=3 owner_counter=2 accepted=1 error=0
field=3 owner_counter=77 accepted=1 error=0
```

Field mapping: 0 = `BuildSnapshot.owner`; 1 = `editLease.holder`; 2 =
`CargoRecord.owner`; 3 = accepted `JobRecord.acceptedBy`. The source probe exits
0 only when all eight bad seeds were accepted, so its successful exit records
successful reproduction of the defect, not successful validation.

The reviewer also executed the author's original strict/sanitizer binary with
`--gtest_brief=1`: **20/20 existing cases passed**, exit 0. This establishes the
test gap without changing those historical results.

| Pre-fix artifact | SHA-256 |
|---|---|
| `game_session.hpp` | `a4c87882f3c6fa2e2d9b4c4c55affce56fb2b6b5af92699c8554e6c42e9844f9` |
| `game_session.cpp` | `6caafb139cc651d89956678dfda76398a1bdfc3a371520ea3a50c0ac273b8360` |
| `test_game_session.cpp` | `e60804c0fab2baa1e081292e4f4951bc71dcc815fea632f36f90f2a6e2eb9d62` |
| Original strict binary | `86228aa2cc7da3d31c57300598436c9e5353ea807fc6fc6c64cef83505310419` |
| Independent probe source | `8159a8fa716fc809264ffd6a37169ec9902621878ff794a66333d6258d6fd5c2` |
| Independent probe binary | `01121d44dd7fd5137a79d7f065d0015cb6d5d188d148cfdc73daed5535ead50a` |

Temporary probe paths are a convenience on this host, not a retained portable
build artifact. The exact bad inputs and observed results above are retained
here; the corrected production regression must cover the same eight cases.

## Other source findings

The private staged-state design satisfies the bounded fake-adapter scope:

- Commands contain intent, not caller-provided price, provenance, balance,
  authoritative snapshots or cargo completion assertions. Public world queries
  return copies; the catalog is immutable and build replacement stays private
  to the authority.
- Accepted geometry, accounts, cargo, jobs and revisions remain unchanged while
  preparation is pending. Catalog costs/yields are used exactly. Loan removal
  yields zero; connected removal rejects. Failed/canceled preparation can burn
  an ID and retain a rejection without publishing an owned part or spend.
- Authentication, epoch and sequence admission precede mutation. Typed request
  equality detects conflicting reuse. The fixed terminal-receipt window does
  not remove the contiguous processed marker, so an evicted request cannot
  execute again. Busy and sequence-gap ingress do not consume a sequence.
- Cancellation and closure discard once. A stale same-session preparation
  generation cannot publish newer work. Allocation failure discards an entered
  adapter reservation; an unexpected adapter exception also discards and then
  propagates before publication. That exceptional attempt retains no terminal
  receipt, so its same-sequence retry is deliberate and uses a fresh ID.
- Only the trusted explicit boundary advances the fake tick. Commit rechecks
  revision, ownership, lease expiry and funds, then calls `canActivate`. Candidate
  allocation and receipt capacity precede activation. The final activation,
  pointer swap, trivial bounded receipt append and destruction require no
  further allocation under the adapter's no-throw/non-reentrant contract.
- Catalog/build limits keep aggregate logical counts in range. Overflow checks
  cover IDs, tick and revisions, credit addition and preparation generations.
  The fake adapter's four slots demonstrate pending/retiring capacity; they
  are not an analytical estimate of GPU bodies, contacts or journal bytes.

No additional algorithmic defect was found in those paths during this review.
That is a source assessment of this small authority, not a full game security,
physics, persistence or concurrency certification.

## Explicit lifetime and scope boundaries

Preparation tickets currently contain only epoch and per-session generation.
A new session restarts the generation counter. Reusing one adapter while old
active or retiring resources remain could therefore alias a previous session's
ticket if its epoch is reused. The actual cove creates a fresh adapter for each
session. The corrected header now makes the interface contract explicit: an adapter
belongs to one session lifetime and may not be recycled while old tickets or
resources remain. A future shared backend must namespace tickets by session
identity or otherwise enforce a non-repeating epoch; the current ticket alone
does not provide cross-session uniqueness.

One admitted participant and one pending operation prevent another accepted
command from replacing the world during preparation. Revision revalidation is
present, but this review does not claim the future multiple-source compensation
case was exercised. DATA-05 must add that adversarial test when it introduces
the additional mutation source. The author's README correctly states this
limit despite the broader future test described in the design.

All receipts, processed markers, loans and allocator state are in-memory.
Snapshots are not a complete save format. Durable acknowledgment, persistent
retired tokens, replay across reload, undo/redo, active cargo mechanics and real
GPU scheduling remain their later tasks. The real cove must keep its empty
bootstrap, disabled workshop and rejecting preparation adapter; headless seeded
cargo/jobs must not become shipping progress through this integration.

## Final independent verification

Read the complete corrected source delta. The factory now rejects a build owner
or lease holder other than the admitted participant, and applies the same rule
to cargo owners and accepted-job participants. The new
`BootstrapParticipantReferencesRejectRoleAliasesAndUnknownIds` case checks all
eight original bad combinations and a valid local accepted-job/lease seed.
The previous tests no longer seed nonexistent foreign participants merely to
reach a later command check. Existing local ownership and available-job tests
remain intact.

Recompiled the independent probe against the corrected source, changing only
its final expectation from eight accepted bad seeds to zero. It exited 0 and
reported:

```text
field=0 owner_counter=2 accepted=0 error=3
field=0 owner_counter=77 accepted=0 error=3
field=1 owner_counter=2 accepted=0 error=3
field=1 owner_counter=77 accepted=0 error=3
field=2 owner_counter=2 accepted=0 error=1
field=2 owner_counter=77 accepted=0 error=1
field=3 owner_counter=2 accepted=0 error=1
field=3 owner_counter=77 accepted=0 error=1
```

Error 3 is `InvalidIdentity`; error 1 is `InvalidBootstrap`. No adapter operation
was invoked. Both probes used GCC 15.2, C++20 and `-O0`, directly compiling the
session plus construction types/catalog/build-model sources. The corrected
probe is `/tmp/salvage-data04-review-before/probe-fixed.cpp`; the final
production regression preserves its essential input coverage in the repository.

The reviewer independently executed `/tmp/salvage-game-session-tests
--gtest_brief=1` after the author's strict/sanitizer rebuild:

```text
[==========] 21 tests from 1 test suite ran. (0 ms total)
[  PASSED  ] 21 tests.
```

The displayed sub-millisecond suite duration is the test runner's rounded
report, not a gameplay or render performance measurement. Exit status was 0,
with no sanitizer diagnostic. The reviewer did not run a second build of the
whole strict suite; the independent compilation was the focused eight-seed
probe. The author's [build recipe](build-standalone.sh) records the complete
warning/sanitizer flags and permits a new output binary for reproduction.

| Corrected reviewed artifact | SHA-256 |
|---|---|
| `game_session.hpp` | `e1b7e1d33861db4847ddc5e631c1ec7506017c606955a125dd2e8b6fc550aa16` |
| `game_session.cpp` | `35e5306eae4c117ebb462807303c4474af76bb8afc65d8dff7e375ecfdeb579c` |
| `test_game_session.cpp` | `0c5fdf72b148efdd61095b5a47a91fe7bd670a6df98e393dce2448395a0cb7a0` |
| Fresh strict/sanitizer binary | `049db6993ba3a226431d09472545345ead261d3da078ab5fe3a5ca213d88e849` |
| Independent corrected probe source | `252d4583058be8b3e96b9e3b81b2bf1739961d5ca6ca167d622b4202d4cb19dd` |
| Independent corrected probe binary | `3b386a2149f492e2c142d76ff32b71fd1a6625fc9ebce89b0ffb943755cf7cbd` |

The construction dependencies remain the hashes recorded in the author's
[source manifest](source-hashes.txt). This review approves the corrected core
at the identities above with no remaining blocking source finding. Root still
owns fresh integrated optimized Bazel/CMake/WASM checks and the actual
native/browser lifecycle evidence; no such result is inferred from a CPU mock.
