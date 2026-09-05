# RIDGEBREAK

Status: production direction and completion contract
Date: 2026-07-31

This document defines the motocross game Voxys is building. It replaces
WRECKWATER: DEAD HAUL as the product direction. The engine work already proven
under WRECKWATER stays: terrain rendering, spectral ocean, GPU physics,
deterministic replay, authoritative networking, and performance tooling.

This is not a claim that the game is complete.

## Product promise

A physics-driven motocross game about big air, speed, and surviving your own
mistakes. A rider on a real motorcycle rips across an open mixed-biome world,
hucks off cliffs and ramps, pulls flips and whips, races rivals, and wipes out
so badly the game wants you to share the clip.

Every major moment must change:

- how the bike handles;
- the rider's state (on the bike, airborne, crashed, remounting);
- the crew's immediate plan in a race;
- what the player can do next.

The memorable outcomes must emerge from authoritative simulation. They must
not be scripted stunts or canned crash cinematics.

## Match

- 12 players per session (freeride lobby or race grid).
- Freeride: every player in one open world. Anyone may ride, stunt, race an
  informal route, or spectate anyone else.
- Race: checkpoints on the same world, lap timing, physics-based collisions
  allowed.
- Six-minute sessions. First-person or chase-cam view.
- No fixed classes. Any player may ride, race, or hunt the next big trick.

### Flow

1. Spawn in the world on your bike.
2. Rip the terrain: ridge lines, cliff faces, canyons, a lake shore.
3. Find or build the next big-air feature: launch, flip, land.
4. Race rivals over checkpoints when a race starts.
5. Wipe out. Crash. Dust yourself off. Remount.
6. Bank a score from tricks and race results.
7. Share the clip.

## Core systems

- Real motorcycle physics: chassis, front fork, rear swingarm, tires, engine,
  brakes, steering, rider weight shift.
- Big-air trick detection: flips, spins, whips, seat grabs, can-cans.
- Landing quality: clean, slap, or crash. Perfect landings bank bonus score.
- Wipeouts and rider ragdoll. Crash recovery without a spectator timeout.
- Open mixed-biome world: alpine peaks, desert canyons, coastal flats.
- Procedurally placed big-air features and race routes on the shared terrain.

## Viral loop

The shareable unit is a readable physical chain reaction:

> Rider overshoots the ridge, lands rear-wheel-first, loops out, the bike
> cartwheels, the rider ragdolls down the slope, and the bike tumbles into the
> lake. Clip. Share. Beat that.

Required social systems:

- authoritative replay clips (ReplayCam);
- shareable session and replay links;
- rider names, liveries, bikes, and colors;
- proximity voice and contextual pings;
- one-button rematch and party invite;
- cinematic replay camera;
- server-validated bike setup sharing.

Progression is cosmetic and sidegrade-based. A new player remains mechanically
competitive.

## First releasable proof: finished 2v2 slice

The first slice is deliberately smaller than the launch session.

- Four real clients.
- Two identical freeride bikes.
- One mixed-biome region (alpine + desert + coast).
- Six-minute match.
- Freeride plus one checkpoint race on the same world.
- Throttle, brake, steer, seat/lean, and restart inputs.
- One final-quality weather state.
- Scoring, race results, respawn, rematch, and replay.

### Gameplay acceptance

The slice is complete only when all of these are proven:

- Native and browser clients join a real authoritative server.
- Players ride the terrain at speed without the physics unbinding.
- A jump can clear a gap and land cleanly.
- A flip and a whip each score once, exactly once.
- A crash separates rider from bike and both behave physically.
- A player can remount and finish the session.
- The score, winner, and replay hash agree on server and clients.
- Four clients converge under latency, loss, duplication, and reordering.
- Reconnection and authority migration do not duplicate tricks or score.

An in-process fixture with colored primitives does not satisfy this gate.

### Visual acceptance

Every final capture must show authored intent at foreground, middle distance,
and horizon.

Required:

- a final bike silhouette and readable rider + livery identity;
- physically based paint, chrome, rubber tires, metal frame, and damage;
- authored coastlines, landmarks, and ride cues;
- no visible terrain contour bands, stretched macro texture, or placeholder
  primitive colors;
- sky, clouds, fog, water, and lighting forming one weather system;
- dust, roost, spray, crash debris, skid marks, and speed feedback;
- readable rider, tools, damage state, trick state, and HUD hierarchy;
- stable temporal presentation without shimmer or distracting LOD pops;
- presentation that remains readable for color-vision deficiencies and reduced
  motion settings.

The independent visual critic owns pass/fail. A feature author cannot approve
their own final capture.

### Performance acceptance

Reference hardware and exact presets must be recorded with every result.

- Client simulation: fixed 60 Hz.
- Authoritative server: fixed 60 Hz.
- Client frame: p95 within the 60 FPS budget during the composed session.
- Server tick: p95 within its 60 Hz budget with four connected clients.
- No normal per-frame GPU transform readback for the player's own bike.
- No capacity overflow.
- No unbounded debris, particles, events, snapshots, or replay storage.
- No latency hitch when a bike crashes or the race starts.
- Network tests include at least 100 ms RTT, jitter, 2% loss, duplication, and
  reordering.
- Browser and native results cover the same visible scene and simulation state.

Average FPS from an offscreen or static benchmark does not satisfy this gate.

## Authoritative architecture

Same authority chain as the proven engine:

```text
authenticated client input
          |
          v
authoritative session and canonical command order
          |
          v
fixed-tick bike, rider, trick, and race state
          |
          +------> snapshots, corrections, replay, ReplayCam
          |
          v
GPU physics and terrain interaction
          |
          v
direct GPU culling and rendering
```

The network layer owns transport-independent session behavior. Native sockets,
browser WebTransport, and browser WebRTC are adapters. They do not own
gameplay rules.

## Required engine work

### Simulation

- Motorcycle chassis, fork, swingarm, and tire model.
- Rider articulation and ragdoll.
- Terrain contact at speed (heightfield raycast or GPU heightfield).
- Mixed-biome heightfield world, ramps, and big-air features.
- Trick detection from authoritative state only.

### Multiplayer and server

- Application-facing authoritative session.
- Real native and browser transport adapters.
- Session host, listener, authentication, matchmaking, reconnection.
- Prediction and correction state for bike + rider.
- Snapshot interest for the race route and freeride spectators.
- Replay checkpoints and deterministic event reconstruction.

### Rendering and content

- glTF 2.0 mesh import and optimized GPU mesh data.
- PBR material and texture pipeline (metallic-roughness).
- Skinned rider, bike, and animation.
- Decals, damage, wetness, and localized dust/debris.
- Ocean and lake interaction VFX and final atmosphere.
- Temporal stability or an equivalent stable presentation path.
- Final UI, audio, accessibility, analytics, and live-operations surfaces.

## Current evidence

The current engine already provides valuable foundations:

- spectral ocean shared by rendering and GPU physics;
- GPU-resident rigid bodies and direct rendering;
- broad phase, narrow phase, solver, CCD, islands, sleep, queries, events;
- deterministic structural, replay, networking, and distributed-server
  fixtures;
- native and WebAssembly/WebGPU targets;
- performance and browser automation infrastructure.

These are engine proofs. They are not yet proof of the finished game, final
visual quality, network deployment, or fun.

## Human gate

The commercial direction remains a hypothesis until human players complete
comparative sessions.

Playtest evidence must record:

- completion and crash rate;
- time to understand the objective;
- coordination and communication;
- recovery after failure;
- rematch intent;
- spontaneous clip sharing;
- accessibility blockers;
- fun rating and support time.

Synthetic observations never satisfy the human gate.
