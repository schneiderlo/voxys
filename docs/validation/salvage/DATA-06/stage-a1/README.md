# DATA-06 stage-a1 — bounded observation transport and wire codec

Implemented and locally checked by root, 2026-09-08, on
`codex/salvage-implementation` after G00 commit
`7f28fabfabf3d63726f6cfa1001c3ce1ed557911`. This is a scoped component checkpoint,
not completed DATA-06 or G01. No independent review is claimed. The
[final summary](summary.json) freezes source, build-registration and artifact
hashes. The [wire/reader contract](../wire-format.md) is the implementation handoff.

## Verified results

| Check | Actual final result |
|---|---|
| Optimized Bazel | All 15 event cases pass; existing 86 session + 12 journal cases also pass (cached, unchanged authority sources) |
| Native CMake | All 15 event cases pass |
| Strict native undefined-behavior/float-cast-overflow build | All 15 event cases pass |
| JS-exception/Asyncify WASM under Node | Same 15 event cases pass; no disabled/skipped cases |
| Actual allocation guard in each runtime | 2,000 publications, 1,002 reads, 1,000 codec round trips and 1,000 rejected ingress records; zero allocation attempts |
| Actual hub creation allocation failure in each runtime | First allocation fails cleanly; next run creates one hub with one allocation and preserves identity input |
| Independent binary fixtures | Four explicitly packed golden layouts match C++ encode/decode in both runtimes |
| JavaScript BigInt fixture reads | 22 lossless 64-bit fields and two binary64 samples pass |
| Shipping application integration | Native and WASM applications compile with the new source registered; no scene/art/runtime producer acceptance claimed |

The same implementer authored the Python and C++ fixtures. “Independent” here
means separate explicit wire-field packing, not an independent reviewer.

| Fixed allocation size | Native GCC/libstdc++ | WASM Clang/libc++ |
|---|---:|---:|
| Record | 248 bytes | 256 bytes |
| 1,536 record slots | 380,928 bytes | 393,216 bytes |
| Complete hub, including metadata | 381,400 bytes | 393,688 bytes |

The 384 KiB ceiling applies to slots, with fixed metadata additional. The hub
owns its arrays in one allocation. No whole hub is constructed on the stack.
The application does not yet create a hub for its GameSession, so these numbers
are measured type/creation sizes, not a new measured live game heap increment.

## What is implemented

- Strong event sequence/frontier/incarnation types, explicit version/kind/lane/
  tick-basis fields and token-free public request references.
- Closed fixed-size receipt/state/closure/diagnostic payloads. Explicit error-code
  adapters decouple the wire representation from source-enum ordinal positions.
- An exact 96-byte envelope and bounded payloads, at most 160 wire bytes. Invalid
  versions, kinds, flags, identities, codes, finite-value constraints, absent
  fields, truncated/trailing data and alternate encodings reject. Encoding failure
  preserves the destination. Future physical/presentation kinds remain reserved.
- Separate bounded overwrite lanes: 256 domain, 256 presentation, 1,024 telemetry.
  Readers copy values and receive proposed cursors. Gaps are explicit; an error
  does not copy records, advance a caller cursor, acknowledge storage or invoke a
  callback. One slow reader cannot retain producer memory or affect another reader.
- Out-of-band occupancy/high-water/range/overwrite/invalid-input/exhaustion state.
  Diagnostic counters saturate. Event publication stops at UINT64_MAX without
  wrapping. Domain revisions and ticks cannot regress; telemetry can be delayed.
- A private hub writer capability reserved for GameSession and a public const
  reader. No public constructor or reader method grants access to a live writer.
  Synthetic tests use standalone detail rings; they cannot mutate a session.

The 15 cases exercise hundreds of physical ring wraps, one-slot capacity, fast
and stalled readers, exact gap boundaries, output/cursor preservation, copy
isolation, 10,000 telemetry samples plus invalid ingress, regressed domain
boundaries, repeated equal revisions, arithmetic at UINT64_MAX, fresh incarnation
refusal of old cursors, saturating counts, private reader capabilities, four
byte fixtures, every truncation, trailing/reserved/absent bytes, unsupported
versions/kinds/tick claims, invalid IDs/errors/durability and NaN/Inf/negative-zero
sample rejection. Publication/read/codec allocation is checked using actual
replacement global new/new[]/aligned-new, not an injected fake allocator.

## Preserved failures and final attempts

1. `bazel-attempt01.log`: initial test registration used the library warning-set
   name in the test package. Corrected to that package's existing
   `PROJECT_TEST_COPTS`; no warnings or tests were disabled.
2. `ubsan-build-attempt01.log`: strict GCC rejected an unnecessary native size_t
   cast. Read offsets now narrow through the proven capacity-bounded uint32_t
   domain before becoming size_t, valid on both 32-bit and 64-bit targets.
3. `wasm-attempt01/compile-1-session_events.log`: the initial native record fit
   256 bytes, while libc++'s variant layout made WASM 264 bytes. Grouping the state
   durability flag beside its change bits removes padding. The declared 256-byte
   record and 384-KiB slot assertions remain unchanged. Golden wire bytes are
   unchanged and match again in both runtimes.

Final evidence is Bazel attempt-04, CMake/strict attempt-03, native guard
attempt-02, WASM/guard/application attempt-02, and JavaScript attempt-01. Earlier
successful attempts are historical and remain alongside failed attempts.

## Reproduce

From the repository root in `nix-shell`:

```bash
bazel test -c opt //tests:session_events //tests:game_session //tests:session_transactions --test_output=errors
cmake --build build-salvage-native --target session_events_tests voxy_native -j 8
build-salvage-native/bin/session_events_tests
bash docs/validation/salvage/DATA-06/stage-a1/build-standalone.sh
/tmp/salvage-events-tests
bash docs/validation/salvage/DATA-06/stage-a1/build-allocation-guard.sh
/tmp/salvage-events-guard
node docs/validation/salvage/DATA-06/stage-a1/test-lossless-wire.mjs
```

For actual WASM CPU tests, get SDK/Node paths from
`wasm-attempt02/manifest.json`, and choose fresh output directories:

```bash
python3 scripts/validate_session_events_wasm.py \
  --sdk <recorded-sdk> --node <recorded-node> --output <fresh-shared-directory> \
  --exception-mode js --asyncify
python3 docs/validation/salvage/DATA-06/stage-a1/run-wasm-allocation.py \
  --shared-validation <fresh-shared-directory> --output <fresh-guard-directory>
```

The runners pin inputs/tools and preserve exact commands, logs and compiled
artifacts. WASM uses a fixed 64 MiB heap, 1 MiB stack and the shipping JS exception
and Asyncify policy. This Node test is not WebGPU or a visible browser journey.
The `.hex` fixtures can be reproduced by `make-golden.py`; it implements only
Python explicit field packing and does not invoke/inspect the C++ codec.

## Next integration work — do not treat these as complete

1. Provision fresh public incarnations in the trusted native/browser composition
   root. Require them for normal creation and fresh recovery; do not serialize
   admission tokens or restore an old observer incarnation from a save. Add the
   hub during session creation and ensure allocation failure leaves prior
   authority untouched. Test restored local-token/epoch transitions explicitly.
2. Wire GameSession admission, final receipts and accepted state changes to the
   private writer. Prebuild bounded notifications before commit, publish after
   accepted state/receipt/journal, and never make overwrite fail a committed
   operation. Exact retries, bad ingress and repeated closure must not emit
   extra transitions. Publish trusted entitlement retirement even without an
   advancing request frontier; include undo/redo and job acceptance.
3. Capture one owner-boundary observation baseline: accepted state, visible
   admission/frontiers/receipts, all lane cursors and diagnostic counters. It is
   a presentation copy, not a physics-certified checkpoint or storage ack. Do
   not expose private inverse escrow or secret tokens through that copy.
4. Exercise domain overrun after receipt eviction, canceled/delayed preparation,
   closure/entitlement retirement, fresh recovery and allocation failure while
   copying baselines. A reader replaces model and cursors together. Read/event
   loss must not rerun a command, spend inventory, or grant a reward.
5. Rebuild the actual transaction publication/recovery allocation guards with
   the integrated sources and account for the new creation allocation. Retain
   their frozen historical evidence; do not silently overwrite prior results.
6. Complete DATA-06 review and the separate DATA-05 transaction review before
   G01. G01 still requires headless build/edit/undo/serialization with exact
   inventory semantics, then the repository hook tests and authorized gate commit.

Reserved force/attachment/damage/cargo/reward/audio producers retain their later
SIM/PLAY/MECH/SAVE/SND gates. The cove art remains unapproved under LOOK-01/G02.
