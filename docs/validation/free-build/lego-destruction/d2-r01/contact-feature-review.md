# Dominant authored contact face regression

A direct GPU contact regression reproduced a face-identity error independently of full-world simulation. A cube rotated ±0.001 radians about Z rests against a wide level primitive floor. Its bottom-edge points also belong to side faces. The old `authored_contact_feature` returned the first containing face with any positive normal alignment, so tiny rotations changed bottom-support IDs to X-side IDs. Zero tilt chose Y correctly.

The regression `GpuAuthoredShapes.ContactTiltedBottomEdgesKeepDominantExteriorFaceIdentity` fails before the patch and passes afterward. It checks both tilt signs and zero tilt, real manifold normal, root-local bottom position, and every resolved face axis/sign. See `contact-feature-before.log` and `contact-feature-after.log`.

The shader now chooses the containing exterior face with greatest signed normal alignment. Exact ties retain the first/lower face index. Contact geometry, clipping, reduction, solver impulses and sleep thresholds are unchanged. This prevents the reproduced feature churn; actual-house settling still requires separate runtime verification and is not established by this test.
