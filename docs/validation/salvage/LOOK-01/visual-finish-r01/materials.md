# Cove materials and lighting

The playable Cove opts into a shared response for authored parts, the robot,
environment and brick terrain. Other rendering routes retain the legacy
response. The limited palette is cream, teal, coral, slate, steel and rubber.
Existing part geometry, paint, construction sockets, prices and saved IDs are
preserved. The new scenery is original Blender geometry with solid materials;
no generated texture or third-party art is required.

## Implementation

`MeshInstance::surface` is a separate four-float field: wet coverage, normalized
saved damage, explicit Cove enable, and current immersion. All-zero selects
the original shader. Admission rejects invalid ranges and nonzero disabled
payloads. The GPU stride is 128 bytes, with existing live body and paint fields
unchanged. The fixed fixture allowance is 144 KiB; actual fixed storage is
138,616 bytes, including the round rope. Overall owner/aggregate caps remain
16/48 MiB.

Wet plastic combines a water film with the substrate, attenuating both
incoming and outgoing substrate light. Conductive reflection retains its
authored color. Immersion removes the extra air/water interface below the
actual ocean surface; otherwise two coats produced a visibly incorrect
submerged highlight. Saved damage raises roughness rather than inventing
damage events or changing paint. Normal variance broadens small highlights.
Direct sun and filtered environment light use the same response.

The application obtains cosmetic part wet coverage from transformed bounds
and a bounded wave-height approximation, with eight seconds of drying.
Swimming selects full robot immersion. This is cosmetic state, not a new
water-force or flooding model. Held presentation packets retain their surface
values together with their accepted body poses.

The terrain borrows the same generation's diffuse/specular environment and
BRDF integration. The legacy sky binding is omitted only in the Cove terrain
layout, keeping its total within sixteen sampled textures. Lighting bake,
shadow/color draws and dependencies follow existing submission tickets.

Cove keeps ACES exposure and sRGB output. Grain and vignette are removed.
Red light is absorbed more strongly than blue/green in the water, making depth
readable as turquoise. Physical wave settings and forces are unchanged. The
fixed weather is a clear coastal afternoon, with warm sun and cool sky fill.

Environment LOD follows its projected bounds at 320/110 vertical pixels.
Kit projection now uses accepted moving roots or the raised workshop pose,
rather than the original source placement. Round eight-sided rope uses actual
accepted endpoints and the existing 28 mm diameter; it adds no simulated sag.

## Focused evidence

- Five material GPU cases and one rope case pass. They cover legacy parity,
  invalid inputs, film/conductor/immersion response, filtered white-furnace
  energy, normal footprint, live-body parity, depth and resource retirement.
- Both lighting-composition profiles pass. The Cove test reads the existing
  pre-water HDR target: shadowing removes direct bed light down to the exact
  sun-off environment value. Final water color changes consistently while
  retaining nearer water depth. Legacy thresholds remain unchanged.
- A separate actual GPU owner case passes with a 64-brick assembly, scenery,
  robot and all five cables. It verifies residency, capacity and lifetime.

The portable XML and summary records are in `checks/`. The full-scene owner
reservation is 11,411,160 bytes, including 1,241,888 scenery bytes and a
33,328-byte external effects dependency. This is requested allocation
accounting, not a measured graphics-driver memory total or frame-time claim.

## Retained findings and limits

The first immersion attempt double-counted the underwater film. The corrected
Cove bed oracle reads actual HDR energy; an arbitrary twenty-code-value
threshold had depended on that erroneous film. Legacy still retains its
original threshold. The standalone legacy test also lacked its shipped sky
runfile and silently used a procedural fallback; the exact PNG is now a
declared test input and missing input fails explicitly.

This work does not implement temporal reconstruction, glass/transmission,
arbitrary broken interiors or general flooding. Separated modular parts retain
their existing closed authored surfaces. Those remaining requirements are
explicit in VIS-02, VIS-03, ASSET-06 and MECH-07; numeric material tests alone
do not approve finished art.
