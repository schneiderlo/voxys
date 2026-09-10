# CompiledAssembly physical input identity, version 1

This describes an internal reconstructible physical-input cache key, not a
save file, command, authentication token or proof of completed simulation.
`CompiledAssembly::compile` first validates the full build/current catalog and
compiles all CPU plans. It then streams the following bytes through SHA-256.
There is no public hash-only admission or cached-data activation API.

## Encoding

All integers are little-endian. `u8/u32/u64` are exactly 1/4/8 bytes. Booleans are
one `u8`, 0 or 1. Signed lattice coordinates use their two's-complement `i32`
representation. Counts and profile element limits are `u32` after bounded
validation, irrespective of native/WASM `size_t`. Work counters use `u64`.
All floating-point input fields are finite IEEE binary64. Encode their bits as
`u64`, replacing either signed zero with positive zero. No other rounding or
normalization is performed. No output mass sums or C++ object representations
participate. The hash has no implicit delimiters beyond this grammar.

Reusable records, each in this order:

- `ID`: 16 world namespace bytes, then counter `u64`.
- `KEY`: ID, content version `u32`.
- `POINT`: x/y/z `i32`, in lattice ticks.
- `FRAME`: POINT translation, frozen cube-rotation ID `u8`.
- `BOUNDS`: minimum POINT, maximum POINT.
- `BOX`: collection-scoped proxy ID `u64`, FRAME, half-extents POINT.
- `STRENGTH`: four binary64 fields: tension N, shear N, bending Nm, torsion Nm.
- `ENDPOINT`: part ID, socket ID `u64`.
- `SETTINGS`: kind `u8`, enabled bool, channel `u8`, limitPermille **u32**,
  reversed bool, defaultLineLengthMillimetres `u32`.

The widened health/damage/limit fields are deliberate digest grammar, not the
separate canonical BuildModel/save codec. Do not reuse this grammar as a decoder
for those files.

## Complete stream order

1. ASCII `Voxys.CompiledAssembly.inputs.v1`, **without NUL**.
2. Compiler version `u32` (1); ticks per metre `u32` (50).
3. Profile, in order:
   - Mass: parts `u32`, roots `u32`, radiusTicks `u32`.
   - Collision: inputBoxes, cells, faces, scratchPieces (each `u32`),
     clipTests `u64`, radiusTicks `u32`.
   - Buoyancy: inputBoxes, cells, references, scratchPieces (each `u32`),
     clipTests `u64`, referenceWrites `u64`, radiusTicks `u32`.
   - Functions: modules, sockets, connections, frames (each `u32`).
4. Build ID; topology revision `u64`.
5. Part count `u32`; parts sorted by durable ID. Each: part ID, definition KEY,
   authored build-from-part FRAME, health **u32**, SETTINGS.
6. Connection count `u32`; connections sorted by durable ID, each endpoint pair
   normalized so A precedes B. Each: connection ID, A ENDPOINT, B ENDPOINT,
   kind `u8`, enabled bool, damage **u32**, STRENGTH, minimum/maximum/rest rope
   length in millimetres (three `u32`, in that order).
7. Count `u32` of distinct **used** definitions, then definitions sorted by KEY.
   Each definition contains the following in order:
   - KEY; permitted rotation mask `u32`; footprint BOUNDS.
   - Solid occupancy count `u32`, then BOX records sorted by proxy ID.
   - Collision count `u32`, then BOX records sorted by proxy ID.
   - Dry mass kg, local COM x/y/z metres, then all nine row-major inertia
     elements kg m²: thirteen binary64 values.
   - Buoyancy count `u32`, then regions sorted by collection proxy ID. Each:
     BOX, buoyancy kind `u8`.
   - Socket count `u32`, then sockets sorted by socket ID. Each: ID `u64`,
     family `u8`, role `u8`, profile `u32`, authored FRAME, capacity `u8`,
     local clearance BOUNDS, STRENGTH.
   - Part STRENGTH, then module payload below.

Frozen enum values in this version: settings Passive/Power/Steering/Winch =
0/1/2/3; connection Weld/Rope/Latch = 0/1/2; buoyancy SolidMaterial/
SealedCompartment = 0/1; socket Structural/DriveShaft/TowLine/CargoLatch =
0/1/2/3; role Neutral/Plug/Receptacle = 0/1/2. These reuse explicitly ordered
schema-1 enums. Changing their values or interpretation requires a new compiler
version/grammar; module payload tags below are independent of variant indices.

## Module payloads

Start with the listed tag `u8`. All scalar physical values below are binary64
unless another type is named. Parameter order is exact.

| Tag | Type | Following fields |
|---|---|---|
| 1 | Structure | none |
| 2 | Flotation | drag coefficients x, y, z |
| 3 | Engine | shaft ID `u64`, maximum power W, maximum torque Nm |
| 4 | Propeller | shaft ID `u64`, force FRAME, maximum thrust N, required power W |
| 5 | Helm | operator FRAME, maximum steering radians |
| 6 | Winch | line ID `u64`, minimum length m, maximum length m, reel speed m/s, maximum force N |
| 7 | TowEye | eye ID `u64` |
| 8 | CargoCradle | latch ID `u64`, maximum cargo kg, capture distance m, angle radians, linear speed m/s, angular speed radians/s |
| 9 | Brace | load transfer factor |
| 10 | Ballast | none |
| 11 | Repair | reach m, health fraction/s, material units per full health `u32` |

## Scope and compatibility

The object owns matching mass, exterior contact geometry/BVH, full buoyancy
contributor coverage and module/socket/connection frames, along with profile
and digest. No borrowed input/catalog pointer survives. The digest binds actual
used physical metadata even when its KEY has not changed. Unused definitions
are excluded; equivalent insertion/endpoint/metadata collection order gives the
same stream. Build/revision, durable part/link IDs and global placement remain
included because mappings and build-from-root poses depend on them.

Owner, lease, paint, entitlement, cost/yield, localization, visual asset paths,
LOD and surface color/roughness/metallic are excluded. They are not physical
compiler output. Permission, economy and visual admission still require their
own current canonical checks; a matching physical digest grants no authority.
If future physical friction/material parameters are introduced, include them
with a bumped compiler version. Compiler changes also invalidate older identities;
never retain version 1 after changing output algorithms or interpretation.

The streaming hash and fixed (at most 256) pointer-sorting workspaces allocate
nothing. Preparation allocations remain bounded by the constituent plans; this
does not claim a scene memory budget, hard real-time compile deadline or
cross-platform floating-point solver determinism.

The independent 670-byte fixture in `stage-e1/golden-input.py` specifies values
without importing/parsing production code. Its SHA-256 is
`8c9012e1400f5e21fe00fdb6ba3e9b2aed1e5155074800e770de2d0a773452e0`.
Runtime test reports, rather than this specification, establish which builds
actually match it.
