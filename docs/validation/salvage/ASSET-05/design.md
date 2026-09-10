# First functional kit — preparation contract

2026-09-08. Root owns the new recipe, isolated candidates, fixture data and
integration checks. ASSET-04 has scoped implementation evidence but awaits
independent review. D18 permits this preparation; ASSET-05/G02 acceptance and
publication remain open. The owner's rejection of the primitive cove remains
in force. These models do not yet replace that cove or activate authored physics.

## Deliverable

Nine original authored parts: beam, plate, engine, propeller, helm, fixed winch,
cargo cradle, generator and awkward crate. Reuse the exact staged pontoon
v2-rc01. Keep every prototype v1 definition and historical asset unchanged.
New equipment uses the existing salvage namespace and explicit version 2;
cargo uses a separate namespace/version 1. All are inspection candidates.

One recipe generates editable Blender files, embedded-texture GLBs, three LODs,
canonical sidecars and neutral authoring previews. Use the existing strict
salvage-rigid-v1 converter and sidecar validator. Retain executable/source
digests and failures; never silently fall back to the legacy importer.

Canonical metres/+Y up/−Z forward, .02 m placements and the existing keyed
.18 m peg/.20 m well convention apply. The Blender bridge remains (-x,z,y),
then the exporter and recorded proper rotation 12; metadata is already
canonical. Coarse collision omits decorative pegs/wells. Authored solid cores
displace water for open machinery; only the existing pontoon is sealed.
Mass properties are an explicit uniform-density proxy approximation computed
about the composite COM, not measured internal machinery or render-triangle
mass. Overlapping mass boxes are prohibited. Record visual/proxy limitations.

The beam is a low .32 m crossmember; the plate is .32 m deep. Their four mount
columns are at x = −1.5, −.5, .5, 1.5 m. This avoids the overlapping coarse
wells caused by retaining the prototype's extra half-pitch central socket.
An outboard engine has an upper housing, mounting bracket and lower leg. Its
shaft/propeller sit behind and below the deck instead of through the deck.
Cargo has a top tow eye and underside latch; it does not masquerade as a
functional player engine or grant mission/economic behavior at this stage.

Use the existing cream/teal/coral/slate/steel atlas and separate linear MR/normal
maps as preliminary materials. One joined mesh/material per LOD bounds draw
count. Near/middle/far geometry budgets are 12,000/6,000/3,000 triangles and
24,000/12,000/6,000 exported vertices; maps 128/64/64 pixels. The maps carry
palette colors and fine procedural grain, while recognizable details are
geometry. This keeps all eight skiff bundles inside the existing 16 MiB GPU
owner limit; the pontoon retains its inspected 512/256/64 maps. These are kit
inspection ceilings, not final performance acceptance or ASSET-06 approval.
Keep socket mating geometry compatible between every LOD.

## Integration and verification

Produce separate narrow/broad skiff registries within the existing eight-bundle,
32-placement limits. Both use the same modules and exact socket connections;
only flotation spacing changes. Cargo inspection can use its own registry.
Do not raise global capacities just to show the kit. Validate with the actual
bundle loader and BuildModel; verify contact/buoyancy metadata and cargo socket
families/frames with the shared catalog/compiler. Negative shifted/wrong-key
connections must refuse. Latch frame checks are authoring checks; BuildModel's
latch record does not certify live capture or momentum merging.

Inspect authoring previews, then actual native/browser views and motion through
the fixture path. Prove scale, visible wells, readable silhouettes, outboard
clearance and two assembled layouts. Retain known remaining defects. An
offline render cannot pass LOOK-01. Live mechanics remain SIM-02–09; the
walkable composed cove and water-ordering work remain LOOK-01/REND tasks.

Record completion only for implemented, verified scopes. Independent review,
prerequisite acceptance and gate commits remain required by the main plan.
