# Wide synthetic wall: contact correction isolation

Both runs use the same 39 source parts, eligibility policy, 80×80 synthetic floor, normal four substeps and 1,200-tick bound. No force, pose or sleep setting changes.

| Case | Positional slop correction | Temporal normal preference | Awake / dynamic | Maximum linear speed | Maximum angular speed |
| --- | --- | --- | --- | --- | --- |
| Both fixes, prior run | On | On | 27 / 29 | 9.00602 | 1.473 |
| A | On | Off | 27 / 29 | 9.00602 | 1.473 |
| B | Off | On | 26 / 29 | 0.615676 | 0.211752 |

All cases fail complete settlement. Case A reproduces the both-fixes final positions. The temporal normal preference does not cause the increased final speed in this fixture.

The increased speeds coincide with parts still falling above the floor. In A, body 29 is at (15.9847,12.5867,2.48115), velocity (0.900985,-7.71947,-0.0588657), and body 40 is at (17.1141,12.227,2.5), velocity (1.48452,-8.88275,-0.0373404). In B, both are already asleep on the floor, at Y10.5 and Y10.6925. The slop correction changes support/collapse timing; final maximum speed alone does not establish explosive energy injection. Residual instability elsewhere remains unresolved.

Logs include all final positions, velocity vectors, angular velocities and orientations:
- `synthetic-wall-ab-a-slop-only.log`
- `synthetic-wall-ab-b-normal-only.log`

The narrow-phase and dynamic-solver shaders were restored byte-for-byte to their original both-fixes state after the experiment, with SHA256 verification. No A/B switch remains in production code.
