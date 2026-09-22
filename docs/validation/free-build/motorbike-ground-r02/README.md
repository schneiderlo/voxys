# Motorcycle ground handling — 2026-09-16

Owner feedback: the motorcycle feels wrong on bumps, slopes and ground contact.
This follow-up changes the creative mode motorcycle controller, not the separate
motocross experience or the character mesh.

## Changes

- Each tire now uses its authored radius (0.784 studs). Front/rear axle heights
  drive the rigid frame's position and pitch. The projected wheelbase is solved
  against terrain as the bike tilts; a bracketed angle solve avoids oscillation
  at platform edges. Mounting requires support under both wheels.
- Small stud ripples retain tire contact. Unsupported wheels keep vertical
  momentum and fall under gravity. Contact velocity is filtered to reduce
  stud-scale launch impulses, while preserving motion over a crest.
- The broad upright collision guard remains separate from the visible frame.
  Its center probe no longer lifts both wheels onto every small middle bump.
  Underside contact supports the bike as it rolls over a ledge.
- Sweeps check both an up-then-across route and an across-then-down route when
  appropriate. This fixes the diagonal sweep catching on descending plate
  edges. Both segments must be clear; thin walls and low roofs still block.
- Grade affects acceleration and coasting. Space brakes and holds on slopes.
  Airborne wheels cannot accelerate or brake against empty space. Steering
  requires front-wheel contact; dismounting requires both tires grounded.
- The camera follows the visible frame height. Airborne wheels do not emit
  ground-contact shadows.

Controls are unchanged: **M** mount/dismount, **W/S** accelerate/brake/reverse,
**A/D** steer, **Space** brake. The existing transient-bike save behavior remains.

## Validation

`//tests:adventure_world` passes all 19 tests, including seven motorcycle tests:

- Acceleration, steering, braking, pause input, invalid input and safe dismount.
- Thin-wall collision, low ceilings and wet mounting refusal.
- Actual rendered axle positions versus shared terrain support while riding a
  studded slope; contact error bounded to 0.004 studs.
- Fixed-step equivalence at 30 and 120 render updates per second on the slope.
- Uphill/downhill speed difference, downhill coasting and brake holding.
- A three-stud platform drop: wheels release, the bike advances past the ledge,
  lands, and never drops more than 0.45 studs in one simulation tick.
- A small obstacle between the tires moves only the broad collision guard,
  not the visible frame; mounting beyond the map edge is refused.

`//tests:adventure_runtime` passes with the installed 8192 terrain and workspace
assets. This includes actual M key routing, mounted movement, braking,
dismounting, and restoring building state. The browser WASM build also passes.

Reproduce from the project environment:

```sh
bazel test //tests:adventure_world //tests:adventure_runtime \
  --test_env=VOXY_ADVENTURE_TEST_TERRAIN="$PWD/data/generated/td_seed_1234_8192.r16" \
  --test_env=VOXY_ADVENTURE_TEST_WORKSPACE="$PWD" --test_output=errors --jobs=4
cmake --build build-lego-wasm --target voxy_wasm -j4
```

Local preview: `?experience=build&preview=motorbike-ground-r02`.

## Limits

This is an assisted upright toy motorcycle with a rigid frame, bounded pitch
and spherical tire support queries. It does not simulate independent fork
suspension, tire deformation, crashes, or full rigid-body balance. Cross-slope
clearance remains conservative. Automated contact checks establish the physical
contract; the owner's riding-feel acceptance and publication remain open.
