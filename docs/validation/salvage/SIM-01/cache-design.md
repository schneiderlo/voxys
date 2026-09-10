# SIM-01 final compiled assembly and cache identity

Design recorded 2026-09-08 and implemented in stage-e1. See [executed evidence](stage-e1/README.md) and the exact [version-1 grammar](cache-wire-v1.md). Independent review remains pending.
The component returns a single immutable `CompiledAssembly` owning the
functional plan and therefore the matching mass, collision and buoyancy plans.
No backend activation, cargo capture, live controls or authority ownership is
implied by the word compiled. SIM-02 and later mechanics still adapt this data.

## Cache input contract

Use an explicitly versioned, domain-separated SHA-256 stream. Encode individual
fields in a documented little-endian grammar; never hash C++ object bytes,
padding, pointers, `size_t` representations, unordered iteration or derived
floating-point aggregate outputs. Normalize floating-point −0 to +0 before
encoding finite binary64 inputs. The compiler version must advance when the
algorithm or interpretation changes; profile fields participate in the digest.

The key must bind actual physical metadata, not only a ContentKey. Sort used
part definitions by exact ContentKey, deduplicate them, and include only the
used definitions. Include definition key, allowed rotations, footprint, every
solid-occupancy and collision proxy with IDs/frames/extents, dry mass/local COM
and full tensor, buoyancy IDs/frames/extents/kinds, all sockets with stable IDs,
family/role/profile/frame/clearance/capacity/strength, part strength and every
closed module-variant parameter. Module tags must be explicit versioned values,
not accidental `std::variant` indices. Include all compile budget/profile
fields in fixed-width form, including geometry work and scratch bounds.

Bind build ID/revision, parts sorted by durable ID (definition, placement,
health and module settings), and connections sorted by durable ID with canonical
endpoints and all kind/enabled/damage/strength/rope configuration. Root identity
remains scoped to that build/revision. Global placement offsets are retained in
the key because owned build-from-root transforms differ even if local shapes
match. A future reusable local-shape cache needs a separate explicit key.

Owner, edit lease, paint, paid/loan entitlement, pricing/salvage value, localization
and visual material/mesh references are not functional compiler output. Exclude
them from this physical cache key and document that matching physical data can
never substitute for GameSession validation, financial admission, current visual
asset resolution or permission checks. Even a cache hit must not activate stale
canonical authority. Existing compile entry points validate the full supplied
build against the supplied current immutable catalog before deriving anything.

## Ownership and verification

A successful final result owns the complete plans, exact profile and input
identity; no borrowed catalog/input lifetime survives. Compile/failure does not
mutate existing builds, inventory or a live scene. Build temporary state and
publish only after complete success. Hashing should stream without an extra
unbounded serialization allocation. No mutable hash-only construction overload.

Require an independent explicit-byte fixture with a separately calculated SHA
and native/WASM agreement, insertion/endpoint/catalog order equivalence, −0/+0
normalization, unused/cosmetic exclusion, and changed identities for every
physical metadata/settings/connection/profile field family. Include same key
with changed current physical metadata; this must never reuse the old identity.
Retain all prior analytical tests, whole-build bounds, native/CMake/strict/WASM
coverage and actual allocation-failure/read guards. Final independent compiler
review remains necessary before SIM-01 task acceptance.
