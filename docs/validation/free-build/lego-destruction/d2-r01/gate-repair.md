# Focused repair of the six D1 gate failures

Date: 2026-09-18. Scope: stale expectations in `tests/test_salvage_asset_fixture.cpp` only. No renderer behavior or capacity changed.

## Authoritative behavior

- `src/render/scene_shadows.hpp` asserts a **208-byte** `SunShadowUniforms` ABI: near matrix/parameters, world origin, two foot contacts, far matrix/parameters. `MeshPath` actually allocates this size.
- `SalvageAssetFixture::fixedGpuRequestedBytes` includes one such uniform in each of its two paths. The old test assumed 96 bytes each: **138616 + 2 × (208 − 96) = 138840**. The existing 144 KiB reservation and 16/48 MiB owner/resident caps remain unchanged.
- Live sun-shadow storage reserves two uniforms. Its old expectation assumed 48 bytes each: the complete owner fixture expectation increases by **2 × (208 − 48) = 320**. Exact owner admission still rejects a ceiling one byte below the required amount.
- `MeshPath::render` keeps all expanded instance records, sorts them by material/depth, and combines compatible consecutive records into instanced GPU draws. Compatibility includes asset, submesh, material, alpha mode and shadow participation. `SalvageAssetFixture::encode` publishes actual batch counts from the model/guide paths. It continues to enforce expanded-instance ceilings *before* batching.

## Tests retained and strengthened

- Exact storage totals, unchanged ceilings, unique-upload counts, one-byte-under-budget refusal and ownership retirement remain checked.
- Complete Cove retains **153** expanded authored records and **103** actual batches. With the supported 64-brick build plus palette, it retains **219** expanded records and **105** batches. Added explicit sums of admitted prefab counts to keep both concepts checked separately.
- The 99-placement case verifies the input count and one-node/one-submesh prefab, then expects one instanced batch.
- Socket inspection retains **17** beams and **510** guide boxes, with two batches across the separate model/guide paths. The existing next-placement overflow and atomic refusal checks remain intact.
- Mechanism animation retains two authored nodes/expanded records and distinct placed transforms at all three tested phases. Both nodes share one opaque submesh batch. Existing invalid-role/nonfinite-phase and ticket-lifetime checks remain intact.
- Kept the pre-existing correction to the live-shadow reservation formula (that edit preceded this task).

## Validation

`nix-shell --run 'bazel test //tests:salvage_asset_fixture --test_output=errors'`

**33/33 passed**, including all six previously failing cases and all 22 hardware GPU cases. GPU: AMD Radeon 890M (RADV STRIX1), Vulkan. No skipped tests. Test time 5.579 seconds; Bazel wall time 16.118 seconds.

Full captured test output: [salvage-fixture-tests.log](salvage-fixture-tests.log).

This focused run resolves the known fixture failures. It does not substitute for the complete normal commit hook or certify D2 destruction integration.
