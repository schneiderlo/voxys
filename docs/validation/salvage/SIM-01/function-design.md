# SIM-01 functional preparation

Implemented by root on 2026-09-08; validation is recorded separately in
`stage-d1/README.md`. This component prepares module/socket/connection data from
one validated build/catalog invocation and owns the matching buoyancy, collision
and mass plans. It creates no solver handles, live joints, engines or inventory.

## Immutable contract

`AssemblyFunctionPlan::modules()[i]` corresponds to mass-plan part `i`, sorted by
durable part ID. Each module copies its closed typed parameter variant,
configuration, health and authored strength. Disabled modules and zero health
remain identifiable; they do not silently delete mass or rewrite configuration.
Paid/loan provenance remains canonical authority data; the functional compiler
cannot grant or convert ownership. Part definition identity and exact
root-from-part transform remain available through the owned mass plan.

Every module has a part-origin frame. Engine adds its shaft socket. Propeller
adds both its shaft socket and an independent thrust frame; force direction is
that frame's local **−Z**, not socket +Y. Helm adds the authored operator frame.
Winch, tow eye and cargo cradle add their referenced line/eye/latch sockets.
Flotation/structure/brace/ballast use the part frame. Profile-1 repair reach is
centered on the part origin; no invented cutting/crane/tool attachment exists.
Adding a module variant requires explicitly updating frame count and dispatch.

All sockets, including unused ones, are copied in `(part ID, socket ID)` order.
Each retains its exact authored frame, clearance, family/role/profile, capacity
and strength plus its root-local frame. Socket +Y remains outward and +X remains
the key direction. Frame composition applies the authored local frame once,
then part placement relative to the root. Root axes follow build axes and the
least-ID member anchors translation; no implicit COM-frame conversion occurs.

Connections sort by durable ID and normalize endpoint order exactly as the
canonical BuildModel does. Each owns its full definition and indices of both
compiled socket records. Damage, enabled state, strength and all rope lengths
are preserved. Disabled connections still reserve endpoint capacity. A winch's
configuration default does not replace an attached rope's authoritative rest
length. Indices are plan-local; they are not durable IDs or generational handles.

Enabled welds have already joined one mass root and must not create redundant
solver constraints. Rope/latch records can reference distinct roots or the same
root. They remain authored intent; later SIM/MECH code must decide legal live
activation and self-constraint behavior. A distant latch link does not prove
capture, a rest pose, merged cargo or additional mass. Connection frames are
root-local; later body/COM adaptation is explicit SIM-02 work.

## Capacity and ownership

Whole-call maxima are 256 modules, 8,192 sockets, 1,024 connections and 768
functional frames. All roots share each budget. Counts preflight before their
owned arrays reserve. Each module contributes 1–3 frames. Count-zero profile
fields are valid budgets but reject any nonempty required output. Profiles
exceeding hard bounds return InvalidProfile; exhaustion returns Capacity.

The upstream canonical candidate-pair budget, root extent and geometry work
limits still apply. In particular, a 1,024-weld tall fixture exceeds mating
validation work despite fitting the connection record limit. Keep that explicit
rejection. A 256-part fixture with 255 welds and 769 ropes verifies maximum
connection/socket records without exceeding the independent work bound.

The compiler reuses successful geometry validation, sorts at most 256 borrowed
input pointers on the stack and stores no borrowed input/catalog pointer. All
published arrays and typed parameters are owned; successful output appears only
after the entire operation completes. Nested errors retain their stage cause.
Actual allocation failures must preserve canonical input and an existing plan.
Read-only spans, binary identity lookup and bounded frame slices allocate
nothing; spans require their owner to remain alive and unmoved.

The subsequent [stage-e1 component](stage-e1/README.md) now implements final
immutable `CompiledAssembly` and physical input identity. Independent complete
compiler review and finding resolution remain before SIM-01 acceptance.
This component does not certify live physics or improve unapproved cove art.
