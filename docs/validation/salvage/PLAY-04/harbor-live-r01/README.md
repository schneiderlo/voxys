# Playable powered harbor lift — native and browser component verified

This completes the scoped live harbor component of PLAY-04 / SAVE-04 under
D20/D21/D26/D27/D28. Parent tasks and full gates remain open. Baseline HEAD is
`7f28fabfabf3d63726f6cfa1001c3ce1ed557911`; preserve the authorized implementation
tree. No full-gate commit, final art, human fun test or performance acceptance
is claimed. No screenshots or image inspection were used.

## Playable behavior

After the generator delivery is durably saved, walk to the dock and press **K**
(or **Power harbor lift**). Installation moves that same banked generator onto
the outer edge of the existing pier, checks actual support/occupancy, admits the
fixed lift frame, and saves the expedition. The machinery appears and controls
unlock only after storage confirms publication. No second reward is paid.

Park the boat inside the frame. If the old delivered load pushed it sideways,
use the existing **R** Return action after installation to reposition it in the
cleared berth. From the dock, **F** requests four-line attachment. An unsafe
request waits up to 600 simulation ticks for fresh safe alignment; Stop, F,
Pause or walking away cancels it. Browser blur/cleanup also cancels it.
The same speed, uprightness, whole-boat and anchor-clearance limits apply.

Hold **Q** to raise or **Z** to lower; release to stop. Browser held buttons work
the same way, including release outside the button. **F** releases the slings.
Pause stops motors and joins actual cable/body observations. Save while
suspended, restart, Resume, lower and release. Release before opening the
workshop. Leave drains owned ropes, bodies and shapes.

The frame uses ten plain opaque beams and four straight cable segments. Detailed
machinery, drums, authored slings, controller support and final art remain open.
The LEGO terrain and its shared physical plate/stud surface are unchanged.

## Final actual-game checks

| Check | Evidence | Result |
| --- | --- | --- |
| Desktop install, lift, suspended process restart, lower, release, sail | `native-r12/summary.json` | 12 stages, passed |
| Browser exact previously failing pre-lift save | `browser-r12/summary.json` and `browser-report-r12.json` | 8 stages plus Leave, passed |
| Fresh browser construction, haul, delivery, installation, lift, suspended reload, lower, release, sail | `browser-r13/summary.json` and `browser-report-r13.json` | 30 stages plus Leave, passed; no uncaptured GPU errors |
| Affected native physics/cove/save regressions | `native-compliance-regressions.log` | 63 cases / 8 suites, passed; no skips |
| Render owner/lifetime regressions | Same log | 20 cases / 3 suites, passed |
| Impact and mechanical-energy comparison, with nonbinding speed ceiling | `compliance-impact-r03.log` | Passed |
| Save and preview UI controls/cancellation | `browser-ui-final-r02.log` | 13 named save cases and 19 preview cases, passed (local Node reporter groups the two files) |
| Optimized Bazel native build | `bazel-compliance-build-r02.log` | Passed |
| CMake native application and cove test build | `cmake-compliance-build-r01.log` | Passed |
| Final WASM application | `wasm-final-build.log`, `web-package-r08.json` | Passed; matching JS/WASM/data packaged with current web files |

**Desktop:** original real delivered world `a76a1221672cefbb385025e1f5f608e3`
is copied unchanged into a new private storage root. Its paid 11-part boat is
1,035 kg, paid part ID 35, with 96 material throughout. Installation saves at
tick 2278; the banked 420 kg generator is at [7, 1.925, -47]. Four lines raise
the boat root from y=-0.225986 to y=3.33651, stopping at 4.18 m target lengths.
Suspended save tick 3029 restarts at 3030. All four lengths and parked cargo
survive. Lowering stays intact; released sailing reaches 2.15065 m/s.

**Fresh browser:** world `4348871c7104ceec16d786acfcacf075`. The ordinary workshop
removes the loan cradle, buys a 12-material beam, moves the winch and launches
the 1,035 kg rig. Actual controls hook/lift the generator, steer the loaded
return, bank it, reload and refuse a second payout. Installation saves at tick
1281. After Return and attachment, root y rises from -0.477322 to 3.32458 m.
Suspended tick 1980 reloads at 1981, with <1 mm root displacement and unchanged
four 4.18 m targets. Lowering pays out to 9.68825 m; all four lines release.
Actual boarding/helm controls sail at 2.34778 m/s. Parts, material and parked
cargo remain unchanged. The driver then checks a drained Leave.

These are cove-relative heights in moving waves, not a flat-water benchmark,
fixed-tick water certification or arbitrary-craft/6-ton service rating.

## Runtime, durability and geometry

- `CoveHarborLift::prepareStructure` defines ten immutable lattice boxes around
  the delivery center. Opaque rendering and player collision use those bounds.
  The static body never follows the boat; observed identity, pose and zero
  velocity are checked. The separate rig derives four underside anchors from
  two actual sealed pontoon parts. An incompatible refit keeps the fixed frame.
- Explicit installation checks generator support across a bounded footprint on
  existing terrain/deck cells. Candidate positions are x=7, z=-47/-45/-43 in the
  current scene. Nonfinite, underwater, steep or occupied positions are refused.
  It preserves the inner walking lane. No terrain is flattened. Numeric sampling
  showed the pier is over deep water, so a shore-only query was inappropriate.
- The old static cargo body must be observed retired and its replacement admitted.
  Gantry upload can finish asynchronously: finish the cargo's neutral tick first,
  then admit the frame through another neutral tick. Scheduling briefly reopens
  and grants that final tick within the same update, before ordinary input.
- `CoveHarborRuntime` owns shape/body/rope lifetimes, actual lengths, broken bits,
  motor changes, queued attachment, restore, Pause and retirement. A same-tick
  joined observation/event frontier is required; commands alone are not proof.
  Destroyed GPU attachment slots become dead/zero-generation before handles are
  forgotten. Partial attachment creation retires any created subset.
- The storage host must acknowledge the exact complete archive SHA before the
  upgrade unlocks. Linux uses the exclusive mirrored/fsynced file worker;
  browser uses exclusive strict IndexedDB publication. Failed prepublication
  writes stay paused for explicit retry. Late/invalid acknowledgment cannot
  announce success. The completed cargo/job state prevents another +60 payout.
- Profile 0 preserves v1 bytes and absence of installed collision. Explicit
  profile 1 uses the existing v2 extension for lengths and break bits. Loading
  validates codec, content, ownership, boat, player and rig before physical
  activation. Fresh neutral constraints use derived anchors/material/forces;
  no serialized GPU handle is trusted. Broken lines stay broken. The parked
  cargo pose and its extra player obstacle restore from the actual archive.
  Older archives never silently relocate a generator.
- Presentation reuses the opaque helper mesh and existing HDR/depth path within
  the 512-draw budget. It adds no textures or model owner. Four displayed cable
  endpoints come from the actual observed boat and fixed frame.

See `docs/salvage-cove-save-format.md` for archive offsets and host action details.

## Rope correction and compliant slings

The old single sweep could amplify corrections from four lines sharing a body.
The solver now performs eight bounded serial sequential-impulse sweeps in one
GPU invocation. Motors advance once per tick. Accumulated tension may decrease
on a later sweep; total applied impulse remains force-limited. Break checks run
at the last sweep, removing that line's accumulated impulse before integration
while retaining its measured overload. This does not claim parallel island
scaling or a general articulated crane.

A second actual fault was a hard-sling snatch load in moving waves. Distance
attachments now accept optional `springCompliance` in m/N. Zero keeps the
hard-rope path, including ordinary towing. Harbor slings use **5e-6 m/N**
(**200 kN/m** axial stiffness). The solver derives implicit spring/damper
coefficients with critical damping from endpoint effective mass. Static
extension is force × compliance. Actual endpoint separation supplies the
stretch; no pose, mass, gravity, water or force evidence is substituted.

The **30 kN applied-force cap** and **45 kN breaking threshold** stay unchanged.
Raising remains 0.5 m/s and lowering 0.25 m/s. The internal GPU command grows
from 80 to 96 bytes with an explicit material vector; allocations/limits use
sizeof. The live/readback/event record remains 128 bytes, with compliance in a
formerly reserved word. Admission rejects nonfinite, negative or out-of-range
material values. Fresh and restored harbor lines derive the same material;
SVCE/SVSC/SVSG layouts remain unchanged.

The impact regression compares identical 300 kg, 3 m/s loads. The hard rope
breaks above 45 kN; the compliant sling stays intact within the 30 kN cap and
shows actual stretch. Mechanical energy falls by more than 100 J on the first
impact and stays below the initial energy plus a 25 J numerical tolerance over
the subsequent checks. A 30 m/s ceiling is beyond the available pendulum energy,
so velocity clipping cannot mask this result. Existing hard-rope break, force,
winch, atomic admission and saved-state regressions also pass.

## Retained failures and exact reproduction

Failed runs are retained and never counted as passes:

- Native r01–r03: attempted a final tick while already paused; fixed scheduling.
- Native r04: startup recognized only v1; bounded header preflight now accepts v2.
- Native r05: coupled single-sweep lowering broke four lines; eight sweeps fix it.
- Native r06: destroyed-slot generation check preceded retirement; ordering fixed.
- Native r08: shore-only parking refused underwater candidates; query real deck.
- Native r09: gantry admitted behind a draining cargo tick; join admissions in order.
- Native r07/r10/r11 are historical passes before final compliance.
- Browser r01/r02/r05: pose/button timing; an attachment request now waits for a
  fresh safe observation. Limits are unchanged.
- Browser r03: lift worked from a wave crest; the test wrongly required 2 m travel.
- Browser r04: a high banked generator obstructed the berth; explicit pier transfer.
- Browser r06: queued attachment worked, but the held-button helper did not wait
  for the visible enabled button; corrected.
- Browser r07: legitimately off-center boat timed out. Use shipped Return after
  installation; do not silently move the boat or weaken alignment checks.
- Browser r08/r10: hard slings broke during raising. r10 measured 49,816.6 N on
  line 4 at tick 1404 and 46,331.3 N on line 2 at tick 2349, with ~12–15 mm
  excess length. r11 reproduces the first overload from the real saved setup.
- Browser r09: Chrome could not start because a long TMPDIR exceeded its Unix
  socket path limit. Use default /tmp for Chrome; workspace TMPDIR for Emscripten.
- Early compiler-warning failures remain in their logs; warnings were corrected,
  not disabled. Existing WASM warnings remain visible in build output.

Frozen pre-lift browser world: `d29b3950f727db3cfbc79490c752187f`, saved tick 1370,
water time 22.8913. Source profile:
`build-harbor-live-13t9ivnw/browser-prelift-profile-r01` (its file manifest is here).
Clone it for each trial; retain the baseline unchanged. `browser-r11/published-slot/`
contains exact current/mirror SVSG bytes exported in a read-only IndexedDB
transaction: generation 5, 10,462-byte SVCE payload SHA-256
`37c3bf0a3b44b9e057947a983408a3c9085f691e915e1324a0247391e38e276f`.
The same published envelopes are portable to the native host; no checkpoint or
success state was synthesized. The final compliant replay is browser r12:
root y=-0.445278 → 3.32453, suspended tick 1977 → 1978, intact lowering/release,
2.32361 m/s sailing and drained Leave.

## Reproduction

Use the project Nix environment. Preserve prior outputs; choose new directories.

```sh
cmake --build build-native-save-host --target voxy_native cove_player_tests -j8
bazel build -c opt //:voxy_native //tests:voxy_tests //tests:cove_player //tests:salvage_asset_fixture
bazel-bin/tests/voxy_tests --gtest_filter='CoveMovement.*:CoveSave.*:*Attachment*:*Winch*'
bazel-bin/tests/salvage_asset_fixture
node --test scripts/test_cove_saves.mjs scripts/test_salvage_preview.mjs
python3 scripts/validate_native_cove_harbor.py \
  --binary build-native-save-host/bin/voxy_native \
  --source-slot /absolute/real-delivered-slot \
  --storage-root /absolute/new-private-root --output /absolute/new-native-evidence
```

Build WASM with `/tmp/voxys-emsdk/.emscripten` and a workspace TMPDIR, then package
current web files and matching `voxy_wasm.{js,wasm,data}`. Final package is
`build-harbor-live-13t9ivnw/web-r08`; its hashes are in `web-package-r08.json`.

```sh
VOXY_TEST_CHROME=/usr/bin/google-chrome \
VOXY_SMOKE_GPU=hardware VOXY_SMOKE_NO_SCREENSHOT=1 \
VOXY_SMOKE_COVE_HARBOR=/absolute/new-browser-evidence \
VOXY_SMOKE_REPORT=/absolute/new-browser-report.json \
node scripts/smoke_integrated_wasm.mjs /absolute/matching-web-package salvage-cove
```

For focused replay, clone the frozen profile and additionally set
`VOXY_SMOKE_PROFILE=/absolute/clone`, `VOXY_SMOKE_KEEP_PROFILE=1`,
`VOXY_SMOKE_RESUME_WORLD=d29b3950f727db3cfbc79490c752187f`, and
`VOXY_SMOKE_PORT=38210`. Keep that port: IndexedDB belongs to its origin. Only
runner-marked isolated profiles are accepted. Ordinary runs create/delete their
own fresh profiles. Retained profiles are listed in final reports.

Local playable preview: `http://127.0.0.1:38203/?experience=salvage-cove&telemetry=0`,
served from the verified web-r08 package. It starts a fresh world in that browser
origin; the lift unlocks after ordinary generator delivery. Its storage is
separate from prior preview ports and the isolated validation profiles.

## Next work

Continue PLAY-05 rescue and protected recovery, then PLAY-06's second job.
Keep full PLAY-03/04, SAVE-04 and G04/G06 open for their remaining prerequisites:
controller use, latching, rescue/second job, broader craft/fault coverage, final
art, human fun checks, fixed-tick water and visible performance. This component
passes one real compatible 1,035 kg craft on Linux and Chrome/WebGPU. It does
not certify the 6-ton admission ceiling, Windows, general crane mechanics or
mid-publication/power-loss recovery. Commit only when a full gate and its
required checks pass.
