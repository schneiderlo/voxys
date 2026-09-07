# BOOT-04 — product defaults, ownership and validation matrix

Date: 2026-09-07. Owner: root. Source baseline: `7563f61fd536df7de5d209ff35d3cd2099ebcbb6`, branch `codex/salvage-implementation`.

Status: complete; independently reviewed by lego_gameplay on 2026-09-07. The reviewer reproduced the listed host and Chrome identity and confirmed mandatory/deferred scope, ownership and unavailable-resource disposition against the entire plan. This is an implementation charter, not completed game, hardware-performance, staffing, funding or release evidence.

## Product defaults adopted for implementation

The active goal is the full `GAME_IMPLEMENTATION_TODO.md`, through G14. Every task needs its stated evidence; each successful gate requires a commit with the repository checks enabled. The root coordinates task ownership and commits. No scope reduction is made here.

| Requirement | Working implementation contract | Completion gate |
|---|---|---|
| Core loop | Build a machine, explore, salvage, return loaded, improve, repeat | G06 first two jobs; G12 campaign |
| Native launch | Windows and Linux; offline solo, creative, 1–4 host-authoritative co-op | G11, G12, G13, G14 |
| Browser | Desktop WebGPU demo; final cove, two jobs, 14 initial functional definitions | G08, G11, G13 |
| Demo co-op | Native host with native/browser guests using the same demo profile | NET-02, NET-07 |
| Compatibility | Compare selected profile's authoritative content; reject demo-only peers from campaign | WORLD-02, NET-02 |
| Progress | Host-owned world; separate demo, creative and campaign saves; only validated design data crosses | G04, MECH-08, NET-05, CONTENT-07 |
| Travel | One active region; separate jobs within it; checkpointed whole-party harbor transitions | WORLD-04, NET-07 |
| Machines | Boats, powered cranes, cutting/repair, amphibious haulers | G07, CONTENT-03, G12 |
| Avatar | Original utility robot; boarding, moving-deck walking, swimming/rescue | ACT-01–03, G08 |
| Campaign | Four authored regions, 50–70 functional definitions, measured 12–18 hour first playthrough | G12 |
| Creative | Separate free-building world and validated local blueprint import/export | CONTENT-07 |
| Inputs | Keyboard/mouse and controller parity, remapping and accessibility settings | G08, QA-07 |
| Language | English, with localization-ready data and layout testing | QA-07 |
| Deferred platforms | Console, macOS, mobile, full browser campaign, browser hosting | Outside mandatory launch |
| Deferred systems | Aircraft, submarines, factories, general programming, combat/PvP, hunger/thirst, voxel excavation, public gallery, voice/chat, host migration | Require explicit future contract revision |

The working region identities are Harbor Reach, Cliffworks, Marsh Delta and Stormbreak. Names and branding are provisional; their gameplay roles remain mandatory. Reference art is optional and is not advertised as a game screenshot.

Starter restoration uses one durable entitlement with nonbankable loan parts; paid additions remain owned. Repeated legitimate rescues cannot create material. These rules are required even before multiplayer.

## Ownership

These are current execution responsibilities, not a claim that a production studio has been staffed.

| Workstream | Current responsible executor | Independent review / outstanding resource |
|---|---|---|
| Product contract, integration, gate ledger and commits | root (Codex) | Project owner retains product/release decisions |
| Gameplay, content schemas and mission integration | root; bounded work delegated to lego_gameplay | Another agent for code; actual participants for playability |
| Physics, persistence, protocol and numerical contracts | root; bounded work delegated to simulation_production | Separate code reviewer and measured hardware fixtures |
| Rendering, asset cooking and platform packaging | root; bounded work delegated to render_architecture | Separate code/visual reviewer; real target GPUs |
| Blender generation/material drafts | root or an explicitly assigned bounded asset worker | Technical checks plus actual art review; no staff artist currently assigned |
| UI, audio and authored campaign content | root coordinates implementation and generation | Human accessibility, audio and content review not provisioned |
| QA and release operations | root maintains evidence and failures | Windows/other GPU machines, human testers and distribution access not provisioned |

All three named agents are investigation/implementation workers in this goal, not human playtest participants. Before parallel edits the root records each task and owned paths in the plan. Full-time production staffing, contractor rates, service costs and a commercial release date remain unconfirmed; CONTENT-01 must establish them from measured throughput and available resources.

## Hardware matrix

The rows distinguish an observed host from required but unavailable validation. No new hardware is purchased or assumed by this report.

| Matrix ID | Platform / role | Hardware and software | Availability and next verification |
|---|---|---|---|
| LNX-890M | Linux native engineering reference | AMD Ryzen AI 9 HX 370 with Radeon 890M; 24 logical CPUs; RAM MemTotal 60,254,692 KiB; AMD PCI vendor/device 1002:150e; Ubuntu 26.04.1 LTS; kernel 7.0.0-31-generic; Wayland | Observed local host. BOOT-03 must establish actual GPU access/driver, display mode, power mode and visible captures |
| WEB-890M | Desktop WebGPU demo on the same reference | Same host; installed Chrome 152.0.7977.82 reported by read-only environment investigation | Fresh launch/adapter and presented resolution still to verify; do not count installed software as passing runtime evidence |
| WIN-REF | Windows native release validation | Exact machine/Windows build/driver not yet provisioned | Required before G13. Record actual hardware when accessible; no assumed pass or invented configuration |
| LNX-INTEL | Intel GPU native/browser compatibility | Exact device/driver not yet provisioned | Required release coverage; QA-01 must bind an actual machine |
| GPU-NVIDIA | NVIDIA native/browser compatibility | Exact device/driver/OS not yet provisioned | Required release coverage; may share Windows coverage if an actual machine qualifies |
| MULTI-4 | Four real rendered clients with declared host/client budgets | Up to four actual participants/devices or processes, individually identified | Not yet run. Multiple processes on one host can test correctness but cannot replace independent-client performance evidence |

Baseline measurement target is 1920×1080, declared preset, fixed 60 Hz simulation and the frame-time/capacity budgets in the plan. Record actual framebuffer dimensions, compositor/display refresh, power/thermal settings, adapter/driver and visibility. The plan's 13.5 ms GPU allocation, 4 ms CPU p95 and presentation gates remain proposals to implement and measure, not achieved values.

The restricted command environment currently exposes DRM sysfs identity but not `/dev/dri`. A normal-host invocation can be used through the supported permissions workflow for real GPU tests. A software adapter result must be labeled as such.

## Evidence and review procedure

Fresh root observations used Python's `platform`, `/etc/os-release`, `/proc/cpuinfo`, `/proc/meminfo`, selected display environment variables and `/sys/class/drm/card1/device/{vendor,device}`. No credentials or unrelated private files were queried. Source scope was checked against the complete active plan and BOOT-01 report. Chrome version comes from the renderer investigator's actual executable-version query; a fresh browser run belongs to BOOT-03.

To reproduce the host identity without starting the game:

```bash
python3 - <<'PY'
from pathlib import Path
import os, platform
print(platform.platform())
print(Path('/etc/os-release').read_text())
print(next(line for line in Path('/proc/cpuinfo').read_text().splitlines()
           if line.startswith('model name')))
print('logical CPUs', os.cpu_count())
print(Path('/proc/meminfo').read_text().splitlines()[0])
print({key: os.environ.get(key) for key in
       ('DISPLAY', 'WAYLAND_DISPLAY', 'XDG_SESSION_TYPE')})
for device in Path('/sys/class/drm').glob('card[0-9]/device'):
    print(device, {key: (device / key).read_text().strip()
                   for key in ('vendor', 'device') if (device / key).exists()})
PY
```

Acceptance for BOOT-04 is that every mandatory feature/platform has an explicit contract, owner and validation disposition. Missing resources are recorded above. This task does not certify G13 hardware, human gates, funded staffing or publishing access. Independent review must check scope against the plan and ensure unavailable resources are not disguised as completion.
