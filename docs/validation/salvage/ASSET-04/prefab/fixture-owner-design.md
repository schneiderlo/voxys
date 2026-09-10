# Fixture-owned GPU residency and completion protocol

2026-09-08, `render_architecture`; reviewed and implemented by root. The bounded
owner exists in `src/render/salvage_asset_fixture.*`. Ten native component tests
pass, along with installed-registry integration and a real browser lifecycle
journey. See `../integration/report.md` for evidence and remaining acceptance
work. This design is not ASSET-04 or LOOK-01 acceptance.

## Implementation refinements and pinned-library limits

- Browser pushes Validation, OutOfMemory and Internal scopes. Native pushes
  only Validation and OutOfMemory: pinned wgpu-native v22.1.0.5 declares Internal
  in its header but aborts on it. The first real-GPU attempt reproduced that
  abort; `fixture-tests-attempt03.log` is retained. Its
  [pinned implementation](https://github.com/gfx-rs/wgpu-native/blob/v22.1.0.5/src/lib.rs)
  explicitly accepts only two filters. This is a backend capability limit,
  not permission to suppress validation errors.
- That same implementation makes `wgpuDeviceDestroy` a no-op. Two attempted
  native loss-injection tests produced no callback; attempts 05/06 are retained.
  `notifyDeviceLost()` tests application-thread forwarding and exceptional
  cleanup only. Actual browser `device.destroy()` now passes in
  `../browser-loss-attempt03/`; it confirms exceptional application shutdown,
  not a successful ordinary queue retirement.
- A discarded encoder can leave MeshPath queue writes pending. Discard flushes
  those writes with an empty submission and tracks a separate internal work
  horizon. It does not advance the submitted-frame counter. Ordinary retirement
  waits for this real queue work too.
- Generation and ticket counters survive shutdown/re-entry of the fixture
  object. A stale ticket from before Leave cannot acknowledge a new frame.
- Scene-view rebinding has its own scoped validation barrier. Encoding pauses
  until it finishes. A failed scene binding is terminal and requires shutdown;
  candidate validation failure still preserves the previous active generation.
- `MeshPath::setSceneTextures` stages both view bindings transactionally; temporary
  bind groups release on allocation failure. Failed texture upload/init paths
  release refs without Destroy because earlier queue writes may still exist.
- GPU requests are bounded to 16 MiB per owner and 48 MiB combined, including a
  conservative 64 KiB fixed-storage reservation. These are requested-storage
  limits, not claims about actual driver or process working set.

## Ownership and proposed API

`src/render/salvage_asset_fixture.hpp/.cpp` exposes a noncopyable
`SalvageAssetFixture` with an opaque implementation. It accepts only shared,
immutable `game::assets::CookedPartBundle` owners. It never parses files, grants
inventory, spawns physics or creates a MotoSession.

Implemented operations:

```text
init(device, queue, FixtureConfig, error) -> bool
beginCandidate(span<shared_ptr<const CookedPartBundle>>, error) -> bool
poll() -> FixtureStatus
publishCandidate(error) -> bool
setSceneViews(environmentView, rayDepthView, error) -> bool
encode(encoder, colorView, depthView, span<FixturePlacement>, FrameView,
       output frameTicket, error) -> bool
submitted(frameTicket, error) -> bool
discarded(frameTicket, error) -> bool
resetInstances(error) -> bool
requestLeave(error) -> bool
shutdown() -> void
stats() -> FixtureStats
```

`FixturePlacement` contains bundle index, stable LOD ID, camera-relative double
root matrix and exact `GridTransform`. A `FrameView` supplies view/projection,
camera-local position, lighting, extent and terrain-depth selection. The root
application selects sidecar LODs by their explicit screen thresholds, then joins
by stable ID. Both sidecar and bundle arrays are ID-sorted, not threshold-sorted.
`encode` uses `placeRigidPrefab` and submits its
complete bounded results through the fixture's own MeshPath.

`FixtureConfig` contains immutable negotiated formats/shader path plus resource
ceilings. The path always selects CW for admitted glTF under the actual LH camera.
Initial instance capacity equals the 512 expanded-draw cap to avoid per-frame
buffer growth. The legacy MeshPath and its CCW default remain independent.

Only one active owner, one candidate and one retiring owner exist. An owner holds
its bundles, unique `(visual ContentKey, exact VMESH digest)` uploads, LOD-to-GPU
mapping, MeshPath and separate completion state. Repeated references to the same
visual key/digest upload once; the same key with conflicting bytes rejects.
Different keys with matching bytes are not promised to deduplicate. Initial hard
caps: eight bundles, 24 unique LOD assets, 32 placements, 256 mesh-node instances
and 512 expanded draws. Resource admission sums each unique asset's verified
GPU counts plus fixed instance/uniform/fallback storage, and checks both per-owner
and active+candidate+retiring ceilings before GPU allocation. CPU snapshot and
decoded counts remain separate from requested GPU storage/driver overhead.

`FixtureStatus` distinguishes idle, active, candidate validating, candidate ready,
candidate rejected, leaving and fatal device/queue failure. Statistics expose
generation IDs, active/candidate/retiring requested bytes, unique uploads,
last submitted/completed frame serials, pending validation/fence callbacks and
last encoded/submitted draw counts. A successful encode is not reported as a
completed frame.

## Candidate creation and asynchronous validation

1. Reject preparation if an encoded-frame ticket is unresolved, another candidate
   exists, a Leave is draining, or admitting the candidate would exceed bounds.
2. Complete CPU mapping/count/identity checks before allocating GPU resources.
3. Push Validation, OutOfMemory and Internal error scopes on the device. Upload
   all unique LODs into a new MeshPath, including retained environment/ray-depth
   bindings. The operation is synchronous between matching push/pop calls, so it
   does not leave an error scope open across an application frame or async wait.
4. Pop all scopes with stable callback payloads. Allocate those payloads before
   pushing scopes, and use guards so exceptions cannot leave scopes unbalanced.
   Pipeline/resource handles being nonnull does not establish successful async
   validation. Any pop failure or captured error rejects the candidate.
5. Explicitly submit an empty command list to flush queued upload writes, then
   register a queue-completion callback for the upload. This candidate can finish
   readiness/cleanup without ever encoding a scene frame.
6. `poll()` on the application thread observes callbacks. Publish is permitted
   only after all scopes succeed and the upload fence completes successfully.
   A failure keeps the previous active owner and its mappings intact. A failed
   candidate retains its own resources until its upload completion, or follows
   the explicit device-loss/final-shutdown release path.

Scope and queue callback payloads own only shared atomic completion records,
never `this`, MeshPath, bundles or GPU handles. They record bounded status/error
information and dispose only CPU callback payloads. Resource teardown happens in
`poll()`/shutdown on the application thread.

The portable shims use native `wgpuDevicePopErrorScope`/queue callbacks and
Emscripten Dawn callback-info structs with `AllowSpontaneous`, as existing browser
queue pacing/readback does. Spontaneous callbacks may be reentrant; WebGPU API
calls, including destructors that release GPU handles, must not run inside them.
[WebGPU C asynchronous-operation contract](https://webgpu-native.github.io/webgpu-headers/Asynchronous-Operations.html).

## Encode versus submit

There is at most one unresolved encoded-frame ticket. The serial is monotonically
increasing and never reused/wrapped. It records the owner generation that was
actually encoded. No publication, Leave transition, view rebind or second encode
may proceed while it is unresolved.

The Application calls one of these **after the actual corresponding action**:

- `submitted(ticket)` after `wgpuQueueSubmit` has accepted the finished command
  buffer. Only here does the owner's last-submitted serial advance.
- `discarded(ticket)` after releasing the failed/abandoned encoder or unsubmitted
  command buffer so it will never be submitted. This does not advance submission.

Wrong, stale or duplicate acknowledgments reject. A failed `wgpuCommandEncoderFinish`
therefore needs the discard path in Application's early return. A root-owned
submission helper/guard should make these acknowledgments hard to omit.

One queue callback per owner may be outstanding. It snapshots the highest serial
submitted when registered. Later frames do not allocate an unbounded stream of
callbacks: when that callback completes, `poll()` registers another fence only
if newer work still needs tracking. Once an owner stops drawing, its final fence
covers its final submitted serial. Completion may conservatively include other
work on the shared queue; it must never infer completion from elapsed frames.

## Publish, Reset, Leave and shutdown

Publication only happens at a boundary with no unresolved ticket. A ready
candidate becomes active; the previous active owner either releases immediately
if its last submitted work is already complete, or occupies the single retiring
slot. If that slot is already occupied, publication stays pending rather than
adding an unbounded generation list. The original active owner remains usable.

Reset clears only CPU instance selection and restores root-owned camera/controls.
It retains the same immutable residency and upload generation. It cannot affect
an already encoded unresolved frame.

Leave stops future encodes, cancels candidate publication and drains both active
and existing retiring work. If the retiring slot is busy, the current active
owner stays quiescent in its active slot until that slot can move; there are
still at most three owners. Candidate-never-created, candidate-never-drawn and
empty-frame cases have explicit no-frame branches and do not wait for fictitious
submissions. Leave is acknowledged drained only when all fixture GPU owners have
retired. The application keeps pumping its normal GPU/event loop while waiting.

Queue failure/device loss is a surfaced terminal status, not an endless pending
Leave. The application then performs its explicit fatal/shutdown path. Pending
CPU callback records remain independently valid if their callbacks arrive later.

For exceptional final destruction, request the narrow MeshPath addition
`releaseHandles()` that releases external refs without explicitly destroying
textures. A generation destructor uses it whenever completed retirement was not
established, including interrupted initialization and device loss. Ordinary Leave
still waits completion and uses normal teardown. This avoids GPU calls inside
callbacks and avoids destroying an object referenced by an unsubmitted command
buffer during exceptional unwind. WebGPU implementations retain internal object
dependencies; external ref release is distinct from texture destruction.
[WebGPU C ownership contract](https://github.com/webgpu-native/webgpu-headers/blob/main/doc/articles/Ownership.md).
The existing benchmark target replacement uses the same release-only principle.

The module retains device/queue handles while alive, plus explicit refs for its
borrowed scene views. Final shutdown releases those refs on the caller thread
after dropping fixture owners; callbacks retain no device/queue refs and cannot
keep the device alive indefinitely. `shutdown()` does not claim a normal Leave
completion when it uses this exceptional release path.

## Focused acceptance

- CPU boundary cases: empty/null/conflicting bundles, duplicate stable assets,
  owner/combined resource ceilings, missing LODs, invalid placements and draw
  expansion limits. Failures retain active generation/counts.
- Actual GPU candidate validation: valid candidate waits for scope/fence results;
  a deliberately invalid GPU pipeline/resource setting rejects through real
  error scope callbacks and preserves the previous active owner.
- Encode/submit lifecycle: refuse publication/Leave/rebind while a ticket is
  unresolved; distinguish actual submit from actual discard; reject duplicate
  acknowledgments; delay ordinary retirement until its real queue completion.
- Repeat Reset/replace/Leave/re-entry with active+candidate+retiring pressure;
  residency is bounded, shared parts upload once and no stale instance survives.
- Destroy a candidate that never drew and destroy the module with pending CPU
  callbacks; later callbacks must touch only their stable completion records.
  Test device loss/final shutdown separately from successful queue retirement.
- Root's shared builds and actual browser fixture must compile/exercise both
  callback signatures. GPU runs require a newly scheduled exclusive window.

This owner is intentionally local to the static asset fixture. It does not supply
global streaming, world persistence, physical authority or LOOK-01 water/HDR work.
