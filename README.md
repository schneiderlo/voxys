# Voxys

Voxys is the engine and active game prototype for
[WRECKWATER: DEAD HAUL](docs/wreckwater_dead_haul.md): an authoritative
multiplayer salvage game built around physical boats, cargo, damage, flooding,
and recovery. The linked completion contract is intentionally explicit about
what is proven and what remains unfinished. Current engine tests and terrain
captures are foundations; they are not a claim that the 2v2 slice or the
12-player launch game is complete.

## Build Systems

This project supports multiple build systems. **Bazel** is recommended for development, while **CMake** is available for standard integration.

## Nix Development Shell

The repo includes a flake and `shell.nix` for local toolchain setup. Use it
before fetching vendored third-party dependencies or building:

```bash
nix-shell
./scripts/fetch_deps.sh
bazel build -c opt //:voxy_native
bazel test //tests:config
```

`nix develop` provides the same toolchain through `flake.nix`. `nix-shell` is
recommended for this asset-heavy checkout because it does not copy the whole
working tree into the Nix store.

The shell provides GCC, Bazelisk-backed `bazel`, CMake, Rust, `uv`, dual-backend
GLFW 3.4, Wayland/X11, Vulkan, and runtime paths for native WebGPU runs. Native
Linux sessions prefer Wayland so fractional desktop scaling does not make the
renderer process an oversized XWayland framebuffer; X11 remains the fallback.

For physical-resolution fullscreen rendering without a display-refresh cap:

```bash
bazel run -c opt //:voxy_native -- --fullscreen --uncapped
```

Use `--vsync` to opt back into FIFO presentation. Neither option changes the
internal resolution scale or scene quality.

The fullscreen performance path keeps the terrain, sky, shadow field, and
depth result cached while the camera is stationary. The animated water stays
live in a separate full-resolution indexed-geometry pass. Five nested 64×64
camera-following clipmap rings and a stretched horizon ring are displaced by
two 256² directional spectrum cascades plus four long swells. The same live
surface drives geometry and material normals. The final ocean path includes exact
dielectric Fresnel/TIR, Beer–Lambert refraction and scattering, filtered HDR
environment reflection, foam, ACES output, and a dedicated underwater
distortion/shaft/particle pass. The cloud environment, foam field, and
infinite-ocean sand/rock material are generated procedurally in code. Moving
the camera refreshes camera-dependent opaque data without switching the ocean
back to the legacy fullscreen material.

Run the five-view native benchmark with:

```bash
bazel run -c opt //:voxy_native -- --fullscreen --benchmark --no-validation
```

Run the deployed, real-interaction browser matrix with:

```bash
node scripts/benchmark_browser.mjs --headed
```

It throws exact 100, 1,000, 5,000, and 10,000 body workloads from one player
view onto the real terrain. It records uncapped headroom and GPU stage
diagnostics, and rejects a profiler-induced throughput collapse. Add
`--modes score,headroom,diagnose` only while the headed Chrome window is
genuinely visible; the runner rejects compositor-throttled RAF.

For the authoritative WASM renderer-throughput gate, open the app in a hardware
WebGPU Chrome instance with `renderThroughput=1`, then run:

```bash
node scripts/benchmark_wasm_render.mjs \
  --port 9333 \
  --expected-width 3440 --expected-height 1454 \
  --warmup-frames 128 --measured-frames 1500 \
  --batch-frames 64 --repeats 3 --minimum-fps 700
```

Historical isolated renderer-throughput runs on an AMD Radeon 890M (RDNA 3,
Chrome 149, Vulkan/ANGLE) measured 944.76, 944.52, and 949.73 FPS at 3440×1454.
All 4,500 measured frames retired through the WebGPU queue. A historical native
five-view run measured 393.6 FPS overall at 3440×1440.

Those numbers are retained only as engine microbenchmark evidence. They use a
physical-size offscreen target or heavily cached views and do not represent a
moving, displayed, composed WRECKWATER match. They must not be used for the
current 60 FPS product gate; that gate requires the exact visible scene,
simulation, clients, server, frame-time percentiles, and reference hardware
defined in the WRECKWATER completion contract.

Physics is selected through a backend facade:

- `webgpu_soft`: GPU-resident simulation and direct GPU rendering.
- `box3d_reference`: pinned CPU oracle and explicit WebGPU fallback.
- `jolt_legacy`: migration baseline; retained until removal is approved.

The WebGPU path includes terrain and water contacts, dynamic broad/narrow
phase, a deterministic colored Soft Step solver, islands/sleeping, CCD,
asynchronous queries/events, replay, and telemetry. Normal frames do not copy
all body transforms through the CPU.

Useful options:

```text
--physics-backend webgpu
--physics-backend box3d
--physics-backend jolt
--physics-cpu-fallback
--no-physics-cpu-fallback
```

See [the implementation plan](specs/gpu-physics-distributed-world-implementation-plan.md),
[the measured physics report](docs/physics-baseline.md), and
[the product slices](docs/product-vertical-slices.md).

Physics sandbox controls:

- Mouse wheel: select ball, cube, rectangle, capsule, or cylinder.
- First left click: capture the pointer.
- Later left clicks: throw the selected object. Hold for continuous fire.
- Right click: throw 128 selected objects at once.

## Terrain Assets

Voxys can import generated terrain from Terrain Diffusion through the offline
tool at `tools/terrain_diffusion_import.py`.

See [Terrain Diffusion Import](docs/terrain_diffusion.md) for the workflow.

## Python Tooling

Python utilities use `uv`.

```bash
uv sync
uv run python tools/test_terrain_diffusion_import.py
uv run python tools/terrain_diffusion_import.py --help
```

### 1. Bazel (Recommended)

Bazel provides hermetic builds, fast incremental compilation, and easy sanitizer integration.

#### Common Commands
| Action | Command |
|--------|---------|
| **Build Native** | `bazel build //:voxy_native` |
| **Run Native** | `bazel run //:voxy_native` |
| **Run Tests** | `bazel test //tests:voxy_tests` |
| **Build WASM** | `bazel build --config=wasm //:voxy_wasm` |
| **Serve WASM** | `bazel run --config=wasm //tools:serve_wasm` |
| **IDE Setup** | `bazel run @hedron_compile_commands//:refresh_all` |

### Browser Physics Telemetry

The local WASM server receives a structured optimization snapshot from the
browser once per second. No F1 overlay is required.

```bash
bazel run --config=wasm //tools:serve_wasm

# In another terminal:
bazel run //tools:read_physics_telemetry
bazel run //tools:read_physics_telemetry -- --watch
```

The latest sample is stored in `/tmp/voxys-telemetry.json`. A bounded history
is stored in `/tmp/voxys-telemetry.ndjson`. Set `VOXY_TELEMETRY_FILE` or
`VOXY_TELEMETRY_HISTORY_FILE` to override those paths.

Telemetry is enabled automatically on localhost. Use `?telemetry=0` to disable
it, or `?telemetry=1` to enable it explicitly on another development host.

For reproducible WebGPU stage captures, scaling matrices, A/B confidence
intervals, and regression thresholds, see
[WebGPU performance profiling](docs/performance-profiling.md).

#### Sanitizers (Debug & Verification)
Enable sanitizers using `--config=<sanitizer>`.

*   **AddressSanitizer (ASan)**: Detects memory errors (buffer overflows, use-after-free).
    ```bash
    bazel test --config=asan //tests:voxy_tests
    bazel run --config=asan //:voxy_native
    ```

*   **ThreadSanitizer (TSan)**: Detects data races.
    > **Note**: TSan requires disabling ASLR.
    ```bash
    # Build first
    bazel build --config=tsan //tests:voxy_tests
    # Run manually with ASLR disabled
    TSAN_OPTIONS="suppressions=.tsan_suppressions" setarch $(uname -m) -R ./bazel-bin/tests/voxy_tests
    ```

*   **UndefinedBehaviorSanitizer (UBSan)**: Detects undefined behavior (integer overflow, null usage, etc.).
    ```bash
    bazel test --config=ubsan //tests:voxy_tests
    ```

*   **MemorySanitizer (MSan)**: Detects uninitialized memory reads.
    > **Note**: Requires an instrumented `libc++` toolchain for full accuracy. System libraries may produce false positives.
    ```bash
    bazel test --config=msan //tests:voxy_tests
    ```

### 2. CMake

Standard build system for the project.

```bash
# Configure
cmake -S . -B build

# Build
cmake --build build -j

# Run
./build/bin/voxy_native

# Test
ctest --test-dir build
```

### 3. Make (Wrapper)

Authentic convenience wrapper around CMake commands.

| Target | Description |
|--------|-------------|
| `make native` | Configure & build native target |
| `make run-native` | Build & run native target |
| `make test` | Run unit tests |
| `make wasm` | Build WASM target (requires EMSDK) |
| `make serve-wasm` | Build & serve WASM target at `localhost:8080` |
| `make clean-native` | Clean native build directory |
| `make clean-wasm` | Clean WASM build directory |

## Git Hooks

This project uses git hooks to ensure code quality. After cloning, run:

```bash
./scripts/setup-hooks.sh
```

### Pre-commit Hook

The pre-commit hook runs `bazel test //tests:voxy_tests //tools:terrain_diffusion_import_test`
before each commit. Commits are blocked if either suite fails.

## Optional RIDGEBREAK prototype

The motocross branch is integrated without replacing the existing terrain
demo. Open the browser with `?experience=ridgebreak`, or launch native with
`--config ridgebreak.cfg`. Without that option, `voxy.cfg`, the authored
terrain, and the 2048-pixel macro texture remain the defaults.
Prototype documentation is retained in `docs/ridgebreak-prototype.md`;
its original default-launch instructions are superseded by this opt-in.

## Build, explore, salvage implementation

The separate expedition game is tracked in
[GAME_IMPLEMENTATION_TODO.md](GAME_IMPLEMENTATION_TODO.md). This self-contained
plan defines the game, architecture, asset workflow, task dependencies and
completion gates. Implementing agents must read it before claiming work.
Completed tasks link to evidence; a checked task is not a completed game.

The first scene is an optional **cove preview**:

```bash
bazel run -c opt //:voxy_native -- --config salvage.cfg
```

In the browser, open `?experience=salvage`. Use **R** or the browser's **Reset**
button to reset the scene. Browser **Leave** waits for owned scene objects to
retire before returning to the default LEGO World route. Construction,
recovery jobs and progression are still implementation tasks.

The **authored cove candidate** now places the complete skiff, modular dock and
generator together: `--config salvage_cove.cfg`, or browser
`?experience=salvage-cove`. Walk with **WASD**, jump with **Space**, and use
**E** near the boat to board, use the helm or return to the dock while alongside.
At the helm, **W/S** controls throttle and **A/D** steers the physical skiff.
**E** leaves the helm; **R** returns the player and boat to the starting berth.
Browser buttons offer the same interactions; the native in-game panel shows
the nearby action, material stock and save status. The eleven-part hull now floats, moves and turns using its
compiled mass, displacement and propeller frame. The player follows its deck.
The cove again uses **stepped LEGO terrain and round studs**. Walking, boat
and cargo collision now match that surface, including open gaps between studs
and pontoons. The actual skiff and generator pass grounding checks; the player
walks up a brick terrace and lands on studs after jumping.
[Brick terrain restoration and playable checks](docs/validation/salvage/LEGO-01/terrain-r01/README.md).
The boat also collides with the authored dock and generator. A separate 420 kg
salvage generator rests on the seabed nearby: **F** hooks/releases its tow eye,
hold **Q** to reel in and **Z** to pay out. Browser buttons provide the same
winch controls. Sail to tow the load; **R** recovers both boat and cargo.
The browser panel also shows the cable's actual length as it reels or pays out.
**P / Pause** now freezes the expedition after queued movement finishes;
**P / Resume** continues with neutral controls. Leave also works from pause.
[Pause behavior and checks](docs/validation/salvage/SAVE-04/pause-r01/README.md).
[Playable towing and checks](docs/validation/salvage/SIM-08/towing-r01/README.md).
The authored boat, dock and cargo now cast sun shadows onto authored parts,
including the bricks you build with. The browser panel puts the main actions
first; expand the job, tools, camera and machinery sections when needed.
[Presentation work and verification scope](docs/validation/salvage/LOOK-01/presentation-r01/README.md).
The pontoons, beams and dock/deck plates now use molded cream, teal and orange
models with panel details and rounded edges. Their separate visual revisions
preserve existing builds, connections, weight and prices. Editable Blender
sources are included; the materials use solid colors without image textures.
[Structural art, compatibility and checks](docs/validation/salvage/LOOK-01/toy-art-r01/README.md).
The helm, winch, engine and propeller now share that style. Their new models
retain the same physical interfaces, and the propeller clears its guard.
[Machinery source, compatibility and checks](docs/validation/salvage/LOOK-01/machinery-r01/README.md).
The boat, dock, cargo and built bricks also cast shadows onto the brick terrain
and ocean. Shadows move with the parts; the shadowed seabed stays visible through
the water. [Scene-shadow integration and checks](docs/validation/salvage/REND-03/scene-shadows-r01/README.md).
Water-relative drag, latching, full mission progression, the robot, dry hull interiors and final visual quality remain unfinished. This is initial sailing, not complete
machine simulation. [Sailing implementation and checks](docs/validation/salvage/SIM-03/sailing-r01/README.md),
[dock collision](docs/validation/salvage/SIM-03/dock-collision-r01/README.md).

At the starting dock, **B** or **Workshop** opens the starter design editor.
Select a part, move or rotate it, snap to a free socket, remove it, keep a valid
change, or undo. Green means the design connects; red means it does not.
Amber means the pointer has no supporting part: point at a part to place,
or use **Keep** to retain the current valid position.
**G / Focus part** frames the selected part; **M / Whole boat** frames the build.
Right-drag orbits, Shift + right-drag or middle-drag pans, and the wheel zooms.
The view buttons and A/D, W/S also orbit and zoom. Framing leaves space for the
controls; narrow browser windows put the panel below the build. **B** returns
to the dock. [Camera controls and desktop/browser checks](docs/validation/salvage/PLAY-02/camera-r01/README.md).
**Launch / Enter** now rebuilds the physical boat from kept changes and returns
you to the dock. The edited boat uses its new mass, collision, buoyancy, steering
and tow point. **Undo launch / I** and **Redo launch / O** restore accepted builds;
**U** undoes unlaunched design changes. Reset preserves the edited craft.
For a first edit, select **Cargo cradle**, Remove, Keep, then Launch: the boat
changes from eleven parts / 1,035 kg to ten parts / 945 kg. The winch can then be
moved onto the freed deck socket. Sail into range before hooking the generator.
Launch refuses blocked standing space or missing propulsion without changing
the sailing boat. Use the browser expedition save control to keep accepted edits across reloads.
Unsaved changes disappear on Leave; controller support remains unfinished.
The **parts drawer** now adds paid parts: choose a type, **Add / V**, Keep,
then Launch. **C** cycles types with the canvas focused. Fresh cove worlds start
with **48 material**; Reset preserves the remaining stock. Pontoons cost 24,
so two purchases expand the starter from 1,035 to 1,275 kg. Prices appear before
Launch. Undo/redo preserves exact paid identities and costs; dismantling returns
the lower catalog salvage value. [Initial paid construction and sailing checks](docs/validation/salvage/PLAY-02/parts-r01/README.md).

The workshop now includes **individual studded bricks**: 1×2, 2×2 and 2×4.
Clear the **Cargo cradle** first: Next / Tab, Remove / Delete, Keep / E.
Choose a colored brick from the palette or press **1, 2, 3**. Point at the
freed deck or another brick, press **R** to rotate, then click a green ghost
to place it. The selected brick tool stays active: keep clicking to build with
the same size and orientation. **Select / Esc** stops and keeps your placed
bricks. Then click existing parts to select them; click the selected part to
move it. **U** undoes a kept edit. **Launch / Enter** leaves out the unused
preview, applies the cost and
rebuilds the physical boat. Eight mixed, stacked bricks now pass native/browser
construction, save/reload, boarding and sailing with exact ownership and costs.
The audited scene budget supports a tested 64-brick design; complex designs
still obey collision and connection limits. The native workshop panel shows
the selected tool, cost and controls with readable text; its camera
leaves room for the panel and palette. Final art, controller support and the
remaining workshop tools are unfinished.
[Brick-builder controls, capacity and evidence](docs/validation/salvage/LEGO-02/builder-r01/README.md).
[Continuous tool and current verification](docs/validation/salvage/PLAY-02/continuous-r01/README.md).
[Native panel implementation and scope](docs/validation/salvage/UX-01/native-hud-r01/README.md).

**Paint your bricks:** use the named colour swatches in the browser, or **Y**
in the native game. A colour chosen with a brick tool stays active for further
placements and size changes. To repaint a built brick, select it, choose its
colour, then **Keep / E** and **Launch / Enter**. Repainting costs no material.
**Original** restores the brick's authored colour; **Undo / U** reverses a kept
paint edit. Colours survive boat saves and blueprint export/import.
[Paint controls, ownership and validation](docs/validation/salvage/UX-01/brick-paint-r01/README.md).

The workshop also has **part settings**. Select a propeller, helm or winch;
**X** toggles it, **L** cycles thrust/steering limits, and **N** reverses
propeller drive. Keep, then Launch applies the change at no material cost.
Undo/redo and Reset preserve settings. Engine drive networks and advanced rope
settings remain unfinished. [Working settings and gameplay checks](docs/validation/salvage/PLAY-02/settings-r01/README.md).
In the browser, expand **Saved designs** to name, save, copy, load or update a
blueprint. Backups and export/import are included. Designs survive browser
reloads; loading one shows its part cost before Launch. The library saves the
design. Use the separate expedition save control below to keep boat and mission
progress. Native storage and named-design controls remain unfinished.
[Persistent designs and the reload-to-sailing check](docs/validation/salvage/PLAY-02/designs-r01/README.md).
[Live launch, moved-winch towing and verification](docs/validation/salvage/PLAY-02/launch-r01/README.md).
[Connected edit/session implementation](docs/validation/salvage/PLAY-02/refit-r01/README.md).
[Starter ownership and sailing checks](docs/validation/salvage/PLAY-02/ownership-r01/README.md).
[Workshop implementation, checks and remaining work](docs/validation/salvage/PLAY-02/workshop-r01/README.md).

The logical expedition save format is implemented and tested on native and WASM.
[Save format and storage contract](docs/salvage-session-save-format.md).
Linux file storage now preserves two complete save copies and passes process-crash
recovery checks. [Native storage contract and remaining integration](docs/salvage-native-save-store.md).
Browser world storage also passes reload, competing-tab and browser-crash checks.
[Browser storage contract](docs/salvage-browser-save-store.md).
The browser cove now supports **manual expedition saves**: Pause, then select
**Save expedition**. Bookmark the resulting address. Reload that address in the
same browser profile and on the same origin to return to the last checkpoint;
the loaded game starts paused. Resume continues play. Leaving does not autosave.
Bought parts, remaining materials, accepted jobs, player position, boat/cargo
motion, cable length and water time survive reload. Missing selected saves fail
visibly instead of silently starting a new world. Three real reloads pass,
including further purchases, reeling attached cargo and sailing afterward.
[Save/load implementation and checks](docs/validation/salvage/SAVE-04/resume-r01/README.md).
The Linux cove also supports manual expedition saves: close the workshop,
press **P** to pause, then **F10** to save. Wait for **Expedition saved** in the
window title. The startup log prints the save folder and the exact command to
reopen that world. Supply `--expedition-world <world-id>` with
`--config salvage_cove.cfg`; an optional `--expedition-root <absolute-folder>`
selects a separate save location. The loaded game starts paused. Two actual
process restarts preserve bought parts and materials, including a further
purchase after loading. Another process cannot open the same active save.
[Native save/restart implementation and checks](docs/validation/salvage/SAVE-04/native-host-r01/README.md).
The generator job now has **durable delivery** in the browser: accept the job,
build a rig with lifting clearance, hook and raise the generator, then steer
the loaded craft into the harbor. **Deliver / H** becomes available when the
cargo is inside the zone and moving slowly enough. The game secures the load
and pauses while saving. **Delivered and saved: +60 material** appears only
after storage confirms the save; Resume then continues play. If saving fails,
retry with **Save expedition**. A real reload preserves the paid build, secured
generator and reward; pressing Deliver again pays nothing.
[Delivery implementation, a working rig and actual reload checks](docs/validation/salvage/PLAY-04/delivery-r01/README.md).
Linux delivery also saves automatically and survives an actual process restart.
A real unwritable-folder check stays paused until permission is restored and
**F10** retries successfully; the reward is paid only once.
[Desktop delivery, retry and restart checks](docs/validation/salvage/PLAY-04/native-delivery-r01/README.md).
The harbor lift is now integrated. After the generator is delivered and saved,
walk to the dock and press **K** to power it. Installation places the banked
generator on the pier and saves the upgrade. **F** requests four-line attachment;
hold **Q** to raise or **Z** to lower; release the key to stop. **F** releases the
rig or cancels a waiting attachment. Reposition an off-center boat with the
**R** Rescue action, then Resume after its automatic save. Save while suspended with **P**, then **F10** on
Linux or **Save expedition** in the browser.
Desktop and browser lift/save/restart/lower/release/sailing now pass. Compliant
slings absorb the reproduced wave impact while retaining their force and break
limits. A fresh browser journey also verifies construction, delivery and the
upgrade together. The second job, final machinery art, world selection/
export/import, periodic autosave, Windows storage and wider recovery cases
remain unfinished.
[Live harbor implementation, verified journeys and retained failures](docs/validation/salvage/PLAY-04/harbor-live-r01/README.md).
**Rescue / R** now returns your existing boat and every fitted part to the dock.
It releases towing and harbor cables first. Undelivered cargo returns to its
recovery site; delivered cargo and the powered harbor stay intact. Rescue saves
automatically and stays paused until the save succeeds. Retry a failed save with
**F10** on Linux or **Save expedition** in the browser, then Resume.
Paid additions and remaining materials survive repeated rescues and reloads.
At the dock workshop, **Rebuild starter / H** restores the original starter boat,
including removed loaned parts. Purchased parts go into owned storage with their
condition and settings intact. **Add part** uses matching stored parts before
charging for new ones. Rebuild and stock reuse save automatically before Resume.
Keep or discard unfinished edits first; the service clears previous launch undo.
Rebuild also protects up to four custom boat designs. **Load recovered design / K**
restores the selected layout in the workshop; Launch uses owned parts and normal
costs. **Next recovery / J** selects a backup; **Remove recovery design / F** removes
only that backup and saves before Resume. A full list never silently overwrites
a design. Existing saved blueprints stay in the design library.
[Protected designs and verified recovery journeys](docs/validation/salvage/PLAY-05/recovery-design-r01/README.md).
Cove waves now follow the submitted physics ticks and preserve their phase
through pause, saving and reload. Native delivery passes; the final browser
package passes construction, towing, delivery, reload and sailing in 29 stages.
The current cove uses one physics tick per submission; broader water simulation
and displayed-frame performance remain unfinished.
[Wave-clock correction and exact verification scope](docs/validation/salvage/SIM-05/cove-clock-r01/README.md).
**Cut weld / C** now cuts the named nearby weld outside the workshop. Release
all cables and stand on the dock or a machine section first. Each cut preserves
owned parts and protects the intact design before changing the boat. A full
recovery-design list refuses a new design; it never overwrites one. Broken builds
keep that protection until rebuilt.

Separate sections have their own collision, flotation, motion and saved root
identity. The player stays with the supporting deck, including when the helm
breaks away. Cutting saves automatically and waits for Resume. **Rescue / R**
returns every section without repairing its welds. The dock workshop can rebuild
the starter, store paid parts, and restore the protected design from that stock.
Native and browser journeys now pass actual cuts at sea, section reloads,
Rescue and repeated rebuilding without paid-part duplication.
[Live cutter implementation and exact verification scope](docs/validation/salvage/MECH-05/live-cut-r01/README.md).

The wider paid boat's boarding regression is also fixed: Spawn, Launch and Rescue
use the complete wave field for placement. Desktop and browser Rescue, boarding
and sailing checks pass. General impact-driven fracture, arbitrary joint
retargeting, the full loss/exploit matrix and broader water/performance work
remain unfinished.
[Atomic recovery checks](docs/validation/salvage/MECH-05/root-recovery-r01/README.md).
[Water-height correction and boarding evidence](docs/validation/salvage/MECH-05/boarding-r01/README.md).
[Section save implementation](docs/validation/salvage/MECH-05/root-resume-r01/README.md).
[Cove archive and startup contract](docs/salvage-cove-save-format.md).

The separate **pontoon inspection** scene loads the authored candidate through
the real asset pipeline:

```bash
bazel run -c opt //:voxy_native -- --config salvage_asset_fixture.cfg
```

Browser route: `?experience=salvage-asset`. Use **0–3** for automatic/near/middle/far
detail, **WASD** to fly, **E/Q** to move up/down, and **R** to reset. Its registry
is `data/salvage/fixture-pontoon-v2.json`; that file selects and verifies the
cooked content without changing C++ or shaders. All five inspection registries
currently select staged `v2-rc01`; its [exact runtime admission check](docs/validation/salvage/ASSET-04/release-runtime.md)
passes, with independent review and publication still pending. The models are static inspection
objects. This route does not represent accepted cove art or working boat physics.
It uses an unobstructed layout with no cove scenery bodies and brighter ambient
lighting so side surfaces and socket wells can be inspected. Reset retains the
model uploads; Leave waits for their actual GPU completion.

Press **G** to cycle clean view, rulers and socket X-ray guides. Browser buttons offer
the same modes. White ruler marks are 1 m along the length and 0.32 m vertically;
amber outlines show the selected model's render bounds. The separate axis stand
uses red for right, green for up and blue for forward. Socket mode instead shows
each socket's local +X key in red, outward +Y in green, +Z in blue, and its
required clearance in amber. Guides use canonical metadata and stay outside
physics and inventory. They show through surfaces; clean view shows actual
geometry. Reset restores clean view. Native capture configurations
can set `[game] asset_fixture_guides = "dimensions"` or `"sockets"`.
See the [guide validation record](docs/validation/salvage/ASSET-04/guides.md).

Inspection configurations can set `[game] asset_fixture_lod` to `"auto"`,
`"near"`, `"middle"` or `"far"` for the initial detail level. The selected level
must exist in every authored bundle. Reset returns to Auto and clean view.

The separate **assembly check** uses `salvage_assembly_fixture.cfg`, or browser
route `?experience=salvage-assembly`. It shows two prototype crossbeams on a
pontoon pair and a separate stacked pair. The game validates all six placements
and five socket connections before rendering. Beam boxes use the existing v1
catalog dimensions; they do not yet have modeled socket wells. Socket mode
shows the connected endpoints. [Assembly evidence and remaining work](docs/validation/salvage/ASSET-04/assembly.md).

The [matched motion record](docs/validation/salvage/ASSET-04/native-motion.md)
contains desktop/browser videos through all detail levels and the commands
for replaying that inspection. Capture overhead is not game performance.

The separate **hierarchy inspection** scene adds an asymmetric Blender stand
with nested rotations/scales and shared geometry beside the pontoons. Launch
`--config salvage_hierarchy_fixture.cfg` or `?experience=salvage-hierarchy`.
Only Auto and Near are available because the stand has one detail level.
Rulers now target the authored stand and display its full composed render bounds;
see the [bounds check](docs/validation/salvage/ASSET-04/hierarchy-bounds.md).
The [hierarchy record](docs/validation/salvage/ASSET-04/hierarchy.md) includes
pre-export geometry checks, matched native/browser views, reproduction and
known limits. This remains technical inspection; game art is not approved.

The **rotation galleries** show welded pontoon pairs in all 24 orientations.
Use `salvage_rotations_a.cfg` / `salvage_rotations_b.cfg` on desktop, or
`?experience=salvage-rotations-a` / `?experience=salvage-rotations-b` in the browser.
Each scene contains twelve pairs and supports the same inspection controls.
[Rotation evidence and remaining checks](docs/validation/salvage/ASSET-04/rotations.md).

The [material validation record](docs/validation/salvage/ASSET-04/materials.md)
includes native/browser pixel tests, reproducible close-view captures and the
remaining lighting and inspection-scene defects. Passing these renderer tests
does not complete the visual gate in the implementation plan.

The first **authored kit** has separate narrow/broad starter and cargo
inspection scenes. Launch `--config salvage_kit_broad.cfg`,
`salvage_kit_narrow.cfg` or `salvage_kit_cargo.cfg`; browser routes are
`?experience=salvage-kit-broad`, `salvage-kit-narrow` and `salvage-kit-cargo`.
Both starter boats contain eleven authored parts with seventeen validated
connections. The scenes show the winch, helm, outboard, cradle, generator and
crate using the existing inspection controls. They are static assemblies;
the separate cove route now supports walking, sailing and initial towing.
See [kit evidence and remaining work](docs/validation/salvage/ASSET-05/README.md).

The isolated **material inspection** compares corrected pontoon and winch
surfaces at every detail level. Launch `--config salvage_material_fixture.cfg`
or browser `?experience=salvage-materials`. It uses the same flight, detail and
guide controls. [Material evidence](docs/validation/salvage/ASSET-06/browser-r01/README.md)
records the native/browser checks. This is an unfinished material calibration;
it does not replace the kit or the cove.

The **surface detail inspection** adds physical texture scale and a rubber helm
grip: `--config salvage_metric_fixture.cfg`, or browser
`?experience=salvage-material-detail`. It contains three isolated parts.
[The latest surface checks](docs/validation/salvage/ASSET-06/shading-r02/README.md)
include smoother wheel/drum shading and matching native/browser views. The
[texture-scale checkpoint](docs/validation/salvage/ASSET-06/metric-r01/README.md)
remains available. Texture seams, lighting and final scene quality are unfinished.
The [lighting investigation](docs/validation/salvage/ASSET-06/environment-r01/README.md)
now feeds the [actual surface-detail scene](docs/validation/salvage/ASSET-06/environment-consumer-r01/README.md)
through an explicitly enabled filtered lighting path. Matched native/browser
views and lifetime checks pass. Other routes retain legacy lighting; the authored
cove now uses this lighting and opaque/water composition. Full dynamic water,
shadows and final material quality remain unfinished.

See the [baseline handoff](docs/validation/salvage/G00/report.md) for build,
launch and validation recipes and known limitations. Existing WRECKWATER,
terrain, LEGO and RIDGEBREAK routes retain their separate roles.

The authored terrain browser regression runs the shipping physics shader on
hardware WebGPU without taking images:

```bash
node scripts/validate_authored_terrain_browser.mjs /tmp/voxy-terrain-report.json
```

It checks cargo support on LEGO studs, penetration correction, open space,
129 bodies across workgroups, and smooth terrain. Set `VOXY_TEST_CHROME` to
the Chrome executable and `VOXY_SMOKE_GPU=gaming-x11` for a normal X11 window.
Node 22+ is required. An optional second path tests another shader revision;
the report records its hash. A completed GPU readback alone is not a pass:
the solver must publish contacts and apply the expected support force.
