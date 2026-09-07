# BOOT-05 native route regression evidence

**Five visible native launch routes passed after the allocation-range fix,
before the later retained-source fix.** Each opened a normal Wayland
window, captured its presented scene after 120 rendered frames, requested exit,
logged `Main loop ended` and `Application shutdown complete`, and returned 0.
No timeout, application error, GPU validation error or device loss was observed.
The agent inspected every original PNG; this is not human acceptance testing.

| Route | Observed scene | PNG / log |
|---|---|---|
| `lego_world.cfg` | Studded beach, ocean, distant terrain | [PNG](native-route-lego_world.png) / [log](native-route-lego_world.log) |
| `lego_shore.cfg` | Studded shore study and ocean | [PNG](native-route-lego_shore.png) / [log](native-route-lego_shore.log) |
| `voxy.cfg` | Existing overhead textured landscape | [PNG](native-route-voxy.png) / [log](native-route-voxy.log) |
| `ridgebreak.cfg` | Motorcycle, rider and course start | [PNG](native-route-ridgebreak.png) / [log](native-route-ridgebreak.log) |
| `salvage.cfg` | Primitive dock, workshop blocks, moored craft and water | [PNG](native-route-salvage.png) / [log](native-route-salvage.log) |

The cove remains a static prototype scene. This check does not claim assembly,
salvage, vehicle control or game-loop implementation.

The earlier five-route run is preserved separately in
[native-route-before-reset-fix](native-route-before-reset-fix/README.md).
The links above and results below refer to the repeated five-route run on
`75f801bb…`. The later change affects only salvage metadata lifetime. The
affected cove route was checked again on final native `c4dc00f1…`; see
[final salvage capture](native-final-salvage/native-route-summary.json) and
[integration report](implementation.md). Old route evidence is not relabeled
as a later binary.

## Artifact and settings

- Native executable: `build-salvage-native/bin/voxy_native`.
- SHA-256 before and after all five runs:
  `75f801bb9a668a0c82b0f20b5652d0fbb36a1dd822ebbc273faba7ff5f0a8bf9`.
- Authored application/config/module input fingerprint reported by the render
  worker at freeze:
  `9402d4e5b002a62ee41139038a07d30c67d6ea0965131ab472d4a86d9e099ecc`.
  The [input manifest](range-fix-inputs.json) records 330 authored files, including
  concurrent `build_model.hpp/.cpp` work that was **not linked into this binary**.
  It is a source-workspace record, not an exact list of executable dependencies.
  The independently measured executable hash above is the primary runtime identity.
- Host: Linux x86_64, Ubuntu 26.04.1; project Nix runtime. Adapter:
  **AMD Radeon 890M Graphics (RADV STRIX1)**, integrated GPU, Vulkan.
- `WGPU_BACKEND=vulkan`; no `VOXY_WINDOW_BACKEND` override. Actual backend:
  Wayland. All configs request 1280×720 logical windows. Each actual framebuffer,
  surface and PNG is **1600×900** at the host's 1.25× content scale.
- Original config values retained: raycast path, resolution scale 1, Fifo/VSync,
  validation enabled. The only CLI additions were screenshot path and frame delay.
  [Per-route metadata](native-route-summary.json) includes exact command, config
  digest, output digest, dimensions, wall duration and exit result.
- Physics requested WebGPU in each config. Actual logs identify `webgpu_soft`;
  no CPU fallback occurred. Body-slot allocations remain the config defaults:
  256 for either LEGO route, 131,072 for voxy/RIDGEBREAK, 1,024 for salvage.
- Every run emitted the host decoration warning `libdecor-gtk-WARNING: Failed
  to initialize GTK` and its plugin-load message. Window creation, GPU setup,
  drawing and shutdown succeeded despite that warning. It is retained in the
  logs and not represented as a warning-free launch.

GPU ownership was exclusive to this sequence; no other worker ran GPU tests
during it. Server/probe binaries had been built before this final sequence.
These short stationary captures provide **no frame-time, memory, capacity,
performance, stability or completed-match acceptance claim**.

## Reproduction

The harness uses an existing binary and refuses to reuse a screenshot path. Run
from the repository root, with an empty output directory and exclusive GPU use:

```sh
nix-shell --run 'WGPU_BACKEND=vulkan python3 scripts/validate_native_routes.py --binary build-salvage-native/bin/voxy_native --output /tmp/voxys-native-routes-new'
```

The command recorded for this run used `docs/validation/salvage/BOOT-05` as its
output directory. [Runner output](native-route-run.log) is preserved. The capture
path uses the normal swapchain frame; it is not the native offscreen benchmark.

## WRECKWATER graphical bootstrap

**The actual graphical client launched, authenticated and continued rendering.**
The [graphical log](native-route-wreckwater-graphical.log) records a real Wayland
window, the AMD Vulkan adapter, a 1600×900 presentation surface, successful
application initialization, the connection becoming `connected`, and six frame-loop
log entries through application time 14.363 seconds. This is a graphical native
application run, not a transport-probe-only substitute.

The loopback server and three transport probes supplied the other peers required
by the existing four-peer match barrier. Server telemetry ended at tick 786 with
all four peers active, `fail_reason=none`, `match_fault=0`, no transaction aborts
and no replay error. There were no application/GPU error messages or device-loss
messages in the server or graphical logs.

**Limits and observed failures:**

- The OS window screenshot failed. GNOME reported that its Shell screenshot
  interface was unavailable, then its X11 fallback could not capture the Wayland
  window. The screenshot tool timed out after 10 seconds and produced **no PNG**.
  Its [complete diagnostic output](native-route-wreckwater-os-screenshot.log) is
  preserved. No screenshot guard, display security setting or application code
  was changed to bypass that limitation. WRECKWATER's visible pixels were not
  independently inspected; its launch evidence is the actual window/surface,
  connection and frame-loop logs.
- The existing graphical configuration deliberately rejects screenshot
  automation (`src/app/application.cpp`,
  `validWreckwaterApplicationClientConfig`). The harness used no screenshot,
  benchmark, teleport or tour flags for this client.
- After the screenshot attempt, the harness sent SIGTERM to its five child
  processes. The server handled it and returned **0**. Each probe returned **3**
  with `Client probe interrupted`. The graphical client returned **−15** and
  did not run its normal application shutdown path. This is explicitly **not**
  an orderly WRECKWATER exit or a completed-match result.
- Server telemetry reported **783 rejected character frames** out of 1,588
  polled frames. The cause was not investigated in this bounded launch check.
  This evidence does not establish healthy input replication, gameplay
  correctness, loss-free transport or completion of the WRECKWATER contract.
- Frame-loop rates and server tick timings remain in the original logs as
  diagnostics. They are **not performance acceptance evidence** for this short,
  stationary, shared server/client scenario.

[Exact process arguments, hashes and exit statuses](native-route-wreckwater.json),
[server log](native-route-wreckwater-server.log),
[peer 2](native-route-wreckwater-probe2.log),
[peer 3](native-route-wreckwater-probe3.log),
[peer 4](native-route-wreckwater-probe4.log), and
[runner output](native-route-wreckwater-run.log) are retained. Credentials were
fresh random values held in memory; saved arguments redact them. The connection
used only `127.0.0.1`, ephemeral port **33687**, with session/match/world and both
epochs set to **1**. The four-peer topology is an existing WRECKWATER requirement,
not a multiplayer design choice for the separate salvage game.

| Artifact | SHA-256 |
|---|---|
| Native graphical application | `75f801bb9a668a0c82b0f20b5652d0fbb36a1dd822ebbc273faba7ff5f0a8bf9` |
| WRECKWATER server | `5dbb545124c16fcbd9cd8ebf34300241e075988281af833269cda5ec700193ef` |
| WRECKWATER client probe | `47d1197fbdd263d5117362c0610d2fd60348cdcff8198bb8795f650525ba5f22` |

The server's compiled authority content digest was
`b4d5983670909fdc210128840d400f872d414df89b23d31f845277a2c1398470`.
Successful authentication demonstrates that this graphical artifact and the
probes accepted that content identity. The native executable hash was verified
unchanged after the run.

### Exact reproduction recipe

From the repository root, use a configured native CMake build. Build the local
server/probes without rebuilding the application under test:

```sh
nix-shell --run 'cmake --build build-salvage-native --target wreckwater_server wreckwater_client_probe -j4'
```

The [actual build output](native-route-wreckwater-build.log) is preserved. Once
the matching application is built and no other GPU test is running, launch:

```sh
nix-shell --run 'WGPU_BACKEND=vulkan python3 docs/validation/salvage/BOOT-05/native-route-wreckwater-harness.py --output /tmp/voxys-wreckwater-route-new'
```

The recorded run omitted `--output`, selecting this evidence directory. The
[harness](native-route-wreckwater-harness.py) refuses to overwrite existing
WRECKWATER evidence. It creates four new credentials, launches the server with
port 0 and `--max-ticks 1800`, obtains the assigned port from its readiness log,
and starts probes 2–4 with `--max-ticks 2400`. Graphical peer 1 uses `voxy.cfg`
plus all required `--wreckwater-*` bootstrap fields and `--log-level debug` to
record the frame loop. After all four connections are established it waits
three seconds, attempts `/usr/bin/gnome-screenshot --window --file <output>`,
then records SIGTERM cleanup. It does not install desktop tools or alter the
application. All five child processes were reaped; no native/server/probe or
screenshot process remained when GPU ownership was handed to browser testing.
