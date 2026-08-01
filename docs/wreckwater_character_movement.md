# WRECKWATER 2v2 character movement core

Status: server-integrated bounded authority foundation

Date: 2026-07-30

This is a CPU rules core for the four-player proof.

The headless server now owns it through
`WreckwaterCharacterAuthorityBridge`. Exact certified character state is
published in schema-5 snapshots and retained in replay.

It is not a client predictor, renderer, animation controller, or GPU character
collision system.

## Proven contract

`WreckwaterCharacterMovementAuthority` owns fixed storage for:

- four characters;
- two moving skiff identities;
- one pending input per character;
- four transition records per tick.

Construction and every tick use `std::array`. The accepted tick path does not
allocate.

Only the next exact tick may close. The rate is fixed at 60 Hz.

An invalid platform frame is checked before live character state changes.
The caller may fix the frame and retry the same tick.

## Live authority integration

After the four roster peers connect, the native authority creates exactly four
characters and binds each stable handle to one authenticated player and
connection generation.

The runtime accepts fixed 144-byte movement requests on the realtime input
lane. One request carries a current sample plus up to three exact prior
samples. It derives the player from the peer, checks the common character
handle and connection generation, and buffers each canonical sample through a
staged bridge copy. Character simulation advances only when the matching
certified GPU pose tick supplies both skiff platform samples. Any character
initialization, lifecycle, or certification failure permanently fail-stops the
authority.

Every published first-slice snapshot contains four 96-byte character states.
The replay recorder stores the same canonical schema-5 snapshot bytes, so
character position, mode, platform link, connection generation, and applied
movement-input acknowledgement are certified replay state.

The client codec, fixed resend window, safe local-character send API, process
probe, local prediction/reconciliation controller, fixed-step
[graphical client loop](wreckwater_graphical_client_loop.md), and remote
presentation sampler understand these records. Visible renderer integration
remains separate work.

## Identity and input

Each character uses the engine's 16-bit generational `CharacterHandle`.

Each input also carries:

- player ID;
- connection generation;
- target tick;
- a monotonically increasing movement-input sequence.

Disconnect clears the pending input immediately. A disconnected character
receives neutral controls while gravity, water, and existing momentum continue
to simulate.

Reconnect requires the exact prior generation and the next generation. Old
connection input cannot control the reconnected character.

For one character and tick, the highest accepted sequence wins. This makes
packet arrival order irrelevant inside the movement core.

Input sequences cannot jump by more than 1,024 by default, and
`UINT64_MAX` is reserved. A hostile packet therefore cannot permanently pin a
connection at the end of the sequence space.

## Moving skiff frame

While aboard, a character stores:

- skiff-local feet position;
- skiff-local planar velocity;
- exact skiff ID, skiff generation, and body generation.

Each tick recomposes world velocity:

```text
v_character =
    v_skiff_linear
  + omega_skiff_world x rotated_local_position
  + rotated_local_walk_velocity
```

The skiff velocity is never integrated into character velocity while attached.
It therefore cannot grow once per tick by accident.

Jumping, walking off an edge, a capsized deck, a submerged deck, or a missing
platform transfers that point velocity once.

Platform quaternion sign is canonicalized. Current platform samples are
ordered by stable generational identity before use.

For the same exact platform identity, consecutive samples must also prove:

- immutable deck extents and local deck height;
- translation consistent with endpoint velocity and bounded acceleration;
- shortest-arc rotation consistent with endpoint angular velocity and bounded
  angular acceleration;
- no missing-tick disappearance and same-generation return.

Skiff generation must advance by exactly one and use a different physical body
handle. A new generation may relocate, but it is not landing evidence until a
second continuous sample exists.

## Transitions

The core has three modes:

- `OnSkiff`;
- `Airborne`;
- `Swimming`.

Airborne feet use 16 fixed rigid-transform intervals and 12 fixed bisection
steps between the prior and current skiff frame. Position is interpolated in
wide world space and orientation follows the shortest quaternion arc. The
earliest descending plane crossing wins; exact ties use stable generational
identity. The impact footprint must fit on the authored deck rectangle.
Starting below a deck and moving farther down cannot snap upward onto it.

Swimming uses:

- gravity;
- submerged-volume fraction;
- configurable buoyancy ratio;
- implicit linear water drag;
- bounded horizontal swim acceleration;
- an optional upward stroke.

Water entry and exit use different heights. This prevents a character at the
waterline from alternating modes every tick.

A swimming player may request boarding assistance. It works only from outside
a deck edge, with input directed toward that edge, within configured
horizontal reach, vertical climb/drop limits, and relative-speed limit.

This rectangle-edge rule is not hull-volume collision. The runtime still needs
an authored ladder/rail/hull obstruction query before shipping.

## Hash

The 64-bit state hash covers:

- configuration;
- closed tick;
- every character slot and generation;
- connection and input sequence state;
- pending inputs;
- current and remembered platform identity/pose.

The hash is an FNV-1a convergence and corruption fingerprint.

It is not a cryptographic authenticator.

The core uses authoritative floating point. Exact convergence is tested for
the same build and arithmetic environment. This is not a cross-platform
lockstep claim.

Configuration and platform negative zero are canonicalized before hashing.
The hash covers persistent authority state, not the transient transition span
returned by the most recently closed tick.

## Deliberate limits

This milestone does not provide:

- terrain, hull-volume, ladder, rail, or other-character collision;
- wave-sampled water height and current (this core currently uses one plane);
- GPU capsule queries or parity with GPU rigid bodies;
- local client prediction, rewind, reconciliation, or tick synchronization;
- rendered local-player control integration;
- animation, IK, hands, camera, audio, or VFX;
- damage-driven deck geometry;
- replay checkpoint fields beyond the certified schema-5 character record.

The existing CPU capsule mover remains useful for terrain collision. A later
runtime adapter should combine its terrain sweep with this core's moving local
frame instead of synchronously reading the whole GPU world.

The authority integration suite additionally proves exact certified-tick input
closure, four-character snapshot publication, lifecycle fencing, and fail-stop
behavior in `test_wreckwater_authority_runtime.cpp`.

## Standalone evidence

`test_wreckwater_character_movement.cpp` covers:

- fixed 60 Hz and stable storage addresses;
- translating-platform inheritance;
- high-speed yaw and angular point velocity;
- no per-tick platform energy growth;
- walking off an edge;
- swept moving-deck landing;
- water entry/exit hysteresis;
- buoyancy and drag;
- bounded water-to-deck boarding;
- disconnect and reconnect safety;
- stale character, connection, and input sequence rejection;
- character/platform capacity and atomic platform-frame rejection;
- exact long-run convergence with reversed platform and input arrival order.
- cross-sector teleport, rotation, geometry-mutation, stale-return, skipped
  generation, and body-reuse rejection;
- rotating-deck rigid sweep, earliest impact, exact tie, and underside
  rejection;
- sequence exhaustion/jump and extreme finite-configuration rejection;
- high-relative-speed and through-centre boarding rejection.
