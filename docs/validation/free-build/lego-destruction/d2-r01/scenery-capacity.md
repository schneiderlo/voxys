# D2 scenery collision capacity repair

The refined imported remainder consumes 5,921 fixed query boxes; its selected wall uses 692 cells. The scenery allocator also counted every village visual-admission envelope as a physical collision slot. Hundreds of paving groups have such envelopes, although those envelopes are never published as colliders. This could reject all nearby trees despite available physical capacity.

`CreativeScenery::admit` now captures the owned plus village physical count before appending village admission envelopes. Envelopes remain in spatial overlap checks. Capacity rejection now defers trees instead of permanently recording their IDs as displaced by construction; genuine construction/clearance suppression still persists.

Validation: `bazel test //tests:adventure --test_filter=CreativeScenery.*:CreativeForest.*`: 9 passed, 3 real-terrain cases skipped without `VOXY_ADVENTURE_TEST_TERRAIN`. The new regressions verify physical admission with only 32 spare slots while envelope count exceeds 32, bounded published solids, and procedural-tree recovery after a full budget becomes available. Existing construction suppression and stable forest identity regressions pass.

A separate CPU experiment checked exact adjacent merging by source ID for the imported wall: 692 cells remained 692. The ineffective optimization was removed; physics/query geometry is unchanged. The actual-world near-prop count still requires runtime confirmation after the allocator repair.
