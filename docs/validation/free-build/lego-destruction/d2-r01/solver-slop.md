# Resting contact tolerance

The dynamic solver accepted a `linearSlop` of .005, but used every negative
separation for positional correction. At rest, an overlap of .0025 therefore
received a separating kick despite lying inside the configured tolerance.

The new direct regression failed before the correction in all three paths:
small-island, global colored, and compact serial. Both solver implementations
now use `min(separation + linearSlop, 0)` for penetration bias. Positive-gap
speculation is unchanged, and overlap beyond the tolerance still separates.
Gravity, damping, restitution, sleep thresholds and solver substeps are unchanged.

The full `//tests:gpu_dynamic_solver` target passes after the fix. Raw before/
after logs and XML are retained here. This is a solver correctness check; it
does not by itself certify that the imported house settles.
