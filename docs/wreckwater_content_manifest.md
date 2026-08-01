# WRECKWATER content compatibility manifest

Status: hashing core, generated build identity, configured Bazel and CMake
authority closures, embedded authority WGSL, native handshake, and
replay-prefix integration complete

## What the core provides

`WreckwaterContentManifestBuilder` produces one deterministic SHA-256 digest
from an explicit allowlist of:

- schema versions;
- integer and IEEE binary32 gameplay rules;
- authoritative asset bytes;
- presentation asset bytes.

The caller declares every allowed logical path before supplying values.
Unknown, missing, duplicate, mismatched, or non-canonical paths fail.

Input order does not matter. Entries are sorted by their canonical logical
path before the root digest is produced.

The builder does not:

- walk directories;
- read file timestamps or permissions;
- use locale-sensitive text rules;
- allocate memory;
- retain asset spans after `addBytes()` returns.

It is suitable for native and WASM builds. It is not intended to run in the
60 Hz authority tick. A build tool or startup path should use it once.

## Hash format

Manifest schema 1 uses a SHA-256 tree.

Each value first gets a leaf digest:

```text
SHA-256(
    "VOXY/WRECKWATER/CONTENT-LEAF/V1" ||
    value-type-u32-le ||
    payload-size-u64-le ||
    canonical-payload
)
```

The root hashes the manifest schema, entry count, total payload bytes, and
every sorted entry. Each entry includes its kind, value type, path length,
path bytes, payload size, and leaf digest. Fixed domain strings separate leaf,
entry, root, and end records.

Integers and float bits are little-endian. Negative float zero is normalized
to positive zero. NaN, infinity, and nonzero IEEE subnormals are rejected.
Rejecting subnormals prevents FTZ/DAZ target modes from silently changing a
rule value before it reaches the manifest.

The full 256-bit digest is the preferred compatibility identity.

Replay schema 2 has a 64-bit `contentHash`. The core exposes the first
eight SHA-256 bytes as one big-endian integer for that field. This truncation
has at most 64-bit collision resistance.

SHA-256 here is unkeyed. It detects content differences. It does not prove who
created a manifest and does not authenticate a client, server, or replay.

## Canonical logical paths

Paths use lowercase ASCII:

```text
a-z  0-9  -  _  .  /
```

They cannot start or end with `/`. Empty segments, `.` segments, `..`
segments, backslashes, uppercase aliases, and paths longer than 127 bytes are
rejected.

These are logical manifest names. They are not filesystem paths to open from
untrusted input.

## Fixed limits

The default and hard ceilings are:

- 128 entries;
- 127 bytes per logical path;
- 512 MiB for one payload;
- 1 GiB total payload.

Callers may lower each limit. Size checks happen before hashing or state
mutation. A failed addition can be corrected and retried.

## Current first-slice authoritative inputs

The live headless server currently has no terrain attached. The generated
authority identity hashes the exact bytes of 82 checked-in files: ten WGSL
modules, 32 C++ translation units, and 40 C++ headers.

- the ten loaded authority WGSL modules listed below;
- match, live-world, vessel-damage, replay, character-movement, and character
  authority-bridge sources;
- the CPU capsule mover and shared terrain-topology contract;
- the logging implementation used by authority code;
- outer protocol, WRECKWATER payload schema 5, session-transport, and native
  TCP framing/handshake v3 sources;
- the GPU context/resource, trusted-shader-source, and WebGPU physics sources
  used by this server;
- the terrain mip generator called by the GPU physics backend's optional
  terrain upload path;
- authority-runtime and native server configuration sources.

Hashing the rule source bytes makes every in-source constant, spawn descriptor,
roster rule, snapshot cadence, terminal-settlement policy, and final server
override part of the identity without a second manually copied table of
configuration fields. Any byte change in a declared source or WGSL file changes
the digest.

The ten WGSL entries are:

```text
shaders/physics_ballistic.wgsl
shaders/physics_ccd.wgsl
shaders/physics_deterministic_primitives.wgsl
shaders/physics_broad_phase.wgsl
shaders/physics_narrow_phase.wgsl
shaders/physics_dynamic_solver.wgsl
shaders/physics_attachments.wgsl
shaders/physics_islands.wgsl
shaders/physics_queries.wgsl
shaders/physics_event_readback.wgsl
```

WreckCam, terrain/presentation assets, unused lockstep shaders, secrets, bind
addresses, ports, replay output paths, and wall-clock data are not inputs.
External compiler, dependency, driver, and GPU identity also remain outside
this content digest; those need a separate build or execution-environment
fingerprint.

The root `//shaders:shaders` target is a glob containing presentation and
unused shaders. The authority generator never walks it. Hashing that whole
directory would make the authority identity change for unrelated edits.

## Presentation manifest for the cove

If one combined product digest is desired, presentation entries should be
separate from authoritative entries but included in the same root.

The current cove path uses:

- `voxy.cfg` values that affect terrain, water, lighting, and material output;
- an authored-cove algorithm revision and its final config values;
- `data/generated/td_seed_1234_8192.ldh`;
- `data/generated/td_seed_1234_8192_albedo.jpg`;
- `data/generated/ocean_environment.png`;
- Color, NormalGL, and Roughness maps for Ground054, Ground037, Grass001, and
  Rock050;
- the explicit render WGSL modules actually loaded by the selected raycast,
  water, primitive, sky, and underwater paths.

This presentation set is not yet used by the headless authority. It should not
be silently inferred from `data/BUILD` or `shaders/BUILD`.

## Bazel and CMake integration

The shared game library contains:

```text
src/game/wreckwater_content_manifest.hpp
src/game/wreckwater_content_manifest.cpp
```

`wreckwater_authority_content.allowlist` is the single checked source/WGSL
allowlist. `tools/generate_wreckwater_build_content.py` receives exact file
dependencies from Bazel or CMake and compares their complete path set with that
allowlist. Missing, extra, duplicate, unsorted, aliased, directory, or glob
inputs fail the build. Every quoted first-party include must also resolve to
exactly one manifested source/header input. Missing, aliased, or ambiguous
include edges fail generation, including edges inside transitively included
headers.

The generator uses the same manifest-schema-1 byte grammar and emits
`generated/wreckwater_build_content.hpp` containing:

- the 32-byte SHA-256 digest;
- the 64-bit replay compatibility value;
- the full lowercase hexadecimal digest;
- the manifest schema, entry count, and payload byte count;
- the ten authority WGSL modules as immutable byte-exact source blobs.

Bazel exposes this as `//:wreckwater_build_content`; CMake exposes the
`wreckwater_build_content` interface target. Both native server and client
probe depend on it. There is no runtime directory scan.

Both native build systems use a separate headless dependency graph. Bazel
uses:

```text
//:wreckwater_server
  //src/server:wreckwater_headless_authority
    //src/gpu:wreckwater_authority_gpu
    //src/network:wreckwater_authority_network
    //src/physics:wreckwater_authority_physics
```

That graph keeps client replication/runtime, window and swapchain
implementation, deterministic adversity, Jolt, Box3D, render, and broad terrain
targets out of the authority binary. `PhysicsWorld` selects its backend through
a link-time factory seam. The broad product factory still supplies all three
existing backends; the headless factory supplies only `WebGpuSoft`.

CMake mirrors the same closure with:

```text
wreckwater_server
  wreckwater_headless_authority
  wreckwater_build_content
  wgpu::webgpu
  glm
```

`wreckwater_headless_authority` contains the same 31 library translation units
as the Bazel authority graph. Server main is the 32nd. It is deliberately
separate from `voxy_core`; the native sandbox, client probe, tests, and WASM
continue using their existing broad targets.

Run the configured-graph guard after changing authority dependencies:

```bash
nix develop -c bazel run \
  //tools:check_wreckwater_authority_bazel_closure
```

It uses configured `cquery`, ignores tools and implicit dependencies, and
requires the 32 translation units and 40 headers selected by the headless
authority plus server main to match the 72 `source` entries in the allowlist
exactly. WGSL inputs remain checked by the generator because they are runtime
shader data, not C++ target sources.

CMake also compares its declared 32 translation units with the manifest during
configuration. On Linux, build and inspect the isolated server with:

```bash
nix develop -c cmake -S . -B build-cmake -G Ninja
nix develop -c cmake --build build-cmake \
  --target wreckwater_server_closure_check
```

The closure target reads `compile_commands.json`, requires exactly the 32
manifested translation units, inspects defined binary symbols, and checks
dynamic dependencies. It rejects client, render, Window/swapchain, Jolt, and
Box3D code plus GLFW, X11, Wayland, Jolt, Box3D, and zstd runtime libraries.

The generator reads each declared file exactly once. The same in-memory byte
snapshot drives both the SHA-256 root and the embedded WGSL. Authority WGSL
must be ASCII, contain no NUL, and use LF line endings.

The headless server passes the generated WGSL bundle through every initial and
lazy WebGPU authority module. A nonempty bundle is strict: a missing, empty, or
duplicate requested logical path fails instead of reopening a deployment file.
Failure to create any required module, including the event-readback module,
prevents authority startup before peers can be serviced.

The include audit proves the first-party quoted-header closure of the selected
authority translation units. The configured Bazel closure guard proves that
the selected first-party source files are exactly those inputs. The CMake
configure assertion and binary closure target independently prove its selected
translation units and linked Linux binary.

Release builds must run from one immutable or read-only source checkout and
publish binaries, generated identity, and replays from that same checkout. The
generator's one-read snapshot closes its own hash/embed race; it cannot make a
separately mutating compiler checkout safe.

## Server and protocol integration

Native TCP wire version 3 carries the full digest in `ClientHello` and
`ServerAccepted`, and transports WRECKWATER payload schema 5 after admission.
The server compares all 32 bytes while the socket is pending, before the
authenticated phase, active-peer vector, roster, or `Connected` event. The
client independently compares all 32 response bytes before its `Connected`
event.

Both binaries log the full 64-hex-digit value at startup. The server passes the
generated big-endian 64-bit prefix to replay schema 2. Native admission never
uses the truncated replay value.

A future replay schema may store all 32 bytes. The existing 64-bit field stays
for schema-2 compatibility.

The current authenticated plaintext TCP restriction remains unchanged.
Public deployment still needs TLS, mTLS, or QUIC. A signed release manifest
would be a separate feature from this unkeyed compatibility hash.

## Tests

The isolated test covers:

- definition and addition order independence;
- a full SHA-256 cross-platform golden;
- single-byte asset changes;
- duplicate, missing, unknown, and mismatched entries;
- path alias rejection;
- signed-zero float normalization plus non-finite and subnormal rejection;
- entry, per-payload, total-payload, and oversized-limit failures;
- stable fixed storage.

The golden digest in the test was generated independently with Python
`hashlib.sha256` and `struct.pack` from the byte grammar above. It was not
copied from this C++ implementation.

The generator test adds:

- an independent fixed digest/header golden;
- a bit change at every byte position in representative source and WGSL
  inputs;
- proof that one immutable read supplies both the digest and embedded WGSL;
- missing, extra, duplicate, glob, path-alias, type, and sort-order failures.

The Bazel closure checker additionally fails on either an allowlisted source
that is not configured into the headless graph or a configured first-party
source that is absent from the allowlist.

Native TCP tests use a correct deployment key with a one-byte content mismatch
and prove the server has no active peer, no `Connected` event, and no
authenticated connection. A hostile raw server returns a wrong digest to prove
the client also fails before `Connected`. GPU-resource tests reject missing,
empty, and duplicate embedded shader paths. The authority GPU test omits the
event-readback shader and proves there is no filesystem fallback.
