# Village forest buffer — recipe 3

A fixed meadow follows the village footprint, including the arrival lane,
blacksmith and cannon. Procedural trunks stay at least 60 units outside the
planned plots. An irregular 90-unit transition brings back full forest density
at 150–168 units. The authored orchard and small village tree groups remain.
The buffer is attached to the settlement, not to the moving player.

Native test and WebAssembly builds passed. All 19 forest, scenery and real
village traversal checks passed. The new regression checks clearance around
seven independent landmarks from three streaming positions and verifies the
surrounding woodland remains populated. The dense-forest streaming test now
starts outside the intentionally open village meadow.

The browser was visually checked facing the bay and facing the village. The
village-facing preview shows open space around the homes, fields, blacksmith
and approach, with forest continuing beyond. Placement recipe 3 was confirmed
in runtime telemetry. The original camera preference was restored afterward.
