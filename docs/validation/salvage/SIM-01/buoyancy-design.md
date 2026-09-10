# SIM-01 buoyancy preparation — complete coverage provenance

Claimed by root 2026-09-08. This stage transforms authored buoyancy boxes into
root space and builds an exact disjoint arrangement. It prepares geometry and
provenance only. Water sampling, partial submersion, flood state, forces/torques
and completed physics ticks remain later SIM/MECH work.

Every cell records **all** source regions covering its full interior. Sources
retain durable part ID, definition-scoped region/proxy ID, root, solid/sealed
kind, exact root-from-region frame and local half extents. Source labels are
canonical plan-local indices, not durable IDs. Do not choose only the lowest
source as collision does: when a sealed region becomes inactive, an overlapping
solid region must remain available to displace water.

Process source boxes in ascending source-label order. Split each existing cell
at a new source intersection: residual slabs retain old contributors, and the
intersection appends the new contributor. Subtract old cells from the new box
to find previously uncovered volume. Maintain two bounded cell/reference pools
and two bounded box scratch lists. Final cells sort by bounds and flatten their
sorted contributor lists into an owned canonical array.

Caps per compiled build: 2,048 input regions, 4,096 cells, 32,768 contributor
references, 8,388,608 intersection/subtraction tests, 8,388,608 reference writes,
and 4,096 scratch pieces. All roots share remaining generated/work budgets.
Empty coverage is valid for parts/roots without authored displacement. Box
coordinates stay within the mass profile's ±256 m per axis. Vector growth is
bounded; temporary ping-pong pools and growth slack must be included in future
world preparation memory budgets, not confused with live element counts.

The all-active volume is the sum of disjoint cells once. The arrangement also
supports later per-region activation/geometry decisions without losing covered
source identity. Do not turn an inactive-region numerical fixture into a claim
of a shipping flooding model. Partial flooding may require clipping a source's
active volume within a cell at the certified water state; full-cell contributor
labels alone do not prescribe that future physical approximation.

`AssemblyBuoyancyPlan` owns the previously validated collision/mass plan plus
coverage per root. Compilation reuses that same build/catalog invocation, so
derived mass and geometry are not mixed across catalog revisions. Disabled
welds/rope/latch links do not combine displacement across independent bodies.
Later latching must recompile the accepted merged group exactly once.

Acceptance: an independent unit-cell oracle verifies exact contributor sets,
union volume, cavities, nesting, duplicates and intersections; enumerate active
subsets to prove surviving solid/other-seal provenance without double counting.
Exercise all rotations, insertion-order equivalence, absent buoyancy, active
loan/paid data, 256-part builds, every capacity/work limit, cross-root budgets,
owned lifetime, and actual allocation failure in native and configured WASM.
