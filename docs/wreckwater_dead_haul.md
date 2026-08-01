# WRECKWATER: DEAD HAUL

Status: production direction and completion contract  
Date: 2026-07-30

This document defines the game Voxys is building.

It is not a claim that the game is complete.

## Product promise

Four industrial salvage crews fight over physical cargo while their boats
fracture, flood, capsize, and shed usable debris.

Every major hit must change:

- hull topology;
- buoyancy;
- handling;
- available tools;
- the crew's immediate plan.

The memorable outcomes must emerge from authoritative simulation. They must
not be substituted with scripted destruction cinematics.

## Match

- 12 players.
- Four crews of three.
- Eight minutes.
- First-person, embodied crew play.
- Near-future industrial salvage sport.
- No fixed classes.
- Any player may helm, grapple, attack, repair, pump, or cut.

### Flow

1. Locate high-value wreckage.
2. Grapple or cut out cargo.
3. Tow it toward an extraction buoy.
4. Fight rival crews for ownership.
5. Repair breaches and redistribute weight.
6. Cut away sections that threaten the vessel.
7. Bank cargo.
8. Risk another haul or protect the lead.

The Crown Wreck appears late enough to create a comeback without erasing the
value of the first part of the match.

Sinking removes unbanked cargo. It does not create a long spectator timeout.
A crew returns in a weaker rescue skiff and can still contest the Crown Wreck.

## Core tools

- Harpoon and powered winch.
- Impact lance or deck cannon.
- Cutting torch.
- Foam patcher.
- Pump.
- Helm and engine controls.

Cargo has real mass. Tow force changes steering. Floodwater changes effective
buoyancy and center of mass.

## Viral loop

The shareable unit is a readable physical chain reaction:

> Rival rams pontoon -> edge breaks -> compartment floods -> crew cuts the
> section loose -> cargo cable swings it into the rival engine.

Required social systems:

- authoritative WreckCam replay clips;
- shareable room and replay links;
- crew names, liveries, banners, and blueprint codes;
- crew radio, proximity voice, contextual pings, and moderation;
- one-button rematch and party invite;
- cinematic replay camera;
- server-validated vessel blueprint sharing.

Progression is cosmetic and sidegrade-based. A new player remains mechanically
competitive.

## First releasable proof: finished 2v2 slice

The first slice is deliberately smaller than the launch match. It is not
allowed to replace the launch target.

- Four real clients.
- Two identical two-person skiffs.
- One coastal arena.
- One heavy reactor cargo.
- Four-minute match.
- Helm, harpoon, impact lance, cutter, patcher, and pump.
- One final-quality weather state.
- Extraction, scoring, sinking, respawn, rematch, and replay.

### Gameplay acceptance

The slice is complete only when all of these are proven:

- Native and browser clients join a real authoritative server.
- Players walk on moving boats and can fall overboard.
- A physical collision can break a structural edge.
- A broken edge can open a breach.
- Flooding changes six-degree-of-freedom vessel motion.
- A detached significant section becomes an authoritative rigid body.
- Cargo can attach, tow, transfer, cut loose, sink, and bank.
- A sunk crew respawns and can finish the match.
- The score, winner, and replay hash agree on server and clients.
- Four clients converge under latency, loss, duplication, and reordering.
- Reconnection and authority migration do not duplicate cargo or score.

An in-process fixture with colored primitives does not satisfy this gate.

### Visual acceptance

Every final capture must show authored intent at foreground, middle distance,
and horizon.

Required:

- a final vessel silhouette and readable team identity;
- physically based wet paint, exposed metal, rubber, glass, rust, and fracture
  interiors;
- authored coastline landmarks and gameplay navigation cues;
- no visible terrain contour bands, stretched macro texture, or placeholder
  primitive colors;
- sky, clouds, fog, water, and lighting forming one weather system;
- wakes, spray, leaks, flooding, impacts, smoke, sparks, and structural stress
  feedback;
- readable hands, tools, crew animation, damage state, objective state, and
  HUD hierarchy;
- stable temporal presentation without shimmer, disocclusion trails, or
  distracting LOD transitions;
- presentation that remains readable for color-vision deficiencies and reduced
  motion settings.

The independent visual critic owns pass/fail. A feature author cannot approve
their own final capture.

### Performance acceptance

Reference hardware and exact presets must be recorded with every result.

- Client simulation: fixed 60 Hz.
- Authoritative server: fixed 60 Hz.
- Client frame: p95 within the 60 FPS budget during the composed match.
- Server tick: p95 within its 60 Hz budget with four connected clients.
- No normal per-frame GPU transform readback.
- No capacity overflow.
- No unbounded debris, particles, events, snapshots, or replay storage.
- No latency hitch when a hull fractures or the Crown Wreck appears.
- Network tests include at least 100 ms RTT, jitter, 2% loss, duplication, and
  reordering.
- Browser and native results cover the same visible scene and simulation state.

Average FPS from an offscreen or static benchmark does not satisfy this gate.

## Authoritative architecture

```text
authenticated client input
          |
          v
authoritative session and canonical command order
          |
          v
fixed-tick vessel, cargo, character, fracture, and flooding state
          |
          +------> snapshots, corrections, replay, WreckCam
          |
          v
GPU physics and water interaction
          |
          v
direct GPU culling and rendering
```

The network layer owns transport-independent session behavior.

Native sockets, browser WebTransport, and browser WebRTC are adapters. They do
not own gameplay rules.

The production float GPU world is not advertised as cross-platform lockstep.
Authoritative replay needs a certified state contract that includes vessel
orientation, angular velocity, constraints, hull topology, breaches, cargo,
characters, and scoring.

## Required engine work

### Simulation

- Breakable rope, winch, spring, hinge, and attachment constraints.
- Authored convex and compound collision shapes.
- Structural-component to rigid-body ownership.
- Six-degree-of-freedom flooding, buoyancy, drag, and center-of-mass changes.
- Moving-platform character collision, swimming, ladders, boarding, and
  impulses.
- Material-aware damage, repair, cutting, and fracture events.
- Bounded significant fragments and secondary debris.

### Multiplayer and server

- Application-facing authoritative session.
- Real native and browser transport adapters.
- Session host, listener, authentication, matchmaking, reconnection, and
  deployment.
- Vessel-capable prediction and correction state.
- Snapshot interest that preserves constraint and structural dependencies.
- Replay checkpoints and deterministic event reconstruction.

### Rendering and content

- Authored mesh import and optimized GPU mesh data.
- PBR material and texture streaming.
- Skinned characters, first-person hands, tools, and animation.
- Decals, fracture interiors, wetness, damage masks, and localized flooding.
- Ocean interaction VFX and final atmosphere.
- Temporal anti-aliasing or an equivalent stable presentation path.
- Final UI, audio, accessibility, analytics, and live-operations surfaces.

## Current evidence

The current engine already provides valuable foundations:

- spectral ocean shared by rendering and GPU physics;
- GPU-resident rigid bodies and direct rendering;
- broad phase, narrow phase, solver, CCD, islands, sleep, queries, and events;
- deterministic structural, cargo, flooding, replay, networking, and
  distributed-server fixtures;
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
