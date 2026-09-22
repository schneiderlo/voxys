# Certified-impact component gate

These tests exercise the real GPU backend and source-aware wall owner. The owner
fixture supplies manufactured, already-certified impact inputs; it is **not**
proof of game input, projectile flight or event routing. The runtime integration
must establish those separately.

- `CannonPhysicsScene.InitialMotionAndTransferredProjectileAreValidatedBeforeOwnership`:
  static/nonfinite motion and invalid projectile handles fail before ownership.
- `CannonPhysicsSceneGpu.ReplacementTransfersRootMotionAndConsumesProjectileAtSameTick`:
  root motion reaches the GPU through one COM/principal conversion; old roots
  and the projectile retire at the same replacement tick. Clearing an unadmitted
  replacement also retires the transferred projectile. Pass, 8.7 seconds.
- `ImportedWallGpu.CertifiedImpactNeighborhoodPreservesSourcesAndBoundsEnergyAcrossRepeatedHits`:
  stale revision/generation/feature and nonfinite evidence refuse; two connected
  source parts detach while unknown support remains fixed. All source IDs stay
  visible and physical. Debris moves under GPU simulation, settles, receives a
  second impact at its current certified pose, and rebuild returns the original
  layout. Pass, 9.5 seconds.

## Explicit approximation

A certified hit selects its source part through the accepted body's authored
face index. Nearby candidates must be original validated stud neighbors, within
2.5 studs in the current pose, with at most eight directly selected parts. Only
their incident bonds and own external anchors are cut. Newly disconnected roots
with previously validated support can also fall; total released parts are capped
at 32. Unknown support and remote anchored roots remain fixed. Every source mesh
instance is retained; no hidden/deleted bricks stand in for destruction.

The projectile already contacted a fixed wall. It is consumed in the replacement
transaction. This interim transfer is deliberately dissipative:

- energy <= min(36 game joules, 20% initial projectile energy,
  20% measured closing kinetic energy);
- total transferred linear impulse <= 20% measured normal impulse;
- initial debris COM speed <= 12 game units/second;
- initial angular speed <= 10 radians/second.

This does not claim fully coupled conservative fracture. It does not replay the
full solver impulse, create arbitrary spin, or force bodies to sleep. Subsequent
gravity, collision, friction, motion and sleeping are GPU simulated. Admission,
settlement and CPU walking geometry retain the existing certified boundaries.

## Long support refinement

Neighborhood distance uses the closest point on the oriented catalog body
(minimum `[-studsX/2,-height,-studsZ/2]`, maximum `[studsX/2,0,studsZ/2]`),
transformed through the current accepted root and source-part rotation. This
includes the nearby end of a long plate without treating its distant center as
the whole piece. Connectivity, eligibility, radius and count limits are unchanged.

`ImportedWallGpu.ImpactIncludesNearbyEndOfRotatedLongSupportingPlate` verifies a
rotated eight-stud plate supporting a struck brick: plate center is over 3.5
studs away, but the supporting end is only .6 away. Both eligible sources release;
the unknown-support piece remains excluded. This and the existing repeated-hit
owner regression passed together, 2/2 cases in 21.5 seconds. Evidence:
`impact-long-support.log/xml`.


## Localized off-centre impulse

Only the directly struck root receives the certified contact impulse and torque.
Project the contact point onto its actual material; compute `(point - COM) cross
impulse` using packed principal inertia. Other released roots begin at rest and
receive forces from GPU contact and gravity. No source pose is moved artificially.

A scale caps combined linear/angular energy, measured normal impulse, COM speed
and angular speed. Accounting is recomputed after the rounded admission frame.
This is still a delayed, dissipative approximation, not a coupled fracture solve.

The earlier response broadcast torque into all released neighbors, jamming the
full-house wall through contact friction. Localizing the direct impulse changed
real source displacement from 0.052 to 2.640758 studs while reducing initial
energy from 34.96 to 18.89 J. `actual-world-impact.log/xml` verifies the real shot,
settlement, source motion, rebuilt collision and saving. `localized-owner.log/xml`
passes four focused GPU cases, including no invented neighbor torque on a centered
hit, off-centre inertia/energy, repeated settled hits and long support selection.

Released parts use shell-only collision with original full-proxy mass/inertia;
all source render geometry remains authentic. See `released-shell-assembly.*`.
