# SIM-01 collision stage — exact orthogonal union

Claimed by root 2026-09-08. This extends the validated mass foundation and uses
only catalog collision boxes, independent of visuals/decorative studs. The 24
proper grid rotations keep transformed boxes axis aligned in root coordinates.

Each authored collision proxy receives a canonical local source label backed by
durable part ID, definition-scoped proxy ID and root. Source labels are specific
to this immutable plan and are not durable contacts or backend handles.

Build a disjoint exact box union per welded root by subtracting retained cells
from each source box in ascending source order. A subtraction emits at most six
disjoint slabs. In overlapped proxy regions, the least source label owns the
resulting volume. Keep this deterministic ownership for contact/load lookup;
collision union volume never substitutes for dry mass.

For each retained cell face, subtract rectangular coverage from adjacent cells
on its outward side. The surviving rectangles are the exact exterior boundary;
internal mating/cut faces do not survive. Partial-face coverage must split a
face, not discard an entire side. Patches retain source/cell/axis/normal/plane
identity. Coplanar patches may meet at edges; a later solver must deduplicate
edge contacts and may not activate unmasked cell boxes as if internal faces
were physical surfaces. Actual solver support and contact generation are SIM-02.

Build a balanced flat preorder BVH over disjoint cells. Split the longest bound
axis at the median with canonical cell-index tie breaks. Store escape indices
for stackless traversal. Read-only closed-AABB candidate queries first count,
then copy; insufficient output leaves the caller's buffer unchanged and reports
the required count. Candidate cells are broad-phase results, not accepted
contacts. The exterior patch list defines permitted contact surfaces.

Bounds per full compiled build: 2,048 input collision boxes, 4,096 disjoint cells,
24,576 exterior face patches, 8,388,608 clipping tests and the mass profile's
±256 m root coordinates. Scratch fragmentation is independently capped at
4,096 pieces. These are generated-element caps across roots. Each vector's
requested capacity is separately bounded by its local cap. Geometric growth
can retain fewer than twice the aggregate live cell/face elements across roots;
count limits are not an exact byte/RSS guarantee. Node storage requests exactly
2*N-1 elements per root; scratch vectors each request at most 4,096 elements.
Failure returns a typed error and retains the old plan and canonical input. No silent
truncation, coarse fallback, world activation or gameplay mutation. BVH nodes
are at most twice the cell count minus the root count. Work limits apply across
all roots; the global world still needs separate resource reservations.

Exact tick-space volume and face area provide numerical checks. Independent
small integer-cell oracles must verify union occupancy, no double counting,
every exposed face, interior-face removal, insertion-order equivalence, cavities,
partial overlap, containment, duplicate bounds and all rotations. Exercise BVH
queries against linear cell checks, capacity refusal and allocation failure on
native and configured WASM. This is CPU geometry evidence, not a solver or
visual acceptance gate.
