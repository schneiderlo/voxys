# First playable cove towing

2026-09-09. Scoped SIM-08/PLAY-03 progress under owner decision D20.
The cove now supports an actual second body connected to the boat's winch.
This is local scene gameplay. Campaign ownership, latching, jobs, saves,
rewards and the complete SIM-08 acceptance gate remain unfinished.
No screenshots were captured or reviewed. No gate passed or commit was made.

## Play

Open `?experience=salvage-cove`, or native `--config salvage_cove.cfg`.
Walk to the skiff, press E to board, then take the helm. F hooks the nearby
salvage generator when its tow eye is within 8 metres of the winch. F again
releases it. Hold Q to reel in; hold Z to pay out. Releasing either key stops
the winch. Browser buttons also provide hook/release, reel, pay out and stop.
W/S throttle and A/D steering still operate the boat. R releases the cable
and restores both boat and cargo. Leave drains all three bodies and their
shape ownership before navigation. Winching stops if the player leaves the boat.

The 420 kg generator starts on the deepened berth's seabed. Its real authored
solid displacement is insufficient to float; it is not given invented sealed
flotation. Winching lifts/drags it, and sailing tows it. The existing fixed
generator on the dock remains a separate scenery object.

## Implementation and limits

- Registry schema 5 adds one explicit cargo placement. The cargo is compiled
  separately through the same mass/collision/flotation/function compiler as the
  boat. It is excluded from the fixed scene union and temporary player collision.
  The skiff stays 1,035 kg; the fixed union still has thirteen proxies.
- The authored water driver supports at most sixteen independent bodies, each
  with its own bounded displacement-cell buffer, controls and GPU dispatch.
  Boat and cargo keep separate mass and water forces. There is no per-frame CPU
  force feedback. Still-water drag and render-produced waves remain the earlier
  approximation; shared fixed-tick water epochs are unfinished.
- The winch's authored line frame and generator's tow eye are converted from
  root space to each body's actual COM/principal frame. The existing tension-only
  distance solver supplies reel/pay-out, force limits and overload breaking.
  The winch has a 12,000 N force limit, 0.5–40 m length limits and 2 m/s reel speed.
  Break force comes from the weaker authored endpoint. No rope/cargo mass is
  added to the boat. F automatically chooses this scene's single salvage load;
  general aimed selection, visibility/reach checks and multiple cargo are pending.
- A depth-tested 28 mm cable uses the existing fenced mesh owner and confirmed
  endpoint observations. It is a straight presentation segment; slack sag and
  rope/terrain collision are unfinished. Dynamic cargo meshes use GPU body poses.
  One additional 1,936-byte helper mesh fits the existing 128 KiB fixed reserve.
- UI actions are queued as intent and applied at the next frame boundary to a
  future physics tick. UI reports when that change's tick has been confirmed.
  This does not publish a campaign transaction or grant durable identity/rewards.
- Per-tick event readback and requested pose-copy capacity are checked before
  encoding owned physics. When unread events occupy all slots, preparation
  returns Busy without advancing encoded time. Pose/event packets carry their
  actual submission serial and wait for its validated completion plus the
  confirmed tick. Owned event overflow, invalid tick order or failed maps stop
  the world rather than publishing partial evidence. Public results include the
  owned incarnation. The cove drains this stream and observes actual rope breaks.
  The 512-event packet capacity is specific to this small scene; overflow fails.
- SIM-04 still needs per-operation GameSession evidence joins, deliberate
  timeout/device-loss tests and authoritative pause. Never call the CPU test
  `GameSession::advanceOneTick()` after completed GPU work to backdate a revision.
  SIM-06 must prepare and publish real future-tick topology transactions.

## Verified result

Both native and WASM applications build. `native-tests.log` records 41 passing
focused cases: existing sailing/dock controls and registry checks, independent
cargo mass/exclusion, two independent water bodies, legacy attachment behavior,
and a real owned rope break with generation-safe handles and confirmed evidence.
The one-slot event test proves Busy before a second tick is encoded, refuses
premature readback disable, then resumes after the event is consumed.
Nine UI lifecycle/control checks pass, including pending confirmation and Leave.

`journey.json` and `browser.json` record actual keyboard and button input in
hardware Chrome 152 on Radeon 890M. All fourteen stages pass with no browser
exceptions or uncaptured GPU errors. Observed results:

| Interaction | Measured result |
| --- | --- |
| F hooks the tow eye | Attachment confirmed, generator still its own body |
| Hold Q for 60 player ticks | Endpoint distance 6.47 → 4.68 m; generator moves and rises |
| Release Q; hold/release Z | Motor returns to zero after each key release |
| W sails with cable attached | Generator moves about 1.7 m from its initial position; rope stays intact |
| Release button | Attachment removed, load continues under its own motion/contact |
| R reset | Boat returns to berth; generator returns to its seabed placement |
| Resize and Leave | Scene stays valid; all owned bodies/events drain before navigation |

Every stage checks completed ≤ submitted ≤ encoded ≤ scheduled, the configured
in-flight tick bound, and pose/event observation no later than completed time.
The private campaign inventory remains zero throughout; no salvage reward is
fabricated. This is functional gameplay evidence, not visual/performance acceptance.
The package hashes identify the exact tested web files.

## Reproduce after a relevant change

```sh
nix-shell --run 'bazel build -c opt //tests:voxy_tests //:voxy_native'
nix-shell --run 'bazel-bin/tests/voxy_tests --gtest_filter="GpuAuthoredShapes.Live*:GpuEventReadback.*:GpuPhysicsTest.*Attachment*:CoveMovement.*:CoveNavigation.*:FixtureRegistry.*"'
node scripts/test_salvage_preview.mjs
nix-shell --run 'EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j 8'
```

Package `web/` plus the built `voxy_wasm.{js,wasm,data}`. Run the ordinary input
journey once after changes that affect it:

```sh
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_REPORT=/tmp/towing-browser.json VOXY_SMOKE_COVE_PLAYER=/tmp/towing-journey node scripts/smoke_integrated_wasm.mjs /path/to/package salvage-cove
```

## Next useful playable work

Implement the future-tick campaign preparation adapter and join physical cargo
identity to a real salvage objective. Add a delivery zone and one durable reward
only after that ownership/receipt path exists. Then implement capture-envelope
checks and cradle latch/unlatch with conserved momentum and exactly one cargo
mass representation. Preserve F/Q/Z and the proven sailing interaction. Do not
resume art/lighting screenshot matrices while those gameplay steps are unfinished.
