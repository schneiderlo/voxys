# D3 cannon impact candidate — 2026-09-18

**A verified local knock-out, not a completed destruction gate.** The actual-world
cannon shot releases eight original parts. The struck brick moves 2.640758 studs
inward and down, using 18.889066 game joules, then settles after 136 ticks.
Nearby pieces mostly remain supported. The test verifies changed walking
collision, exact rebuild and recovered saving. Whole-house collapse, a walk-through
opening, durable damage and browser acceptance are not established by that test.

## Active assets and interaction

The installed package is `data/adventure/ldraw-blacksmith-ground-r01/`, replacing
the old 39-part upper-wall selection in runtime packaging. All 2,140 original
source identities remain represented. The new section has 20 source parts,
18 eligible pieces, two unknown-support plates held fixed, 30 internal bonds,
44 boundary bonds and 6,409 fixed-remainder collision boxes. Its 11-node mesh is
81,656,239 bytes, SHA-256
`4ee13dc6e14e8c33a6e7c2507c23db986e6e81fe777f5304b727ae37a45232c5`.

**Visit cannon** checks a supported clear landing near world X/Z `(1240, -1027)`
and then enters the same cannon control used by **C**. **A/D** aims horizontally,
**W/S** changes elevation, and **Space / Fire** fires. The default trajectory
strikes source `93361846531299384`. Only one shot is outstanding. Leaving waits
for the shot and for changing wall geometry; movement cannot bypass that lock.
**Rebuild wall** restores the source assembly and saving. Damage remains
session-only. Manual release/support-removal actions remain test-only.

## Physical ownership and approximation

The runtime consumes ordered, complete, certified GPU event batches and rejects
stale incarnations, gaps and overflow. Body generations and accepted geometry
revision must match. The target face identifies the original source part through
its compiled shape label; a raw event source ordinal is not an LDraw identity.

A hit releases validated connected neighbours within 2.5 studs in their current
accepted poses, with at most eight directly selected pieces and at most 32 total
pieces after support propagation. Long pieces use distance to their oriented
catalog envelope, not just their centres. Unsupported external attachments stay
fixed. No source mesh is silently replaced with generic debris or removed.

The ball already contacted the intact static wall. The replacement transaction
consumes that ball and admits the released roots together. This is a **delayed,
dissipative approximation**, not one coupled projectile/fracture solve. Initial
fragment energy is capped at the smallest of 36 game joules, 20% of initial ball
energy, and 20% of measured closing kinetic energy. Total transferred linear
impulse is at most 20% of measured normal impulse. COM speed is capped at 12
studs/s and angular speed at 10 rad/s. Only the struck root receives the direct impulse and off-centre torque, using
its packed mass/inertia and the actual contact lever. Released neighbors start
at rest; the GPU propagates forces through contacts and gravity. The
energy ledger includes both translation and rotation. See
[component tests and transfer details](impact-components.md).

The GPU then owns gravity, contact response, friction, motion and sleeping.
Certified settlement publishes static walking/camera geometry at the accepted
revision. Re-impact uses the current settled pose. No forced sleep or arbitrary
position kick substitutes for physical movement.

## Evidence and its limits

| Evidence | What it establishes |
| --- | --- |
| `impact-packet.*` | Initial motion and transferred projectile validation before admission |
| `impact-transaction.*` | Atomic GPU root motion admission and projectile retirement |
| `impact-owner.*` | Source-preserving owner release, bounded energy, settled re-impact and rebuild; inputs are manufactured certified hits |
| `impact-long-support.*` | The nearby end of a rotated long support plate participates without releasing unknown support |
| `impact-torque.*` | Packed-inertia torque and combined linear/angular energy bounds; three focused GPU cases pass |
| `contact-dense-before.log`, `contact-dense-after.*` | Real missing-hit regression fails before dense contact packing and passes after |
| `contact-event-ring.*`, `contact-event-lifecycle.*` | Two existing event-stream and four event-lifecycle regressions pass |

The [contact-event regression](contact-dense-regression.md) explains why the
cannon physically bounced while gameplay previously saw no hit. This correction
does not change CCD trajectories or the solver.

The installed-world check uses the actual 8,192-square terrain, ordinary start,
**Visit cannon**, and C/aim/Space controls. `actual-world-impact.log/xml` passes.
It records 18.889066 J, 2.640758-stud source-pivot displacement and 136 settlement
ticks. The 747 vacant old cell centres include omitted stud collision; that count
alone is not accepted as motion. The independent source-pivot assertion remains
>0.5 stud. Pause/interaction locks, intact collision restoration, save recovery
and resumed construction also pass.

The failed intermediate response applied torque to all released neighbors. They
pressed into each other, raising contact friction and stopping the hit piece
within 0.052 stud. Giving only the struck root the physical contact impulse fixes
this with less energy. `localized-owner.log/xml` covers four passing GPU tests,
including a centered strike that must not invent neighbor torque.

`released-shell-assembly.log/xml` covers 17 CPU tests. The ReleasedShell profile
removes selected stud strips only, preserving all five shell boxes, source/render
identities, full-proxy mass, COM and inertia. Cuts/re-impacts preserve the profile;
rebuild restores Detailed. A real compiled-remainder test proves no initial shell
overlap. This is an explicit gameplay approximation of released clutch, not exact
ABS deformation. Source appearance is unchanged.

## Remaining acceptance

- [x] Make the actual-world shot produce meaningful source-part motion; preserve
  the >0.5-stud regression rather than accepting collision-only changes.
- [ ] Verify the installed browser: Visit cannon → Fire → visible pieces moving →
  certified settlement → Rebuild, with an independent visual critique.
- [ ] Verify the exposed geometry against real player and bike queries. A route
  into the room is not proved even with the selected pieces absent: retained
  corner hardware restricts clearance. Do not erase unrelated collision.
- [ ] Recheck all native/manual variants, source preservation and relevant GPU
  regressions after the final physical change.
- [ ] Run the normal repository gate and commit only accepted scope. No D1–D3
  completion, full-house/roof collapse, durable damage or release is claimed.

## Reproduce native integration

From the repository root in the Nix shell, build `//tests:adventure_runtime`.
Set `VOXY_ADVENTURE_TEST_WORKSPACE` to the absolute repository root and
`VOXY_ADVENTURE_TEST_TERRAIN` to the installed 134,217,728-byte
`td_seed_1234_8192.r16`. Then run:

```sh
bazel-bin/tests/adventure_runtime \
  --gtest_filter='SourceHouse/ImportedWallRuntimeIntegration.*CannonImpact' \
  --gtest_output=xml:/tmp/voxys-cannon-impact-ground.xml
```

The complete source-house group also includes manual connection-release and
support-removal variants. Without the explicit terrain opt-in, these installed
world tests skip. The ordinary commit gate is still
`bazel test //tests:voxy_tests //tools:terrain_diffusion_import_test`.
