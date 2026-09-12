# Named mechanism renderer handoff

Prepared 2026-09-12 in scratch only. **Not compiled or executed yet.** Root owns
integration, application phases, final tests, playthrough and publication.
No live files, build entries, shader files or GPU ABI were changed. Hard stop:
2026-09-12 07:27:33 UTC.

## Exact files to integrate

The following seven files have matching original copies in `base/` and changed
copies in `overlay/`, relative to this directory. Other files under the shared
overlay belong to root; do not overwrite them from this renderer handoff.

- `src/game/assets/rigid_prefab.hpp`
- `src/game/assets/rigid_prefab.cpp`
- `src/render/salvage_asset_fixture.hpp`
- `src/render/salvage_asset_fixture.cpp`
- `tests/test_rigid_prefab.cpp`
- `tests/test_salvage_asset_fixture.cpp`
- `tests/test_mesh_path.cpp`

All additions use existing compilation units. No CMake/Bazel changes are needed.
`renderer-source-hashes.json` binds the seven original and candidate copies.

## Application API and drive contract

`game::assets::RigidMechanismKind` has `PropellerRotor` and `WinchDrum`.
`RigidMechanismPose` contains `{ kind, double radians }`.
`SalvageFixturePlacement::mechanism` is an optional pose. Omission uses the
unchanged neutral path. Finite angles are reduced modulo tau by the CPU
placement adapter; a zero angle preserves the neutral matrix bytes.

Use only an actually supported role on a live boat part. The cheap support query
is already available: `content->renderBundles()` returns a nonallocating span;
read the selected bundle/LOD's `prefab.mechanism` and its `kind`. Do not query
canonical `content->bundles`, whose art intentionally stays unchanged. After
active admission, all LODs of one bundle have the same optional role. Legacy
static art and prototype placements must receive no mechanism pose.

- Rotor: integrate the successfully submitted effective propulsion command at
  the application's owned fixed ticks. Positive phase turns about canonical +Z;
  reverse command reverses phase. Suggested full-output presentation speed is
  two revolutions per second, as chosen by root. This does not claim physical
  shaft RPM or an engine drive network. Neutral/disabled/pause/workshop do not
  advance phase. Prepare/submit/discard ownership must not double-consume ticks.
- Drum: positive phase turns about canonical +X. The author confirmed effective
  winding radius **0.28 m**. Advance only from a newly accepted observation of
  the exact rope: `phase -= (newRestLength - previousRestLength) / .28`. Motor
  input or wall-clock time must never auto-spin it. First attach, a new rope or
  restored owner establishes a baseline without rotating. Do not consume stale
  ticks, a different generational attachment, paused/workshop observations or
  observations belonging to an inactive owner. These phases are presentation
  state; no save schema or canonical part identity changes are needed.
- Root resolves the exact boat part/root/BodyHandle as before. The renderer
  carries the unchanged handle, including generation, on both mesh draws.

## Art and loader contract

Each articulated LOD contains exactly two mesh-bearing root nodes:

| Role | Exact name | Canonical pivot | Positive canonical axis |
|---|---|---|---|
| Static | `voxys_mechanism_static` | Authored node translation | None |
| Rotor | `voxys_propeller_rotor` | `(0,0,0)` | `+Z` |
| Drum | `voxys_winch_drum` | `(0,.12,0)` | `+X` |

A part contains the static node and one moving role. Both are unparented,
unskinned mesh nodes with identity rotation/scale. Vertices are rebased to the
node pivot; node translation restores the authored neutral placement. One node
can reference multiple material submeshes. Separate root nodes may even share a
mesh: selection is by exact **node identity**, not mesh/material index.

`prepareRigidPrefab` validates reserved names, both roots, numeric pivot and
identity transforms before admission, then stores the CPU-only optional binding.
It derives the source-local rotation axis from the transpose of the declared
render-to-canonical basis. For current basis 12 the axes are `-Z` and `-X`.
`placeRigidPrefab` composes the moving draw as:

```text
camera-relative/live-body root * grid part * declared render basis
    * node translation * rotation(source axis, phase)
```

It keeps every stationary matrix unchanged. The optional pose is a trailing
defaulted parameter, preserving existing callers. Wrong/unavailable role, NaN,
infinity or existing capacity failures leave the caller's output unchanged.

Fixture preparation rejects mismatched roles across a bundle's LODs and roles
on the wrong canonical name (`salvage.part.propeller` or `salvage.part.winch`).
Fixture encode forwards the pose during its normal expansion, before opening
the ticket. Existing MeshPath performs material/normal transforms, shadow
casting, depth and live body resolution; there is no separate mechanism pass.
The same opaque-before-water order, owner and fence/retirement path apply.

## Assets and capacity

Sibling source/cooked assets are in
`build-cove-mechanisms-art-r01/overlay/data/salvage/mechanisms/r01/`.
The author reports propeller 6/6/5 submesh draws and winch 7/7/7, two node draws
per part per LOD. Propeller GPU bytes remain 238,752. The winch's orange rotation
mark adds 68 vertices across all LODs, **4,896 GPU bytes**; existing canonical
geometry changes by at most 7.45e-9 m. Authoring and strict CPU cook checks passed
in the sibling task; this renderer handoff does not substitute for admission.

Projected existing owner after those asset replacements: **10,745,384 bytes**.
Renderer motion adds no GPU bytes: instance stride remains 112, fixed requested
storage 119,336 within the 131,072-byte reservation; owner/resident ceilings stay
16/48 MiB. Both moving and static nodes count against existing 256 mesh-instance
and 512 expanded-draw limits. Root must preserve the full scene/64-brick case.

## Authored focused checks — root must run them

```text
RigidPrefab.NamedMechanismUsesCanonicalAxisPivotAndExactNodeAcrossAllPartRotations
RigidPrefab.MechanismContractRejectsMissingDuplicateHierarchyAndWrongPivots
RigidPrefab.MechanismPhaseRefusalIsAtomicAndDoesNotRelaxExistingCapacity
FixtureGPU.NamedMechanismPhaseUsesOwnedTicketWithoutAdditionalGpuReservation
FixtureGPU.RejectsWrongCanonicalMechanismRoleAndMissingRoleInAnotherLodBeforeUpload
MeshPathGPUTest.NamedMechanismsKeepStationaryNodesAndMatchLiveBodyAtLargeSectors
```

CPU cases independently calculate signed quarter-turns for both roles under all
24 part rotations, basis 12, translated/rotated roots including negative offsets,
exact winch pivot, stationary-node isolation even with a shared mesh, zero-byte
equality, wrapping, malformed contracts and atomic refusal.

Fixture cases require unchanged requested bytes/draw counts across changing
phases, owned submission completion, untouched ticket on refusal, and complete
LOD-role/canonical-name validation before upload.

The numeric GPU case renders both roles with distinctive green stationary and
orange moving regions. It requires a visible motion difference, exactly unchanged
stationary pixel masks, agreement between static matrices and a COM/principal-
rotated live GPU root at sector `(1000000,-2000000,3)`, and blank output for a stale
body generation. The test writes only numeric evidence unless the pre-existing
optional `VOXY_MESH_CAPTURE_DIR` is explicitly set; root should leave it unset.

Existing budget/full-scene and shadow/lifecycle checks remain relevant to final
integration. Application drive-state tests and the one combined native/browser
mechanism journey are root's work. No new screenshot sequence is needed.

## Review and limits

The source-only whitespace check produced no diagnostics. The asset author
independently reviewed this adapter against actual cooked node names/transforms:
no incompatibility found; winch Y=0.1199999973 fits the explicit 1e-6 tolerance.
No compile, CPU test or GPU execution of these source changes has occurred yet.
General skeletal animation, arbitrary rig hierarchies, physical shaft dynamics,
collision changes, engine networks, character animation and ACT gates are outside
this bounded mechanism-presentation change.
