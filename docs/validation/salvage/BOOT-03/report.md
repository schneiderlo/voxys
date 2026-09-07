# BOOT-03 — visible rendering and memory baseline

Date: 2026-09-07. Runs: root. Independent evidence review: `lego_gameplay`.
This review read the captured data and images; it launched no GPU work.

**Result:** the legacy landscape completed visible startup, movement, shore
views and the full playground journey. Separate native/browser runs captured
RSS, WASM allocation, JS heap and identified DRM memory. The **16.67 ms
presentation target is not met**. Memory results are sampled and explicitly
partial; they are not exact physical-memory peaks or leak certification.

**Recommendation:** accept these as the recorded engineering baseline, with
the provenance and measurement limits below attached. BOOT-03 requires a
baseline, not achievement of the later performance gates. Do not interpret
this recommendation as passing QA-02, native/browser timing parity, final
gameplay, or the new salvage mode. No checkbox was changed by this reviewer.

## What was captured

| Evidence | Observed outcome and scope |
|---|---|
| [Visible run](visible-world.json), [log](visible-world.log) | Passed startup and both journeys; `experience=default`, actual URL `/`; 1920×1080 render output |
| [World summary](world-journey/summary.json) | 12 s idle, 10 s held-W movement, then six 6 s camera views |
| [Playground report](playground-journey/report.json), [stress data](playground-journey/stress.json) | Tower/impact/reset/replay, 48-brick pool, eight balls, 10 s stress, exit/re-entry |
| [Browser memory](browser-world-memory.json), [log](browser-world-memory.log) | Startup and both journeys passed; memory status `partial`; separate instrumented run |
| [Native memory](native-world-memory.json), [log](native-world-memory.log) | `lego_world.cfg`; 600-frame screenshot and orderly exit 0; memory status `partial` |

There is no `playground-journey/summary.json`. Its statistics below are derived
from `stress.json`, using the same sorting/index convention as the world
validator. Raw arrays remain in those JSON files rather than this report.

Images reviewed include [walking](world-journey/walking.png),
[the full 48-brick pool](playground-journey/full-pool.png) and
[native landscape](native-world-memory.png). The movement capture ends
underwater above the studded terrain. This is the existing character/terrain
path, not a successful test of the proposed swimming/boarding game.
[Startup](visible-world.png) and [shore overview](world-journey/shore-overview.png)
provide the corresponding visible scene evidence.

## Host, display and provenance

Host identity comes from [BOOT-04](../BOOT-04/report.md); current display and
power settings were captured in [environment.json](environment.json).

| Setting | Recorded value |
|---|---|
| Host | Ryzen AI 9 HX 370; Radeon 890M integrated GPU; 24 logical CPUs; 60,254,692 KiB MemTotal |
| OS/session | Ubuntu 26.04.1 LTS; Linux 7.0.0-31-generic; GNOME/Wayland |
| GPU identity | AMD PCI 1002:150e; observed DRM device `0000:65:00.0`, kernel driver `amdgpu` |
| Native API/driver | Vulkan, RADV STRIX1; Nix Vulkan inventory reports Mesa 26.1.2 in [BOOT-02 toolchain output](../BOOT-02/toolchain.log) |
| Browser | Chrome 152.0.7977.82; AMD/RDNA-3, fallback=false, timestampQuery=true |
| Browser driver limit | Browser JSON does not record its loaded userspace driver version; do not assume the native Nix Mesa version proves Chrome's loaded library |
| Physical monitor | LG ULTRAWIDE, HDMI-1, 3440×1440 at 84.957077 Hz; compositor scale 1.25 |
| Browser viewport | 1920×1080 CSS/device-emulation viewport, DPR approximately 1; actual timestamped render output and PNGs 1920×1080 |
| Native viewport | 1536×864 logical; 1920×1080 framebuffer; Fifo/VSync presentation |
| Power | `balanced`; CPU governor `powersave`, energy preference `balance_performance`; GPU DPM `auto` |
| Unrecorded environment | AC/battery state, clock/thermal trace, exact compositor scheduling, and per-run browser userspace driver build |

Chrome ran visibly with `--ozone-platform=wayland --enable-features=Vulkan`;
the harness did not select a software adapter. Its retained log nevertheless
warns that Wayland is not compatible with the requested Vulkan presentation
flag. The AMD non-fallback adapter and completed timestamp queries establish
hardware WebGPU execution; they do not establish which compositor backend
Chrome ultimately selected. Driver/display attribution should be strengthened
in the next comparable capture.

These runs used **pre-BOOT-05 application binaries**, not the current working
tree's new salvage lifecycle or DATA-02 integration. Base Git revision was
`7563f61fd536df7de5d209ff35d3cd2099ebcbb6` plus the BOOT-02 uncommitted inputs.
The page build ID is the packaging placeholder; content hashes establish identity.

| Artifact | SHA-256 / evidence |
|---|---|
| Native executable used | `a4acf4e7c44c6159db7e9a2c7b966b555190b05b2fb6e42e9088e126be20c01b`; [native manifest](../BOOT-02/native-final-artifacts.json) |
| WASM module used | `cba69955b8bcaa6cc3407c56970174912abbe27e4962864a000f26907ba5a712` |
| WASM JS used | `eebadd19497bffc525540fc14c77a7c897367b837621e43fe1fe41f4cf67521e` |
| WASM preload data | `7097b3d96d36d1d1221b77dd93f33357be07d39b713eb03fe33acc367e64b4dc` |
| Authored WASM input fingerprint | `4b1659945758ecb82edd3811a1819267bc41e8c4d6a048367ca9ee3776a26ad3`; [manifest](../BOOT-02/cmake-wasm-fresh-artifacts.json) and [build recipe](../BOOT-02/cmake-wasm-fresh.md) |

The disposable WASM package remains `/tmp/voxys-boot02-cmake-fresh-web`; its
module hash was independently checked during this review. The native build
path has since been rebuilt and the old executable was not retained. Rebuilding
today therefore requires a **new capture**, not attribution of these results
to the new binary.

The exact visible runner and three dependencies are preserved in
[snapshot/](snapshot/voxys-boot03-visible-baseline.mjs). All four copies match
the original [runner hashes](visible-runner-hashes.json). No pre-capture
memory-runner hash or snapshot was retained. Its later salvage-route edits
mean the current runner is not exact historical proof. The memory helpers were
unchanged through these captures according to their owner, but their later
hashes alone cannot establish capture-time identity. The raw memory reports
and logs are retained; exact historical memory-runner replay is unavailable.

## Visible timing and startup

All times below are milliseconds. CPU means application telemetry work;
RAF means browser callback/presentation cadence. GPU means a completed
timestamp interval for the frame command stream, including GPU physics and
render passes; it excludes CPU work, queue uploads, presentation and query
readback. These clocks must not be added or treated as photon latency.

The harness polls telemetry on RAF, so CPU readings can repeat when application
and presentation updates differ. GPU samples are deduplicated by reported
frame ID and arrive asynchronously: observed idle samples were three frames
old; walking samples were three to ten frames old. The first GPU sample of a
phase can describe earlier work. No inference of CPU/GPU overlap is made.

Percentiles sort values and select index `floor(n × p)`. RAF/CPU counts support
descriptive p50/p95/p99 for these short samples, not a long-session guarantee.
Each GPU phase has only 12–22 completed samples, fewer than the budget helper's
30-sample minimum. Therefore only GPU median and maximum are promoted here;
the raw JSON's sparse tail percentiles are not accepted as robust p95/p99.

| Scene / duration | RAF/CPU n | RAF p50 / p95 / p99 | CPU p50 / p95 / p99 | GPU n | GPU median / maximum |
|---|---:|---|---|---:|---|
| Idle shore / 12 s | 658 | 18.30 / 20.10 / 20.70 | 0.40 / 0.70 / 1.10 | 22 | 1.226 / 2.813 |
| Held-W movement / 10 s | 561 | 17.90 / 19.70 / 20.10 | 0.40 / 0.80 / 1.00 | 19 | 6.897 / 8.881 |
| Inland / 6 s | 334 | 18.00 / 19.80 / 20.20 | 0.40 / 0.70 / 0.90 | 12 | 0.904 / 1.689 |
| Shore overview / 6 s | 336 | 17.90 / 19.50 / 20.00 | 0.30 / 0.70 / 0.90 | 13 | 1.278 / 1.874 |
| Cliff / 6 s | 334 | 18.00 / 19.60 / 20.10 | 0.60 / 1.00 / 1.10 | 12 | 2.720 / 2.978 |
| Close cliff / 6 s | 330 | 18.30 / 19.90 / 20.20 | 0.70 / 0.90 / 1.00 | 12 | 1.369 / 1.772 |
| Distant / 6 s | 329 | 18.30 / 20.00 / 20.50 | 0.70 / 1.00 / 1.10 | 12 | 2.237 / 2.605 |
| Return / 6 s | 332 | 18.10 / 19.80 / 20.20 | 0.70 / 0.90 / 1.10 | 12 | 2.360 / 2.552 |
| Full-pool impacts / 10 s | 616 | 16.60 / 23.70 / 24.60 | 3.50 / 5.30 / 6.20 | 21 | 3.898 / 6.733 |

The full pool first reached 48 sleeping bricks. Eight shots then produced a
peak of 56 awake objects; the stress endpoint had 43 bricks and eight balls,
six awake and 45 sleeping. This measures the legacy bounded playground,
not 256-part vehicles or the proposed salvage physics workload.

**Target gap:** movement RAF p95 is 3.03 ms above 16.67 ms; full-pool p95 is
7.03 ms above it. Full-pool CPU p95 is 1.30 ms above the proposed 4 ms budget.
RAF intervals above 16.67 ms were 479/561 during movement, 293/336 at the shore
overview, and 285/616 during impacts. These are threshold exceedances, not
OS-reported dropped-display-frame counts. No sampled RAF interval exceeded
33.3 ms; the largest was 25.0 ms in the playground. The short-sample p99 values
are below 25 ms, but do not certify the later whole-route hitch requirement.

The visible startup check took **7,957 ms** from navigation through completed
GPU telemetry, screenshot and image validation. It is not an isolated
time-to-first-frame measurement. The separate instrumented browser took
8,077 ms and is excluded from the timing table. Native instrumented logs show
application initialization at 3.017 s, screenshot request at 12.966 s and
shutdown complete at 13.293 s. There is **no native walking/RAF/per-frame CPU
or GPU distribution** in this capture; 600 frames divided by elapsed time
would not supply one.

Each browser used a new temporary profile and process. OS file cache, driver
shader caches and system caches were not cleared. “Cold startup” here means
fresh application/browser state, not a verified cold disk/shader-cache run.
Idle views reuse the terrain visibility cache; walking changes the view and
has higher GPU cost. No offscreen throughput benchmark ran (`status=0`, zero
batch frames). Historical cached/offscreen FPS is excluded. The startup
budget helper intentionally reports one sample as `insufficient_samples`;
its default 1 ms diagnostic target is not this plan's product budget.

## Separate memory observations

Memory runs are instrumented and excluded from the visible timing table.
All table values use **MiB = 1,048,576 bytes**. Maxima are taken from concurrent
sample sums, never by adding independent per-process peaks. No memory domains
are additive on this shared-memory GPU.

### Native landscape

The native process exited itself after the requested screenshot, with
`child_exit_code=0` and `stop_reason=child_exited`; the 90 s safety deadline did
not stop it. There are 67 samples over 13.316 s, no history truncation and one
startup fdinfo disappearance. RSS and requested/resident GPU totals each
have 65 usable samples. No unidentified DRM client or traversal cap occurred.

| Native domain | Startup sampled peak | Observed steady range | Whole-run sampled peak |
|---|---:|---:|---:|
| Process RSS | 426.723 | 422.281–422.281 | 426.723 |
| DRM requested | 1,149.965 | 1,026.168–1,030.238 | 1,149.965 |
| DRM resident | 1,149.965 | 1,026.168–1,030.238 | 1,149.965 |

For this descriptive split, startup is elapsed <3.017 s and steady state is
3.017–12.966 s, aligned approximately to application log markers; process
launch and the log clock have a small unmeasured offset. There are 50 steady
samples. RSS peaked at 1.210 s and GPU at 1.817 s, showing a startup transient
above the later steady values. Its cause is not attributed to staging without
resource-level evidence. The separate `/proc` VmHWM record is 426.723 MiB;
`wait4` reports 424.590 MiB. These accounting observations differ and are
retained separately rather than reconciled or summed. Native allocator live
bytes were not measured; RSS is process residency, not `malloc` allocation.

### Browser landscape, movement and playground

The fresh browser process tree was observed for 95.258 s at a requested
200 ms interval. It produced 452 process samples; 340 have complete observed
DRM requested/resident totals and 338 have complete observed RSS sums.
There were 153 `FileNotFoundError` observations across 112 samples: 150 fdinfo
paths and three process paths disappeared while scanning. These are explicit
race/coverage gaps, not zero usage or evidence of a leak. No permission denial,
unidentified GPU client, cap overflow or dropped history was recorded.

The CDP sampler retained 441 observations with no reported CDP errors. WASM
values are unavailable before module initialization. The complete browser
memory status remains **partial**, independently of the successful startup.

| Browser phase | Process samples / complete DRM | RSS observed peak | DRM requested/resident observed peak | JS used peak | WASM allocated peak |
|---|---:|---:|---:|---:|---:|
| Browser bootstrap | 3 / 3 | 717.512 | 107.242 / 107.242 | 0.443 | unavailable |
| Fresh page startup | 38 / 30 | 2,501.906 | 843.625 / 843.625 | 2.574 | 404.187 |
| Extra 2 s startup hold | 10 / 5 | 2,449.781 | 799.082 / 799.082 | 2.786 | 404.187 |
| World journey | 303 / 230 | 2,511.137 | 786.277 / 786.277 | 4.440 | 404.187 |
| Playground journey | 98 / 72 | 2,348.371 | 1,089.293 / 1,089.293 | 7.813 | 404.217 |

These per-domain maxima occur at different moments and must not be summed.
The largest complete observed browser RSS sum was 2,511.137 MiB at 13.562 s;
a partial scan's known subtotal reached 2,562.094 MiB during startup. Therefore
the smaller complete-scan peak is **not an upper bound on actual usage**.
The GPU peak was 1,089.293 MiB at 94.412 s during the playground journey.
The final complete sample was 2,343.172 MiB RSS and 1,075.469 MiB GPU.

WASM linear-memory capacity stayed at 512 MiB; `mallinfo().uordblks` reached
404.217 MiB. CDP JS used heap peaked at 7.813 MiB, allocated JS heap at
10.750 MiB, embedder heap at 37.588 MiB and ArrayBuffer/external-string backing
storage at 110.200 MiB during startup. They describe distinct, potentially
overlapping accounting views. Instrumented DOM/telemetry/journey activity is
included; the upward JS/embedder trace is not a leak determination.

Process scanning itself took browser p50/p95/max 10.94/15.45/20.86 ms and
native 1.66/2.04/2.74 ms. The process collector sleeps after each scan, so its
actual spacing exceeds 200 ms. Main-thread work can delay CDP samples further.
No exact between-sample high-water mark is claimed.

### Meaning and missing categories

DRM accounting is deduplicated by device and client ID. `drm-total` records
requested buffer storage; resident means backing storage is instantiated.
The AMD `drm-memory` alias is not added again. Shared buffer objects can still
appear under different clients, so totals are not unique physical usage.
These meanings follow the [kernel DRM specification](https://dri.freedesktop.org/docs/drm/gpu/drm-usage-stats.html).

The observed driver exported requested, resident, shared and purgeable regions.
`active`, staging and pending-retirement bytes are **unavailable**, not zero.
The total can include allocations while the driver attributes them to a
client, but it does not certify hidden driver overhead or every resource
retained after application release. Internal physics estimates and cumulative
`createBuffer` counters are not substituted for live global GPU memory.

RSS is the scoped native process or newly launched Chrome descendant tree;
shared mappings can be counted by several processes. VmHWM/wait4 are separate
high-water observations. CDP's [heap API](https://chromedevtools.github.io/devtools-protocol/tot/Runtime/#method-getHeapUsage)
reports isolate-wide values, including optional embedder/backing categories.
The WASM export reports allocator-used bytes separately from linear capacity.
No renderer/C++/vendor allocation hooks were added for these observations.

## Reproduction and retained failures

Run from the repository root on the recorded visible hardware environment.
Use fresh result paths so the reviewed evidence is preserved. The frozen
visible runner expects its three adjacent dependencies, already copied into
`snapshot/`. Node 22.23.1 appears in the retained initial harness failure.
The prepared package and native builds follow the [BOOT-02 recipes](../BOOT-02/report.md).

```bash
VOXY_SMOKE_GPU=gaming VOXY_SMOKE_WIDTH=1920 VOXY_SMOKE_HEIGHT=1080 \
VOXY_SMOKE_REPORT=/tmp/boot03-repeat-visible.json \
VOXY_SMOKE_SCREENSHOT=/tmp/boot03-repeat-visible.png \
VOXY_SMOKE_JOURNEY=/tmp/boot03-repeat-world \
VOXY_SMOKE_PLAYGROUND=/tmp/boot03-repeat-playground \
node docs/validation/salvage/BOOT-03/snapshot/voxys-boot03-visible-baseline.mjs \
  /tmp/voxys-boot02-cmake-fresh-web default
```

The actual retained visible report takes precedence over a recalled command:
it records `default` and `/`, not an explicit `lego-world` query. That default
loads the same landscape config. The memory run explicitly used `lego-world`.
For a **new comparable memory capture**, the current instrumented runner can
be invoked as below; record its fresh source/package hashes before running.
This command does not recreate a missing historical memory-runner snapshot.

```bash
VOXY_SMOKE_MEMORY=1 VOXY_SMOKE_MEMORY_INTERVAL_MS=200 \
VOXY_SMOKE_GPU=gaming VOXY_SMOKE_WIDTH=1920 VOXY_SMOKE_HEIGHT=1080 \
VOXY_SMOKE_REPORT=/tmp/boot03-repeat-browser-memory.json \
VOXY_SMOKE_SCREENSHOT=/tmp/boot03-repeat-browser-memory.png \
VOXY_SMOKE_JOURNEY=/tmp/boot03-repeat-memory-world \
VOXY_SMOKE_PLAYGROUND=/tmp/boot03-repeat-memory-playground \
node scripts/smoke_integrated_wasm.mjs /tmp/voxys-boot02-cmake-fresh-web lego-world
```

The original native command was the following, with evidence output paths.
Use new paths for a rerun. The old binary is no longer at this build path;
hash a newly built executable and label any rerun as a new baseline.

```bash
nix-shell --run 'python3 scripts/measure_native_memory.py --output docs/validation/salvage/BOOT-03/native-world-memory.json --duration 90 -- build-salvage-native/bin/voxy_native --config lego_world.cfg --width 1536 --height 864 --vsync --screenshot /home/modkin/workspace/schneiderlo/voxys/docs/validation/salvage/BOOT-03/native-world-memory.png --screenshot-frames 600'
```

Pure collector verification is `python3 scripts/test_salvage_memory_probe.py -v`
(13 passing tests at implementation handoff), plus Python compilation and
`node --check scripts/smoke_integrated_wasm.mjs`. Non-GPU launcher checks
separately confirmed natural exit, explicitly labeled deadline termination,
and stopping the observer without stopping its subject. No GPU rerun was
performed while writing this report.

The [initial failed run](visible-world-harness-failure.json) and
[its log](visible-world-harness-failure.log) are preserved. The image checker
was absent beside the temporary runner, so Python exited 2 and the runner
reported a misleading landscape assertion. Copying the dependency beside
the runner resolved that harness failure; the following visible run passed.

The successful visible run still contains two event-ring-full and two
telemetry-ring-full GPU physics warnings. No uncaptured GPU error or device
loss was recorded. These warnings are limitations of baseline diagnostic
coverage; a later gameplay/event acceptance cannot silently ignore them.
Native libdecor/GTK startup warnings and Chrome presentation/background-service
warnings remain in their logs rather than being removed from evidence.

Before claiming stronger results, capture longer per-scene GPU distributions,
actual Chrome userspace-driver identity and power/thermal state, fresh runner
and executable hashes, and native movement/frame telemetry. A tighter memory
collector may improve transient-file coverage; resource-level staging and
retirement accounting belongs with later lifecycle work. These are concrete
limits of this baseline, not evidence that the product targets have been met.
