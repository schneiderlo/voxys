# SIM-01 — analytical assembly compiler

Claimed 2026-09-08. DATA-03 canonical builds and ASSET-02 validated physical
metadata are complete. This compiler is CPU-only derived data preparation; it
does not own inventory, allocate durable IDs, mutate GameSession, activate
physics, or imply a completed simulation tick.

## Staged implementation

1. **Mass foundation (implemented; [stage-a1 evidence](stage-a1/README.md)):** validate the complete build against the
   supplied immutable catalog; find enabled welded components; produce stable
   root/part mappings, explicit build/root/part frames, dry mass, center of mass
   and full inertia. This intermediate result is named `AssemblyMassPlan` and
   must not be mistaken for a complete `CompiledAssembly` or backend shape.
2. **Physical geometry (implemented; [stage-b1 evidence](stage-b1/README.md)):** compile exterior box union/contact surfaces, bounded
   child acceleration and stable contact-to-part/proxy identity. Remove internal
   mating surfaces; decorative render studs never become contacts. Bound both
   input children and generated fragments/pairs. Reject excessive complexity
   without changing a live build.
3. **Flotation (implemented; [stage-c1 evidence](stage-c1/README.md)) and functional frames (implemented; [stage-d1 evidence](stage-d1/README.md)):** transform authored buoyancy regions,
   prevent double-counted overlap, preserve sealed/solid provenance, and map
   module points/directions and non-weld endpoint frames to their roots. A
   authored latch link does not certify a live captured/merged cargo body.
   Do not blindly reuse collision's least-source ownership for overlapping
   buoyancy. A solid core must retain displacement when an overlapping sealed
   compartment floods or is disabled. Preserve the covering region identities
   in a bounded disjoint arrangement (or explicitly reject unsupported overlap);
   later SIM/MECH state decides which contributors displace water. The all-active
   union volume alone is insufficient provenance for flood-aware simulation.
4. **Owned final output/cache identity (implemented; [stage-e1 evidence](stage-e1/README.md)):** immutable reconstructible complete
   assembly, compiler/profile version and a canonical digest of topology and
   all physical content inputs. A matching content key alone cannot substitute
   for binding actual admitted metadata. No raw C++ padding or pointer hash.
5. **Acceptance (shared validation implemented; independent review pending):** independent asymmetric numerical fixtures, all 24 rotations,
   insertion-order equivalence, topology/ballast changes, invalid/capacity and
   allocation-failure behavior, native/CMake/strict/WASM coverage and review.

## Root and mass contract

Only enabled `Weld` edges join a dry rigid component. Rope/latch links remain
separate until their actual mechanics authorize relative motion or capture.
Health, paint, module enablement and loan provenance do not silently remove dry
mass. A later accepted break/removal must change canonical topology explicitly.

Roots are ordered by the least durable part ID in their component. That part's
placement translation anchors the root; axes initially match the build axes.
The anchor part's rotation is retained in `rootFromPart`, not applied twice to
the root. The root key is a derived member identity scoped by build/revision,
not a new durable ID or a solver body handle. Parts are ordered by durable ID.

`buildFromRoot` is an exact grid transform. `rootFromPart` is exact grid data;
root-local COM and inertia are double precision. A later backend COM frame must
explicitly adapt these through SIM-02 rather than moving the authored root
silently. Subtract lattice anchors before conversion to floating point so a
large common build translation does not contaminate small local mass offsets.

Use deterministic compensated accumulation and two passes. First compute mass
and weighted root-local COM. Then rotate each full authored tensor by its exact
signed-permutation rotation and apply the parallel-axis theorem about the final
COM. Compute the six independent tensor entries and mirror them exactly.
Validate finite positive mass and physically valid aggregate inertia after
normalization; authored per-part magnitude limits do not apply to the summed
assembly. Reject numerically near-singular physical tensors explicitly.

Initial per-call limits: 256 parts, 64 roots, and all transformed footprint
coordinates within ±256 m of their component anchor. These are compile-profile
bounds, not a claim that 64 roots remain available in a loaded world. Scene
preparation must additionally reserve the global root/child/joint budgets.
Disconnected components are represented for later launch policy/fragment use;
this stage does not declare any disconnected design launchable.

Validation constructs a temporary canonical BuildModel from the provided
snapshot and catalog. This prevents using a previously validated model with
different physical definitions unnoticed. Complete owned output is returned
only after all work succeeds; allocation failure returns Capacity and preserves
the input. Empty builds and over-limit profiles return typed errors. No observer,
renderer, allocator, storage or application integration is part of this stage.

Current stage-c1 output retains the full contributor set for every disjoint buoyancy cell, with global cell/reference/work caps and no live flooding state. Module/socket/connection frames now pass stage-d1 validation; stage 3 preparation is implemented. Final immutable output/cache identity now passes stage-e1 validation. Independent full compiler review and any finding resolution remain before SIM-01 acceptance.
