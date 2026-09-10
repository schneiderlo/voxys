# Two-job ownership and archive preparation

2026-09-10. Source parent `e5146785`, with the existing local brick-builder and
camera changes preserved. Scratch: `build-cove-two-cargo-2ohjej41/`.

The next required gameplay milestone is the 700 kg awkward-crate job after the
generator powers the harbor. The current application has one cargo owner and
one job role. Overwriting those with the crate would lose the installed generator
or its receipt. This change extends the physical archive to retain both loads
and checks the existing session's independent delivery/recovery behavior.

This is preparation for the whole two-job loop. **The crate job is not playable
yet. PLAY-06, PLAY-05 and all parent gates remain open.** Existing preview and
save origins remain available; this change does not replace their frozen builds,
publish main, migrate worlds, or claim a new game gate.

## Implementation

`CovePhysicalSave::additionalCargo` and trusted `CoveSaveContext::additionalCargo`
add the explicit second load. SVCE v5 retains the complete v4 root table and
appends one 169-byte record after a four-byte count. It retains the original
generator in the base fields, even when the other load is the selected target.
Both banked objects keep their physical pose and completed job identity. The
logical session removes only the delivered cargo and preserves both receipts.
No logical-session or outer-storage format change is needed.

The codec requires all installed roles, exact definitions, independent motion,
owned logical cargo, completed/banked agreement and valid tow ownership. One
winch cannot retain lines to both loads. The crate's Accepted/Completed state
requires the banked generator and installed harbor. The future command boundary
must check that prerequisite before accepting the job too; archive validation
alone does not authorize a command.

The extra record fits the existing physical-byte allowance with 32 roots.
V1–v4 keep their encoding selection and fields. An old one-job context cannot
load the two-job profile, and an archive cannot discard a load to become legacy.
`CoveRestoreCandidate` explicitly refuses additional cargo until its complete
physical consumers exist. This prevents a partially implemented loader from
silently dropping the crate. The live application still creates one-job contexts.

See [the field-level contract](../../../../salvage-cove-save-format.md).

## Verification

All executed checks pass: 25 archive, 137 session and 56 existing Cove cases
through Bazel; 25 archive cases through CMake; all 198 shared WASM cases with
zero skips or disabled cases. Both applications build. The exact outcomes and
source/binary hashes are recorded in [results.json](results.json) and
[artifacts.json](artifacts.json). Commands run inside the Nix development shell:

```bash
bazel test //tests:cove_save //tests:game_session --jobs=4 --test_output=errors
bazel test //tests:cove_player --jobs=4 --test_output=errors
cmake --build build-native-save-host --target voxy_native cove_save_tests -j4
build-native-save-host/bin/cove_save_tests
EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j4
python3 scripts/validate_session_transactions_wasm.py \
  --sdk /tmp/voxys-emsdk/upstream --node /home/modkin/.nix-profile/bin/node \
  --output /tmp/voxys-two-cargo-wasm-new --exception-mode js --asyncify \
  --expected-tests 198 --use-frozen-sdk-cache
```

Four added archive cases cover the two loads before/after generator banking,
crate towing/broken line/release/delivery, independent identities/motion/stock,
progression refusal, sixteen missing/forged/conflicting cases, bounded decoding,
truncation and downgrade refusal. Existing canonical golden hashes remain
assertions. The added session case delivers the first cargo through the real
command/confirmation boundary, crosses checkpoint bytes into a fresh owner,
accepts/delivers the second cargo, and refuses both duplicate rewards. Its
adapter models physical confirmation; it is not a GPU or disk-I/O claim.

The first shared-WASM runner stopped before compilation because copying the
SDK cache exceeded the temporary directory quota. Its failed log is retained.
The new optional `--use-frozen-sdk-cache` reads the existing SDK libraries with
`FROZEN_CACHE=True`; the compiler refuses cache locking or missing-library
builds. It does not modify or delete the cache. The successful r02 runner
uses a fresh output directory and the same strict warnings,
JS exceptions, Asyncify, fixed 64 MiB heap and all shared test cases.

No screenshots, gameplay-state injection, new physical journeys or broader
performance claims. The current source still needs the following integration.

## Next implementation, in order

1. Admit both authored cargo assets and immutable mission roles. The existing
   `functional-kit/r09/crate/cooked` has a 700 kg, 2.4×1.6×1.6 m collision box
   and an off-center tow eye at (0.5, 0.88, 0) m. Its 100-material salvage reward
   is distinct from the generator's 60. Verify its actual mass, displaced volume,
   tow frames and available mounting options through the compiler; tests here
   use a synthetic cargo definition and do not certify that physical asset.
2. Give runtime cargo a bounded owning collection: compiled geometry, shape,
   body, observed/spawn/delivered motion, retirement, render slot and job identity
   for each load. Rendering, observation, joined save ticks, admission rollback,
   player collision and Leave must include every cargo, including banked ones.
3. Define explicit job activation and save migration. Existing paid parts already
   use IDs 35 onward: never reserve fixed crate IDs over those instances. Retain
   every old part, material, loan entitlement, design and generator receipt.
   Derive expected roles from trusted mission setup, not imported archive fields.
4. Integrate target selection, unlock/accept, Hook/reel/release, delivery/retry,
   both-body restore and Rescue. Keep the installed generator powering the harbor
   while the crate is loose, attached, lost, rescued or banked. Remove the current
   restore refusal only when all those physical consumers exist.
5. Exercise reward → boat revision → crate haul on native and browser with two
   viable designs/routes, at least one stable first-attempt design, exact costs,
   save/reload and no duplicate payouts. Complete the remaining PLAY-05 loss
   requirements, latching/controller scope and human/performance prerequisites
   before checking the full task/gate. Preserve the owner's no-screenshot rule.

Publication note: the verified component above is included in the subsequent [working main checkpoint](../../checkpoints/2026-09-10-brick-builder.md). Earlier local/publication statements describe the original verification time.
