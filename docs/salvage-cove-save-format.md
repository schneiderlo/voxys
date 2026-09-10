# Cove expedition archive (live SVCE v1–v4; two-job v5 preparation)

This format joins an existing SVSC logical session checkpoint to physical cove
state. It is implemented in `src/game/expedition/cove_save.*`. It does not by
itself activate a restored world or acknowledge a durable save. The outer
native/browser storage envelope remains SVSG v1.

SVCE v1 remains byte-identical for existing worlds. V2 explicitly records
installation of the harbor lift and its four suspension lines. Application
startup accepts schemas 1–4, then fully validates the selected archive before
creating a replacement owner. V2 restores the fixed body and every unbroken
line with neutral motors, after publishing recovered ownership. Codec acceptance
alone is not evidence of a durable save or completed physical restoration.

The live host now captures schema 4, including ordinary single-root boats.
Restore preparation reconstructs every accepted root, its authored-origin
motion, the rider's section and the winch's section. Application startup owns
and admits the complete set before its joined neutral restoration tick.
Native purchases, disk saves and two restarts pass; an actual historical v1
save also loads, upgrades through F10 and restarts as v4 without altering its
source. Browser and full fragment acceptance are recorded in
`validation/salvage/MECH-05/root-resume-r01/README.md`.

Live cutting, all-section Rescue and protected rebuilding now pass the bounded
native/browser journeys in `validation/salvage/MECH-05/live-cut-r01/README.md`.
General impact-driven fracture, arbitrary joints and broader fault coverage
remain separate requirements.

## Two independent mission loads (schema 5 preparation)

The shared codec now has an explicit two-job profile. The live application still
creates one-job contexts and captures v4. Its restore preparation refuses a v5
two-load archive until it can own and restore **both** physical bodies, ropes
and render mappings. No current world is silently migrated or replaced.

V5 preserves the entire v4 prefix and appends this table **after the last boat
root**, before the current/parent logical checkpoint lengths:

| Field | Encoding |
|---|---|
| Additional cargo count | u32; exactly 1 in this profile |
| Cargo identity | Durable ID, 24 bytes |
| Job identity | Durable ID, 24 bytes |
| Installed cargo definition | ContentKey, 28 bytes |
| Authored-root motion | Existing motion encoding, 64 bytes |
| Cargo state | u8: Loose / Towed / BrokenTow / Banked |
| Tow winch part | Durable ID, 24 bytes; zero without a retained tow |
| Rope length | f32, 4 bytes; zero without a retained tow |

Each additional load is 169 bytes. The table adds 173 bytes and fits the
existing 4096-byte physical allowance even with all 32 boat roots. V5 requires
the complete root table. The original generator stays in the base fields;
selecting another target must never overwrite it. The second load stays in its
table slot after banking, with zero velocities and a completed logical job.
The logical checkpoint removes only the cargo that was delivered and retains
both job receipts. Its existing serialization and exactly-once transactions
already support two jobs; no SVSC or SVSG version change is needed.

`CoveSaveContext::additionalCargo` supplies the expected job/cargo IDs and full
definition from trusted installed mission setup. The reader never derives that
context from imported bytes. An empty context admits only the old one-job
profile; a one-entry context requires both loads, both distinct definitions and
both jobs. Missing or extra records, swapped roles, incorrect mass/value,
unowned cargo, a banked moving body and two retained lines on one winch refuse
the complete archive without changing the caller's output.

The second job may be Available before the generator is recovered. Accepted or
Completed requires the generator's completed/banked state and installed harbor
profile 1. This validates the saved progression; the future live command
boundary must enforce the same prerequisite **before accepting** the job.
It is not a substitute for command authorization or proof of real hauling.

V1–v4 encoding remains selected when there is no additional cargo. V5 cannot be
downgraded by omitting one load or rewriting the version. The first and second
cargo's rope/state and logical receipt are checked independently against the
accepted boat and catalog. Physical restore still needs actual geometry, tow-eye
and same-tick GPU evidence for every load before live activation.

## Starter rebuild and stored paid parts

The embedded logical checkpoint can use SVSC envelope 2 for registered starter
recipes and exact paid storage; this does not require another SVCE version.
See `salvage-session-save-format.md` for its field order and transfer rules.

Before any rebuilt boat is admitted, the cove compares its saved recipe with the
installed starter recipe and binds current grant IDs to the original scene slots
by recipe ordinal. Removed original IDs are not revived or guessed from position.
Paid stock is not a physical body. Withdrawal rejoins the normal boat compiler,
shape upload, body replacement and observed retirement boundary.

Rebuild starter / H is an explicit dock-workshop service. Ordinary Rescue / R
preserves the current accepted design. Rebuild and stored-part withdrawal pause
after actual physical/logical publication and require an exact whole-archive save
acknowledgment before Resume. Failure leaves the previous disk generation intact
and controls locked for explicit retry. Neither service banks cargo or grants a
mission reward. Existing saved design-library entries are not rewritten.

### Protected recovery designs (schema 3)

Before starter rebuild, stage a normalized design of the accepted boat alongside
the prepared replacement. Publish both together after successful physical
admission. Skip the original installed starter design and deduplicate exact
normalized bytes. Keep at most four distinct designs, each at most 128 KiB, with
at most 96 parts and 1,024 enabled welds. If a fifth distinct custom design would
be needed, reject the rebuild before replacing the boat. Never evict a backup
implicitly. Rebuilding the original starter again retains every previous backup.

These are SVBP designs: part definitions, placements, paint, module settings and
welds. They contain no durable instance IDs, health, ownership, entitlement,
inventory or physics. Canonical normalization sorts parts by placement,
definition, paint and settings; renumbers ordinals; remaps and sorts weld
endpoints. Replacing starter IDs does not make the same design a new backup.
Every archive backup must decode against the selected catalog and re-encode
to these exact normalized bytes. Duplicates, malformed bytes and excess counts
are rejected; the old output/save remains intact.

Schema 3 keeps the first 419 bytes of v1 (with schema 3 at offset 4), followed by
the 22-byte harbor section described below. Its profile may be 0 or 1. At offset
441 it stores a u32 recovery-design count (1–4), then for each design a u32 byte
length and its exact SVBP bytes. The current SVSC length/bytes, parent length/bytes
and outer SHA-256 follow the final design. With no backups or explicit root table, the codec emits the
original v1/v2 encoding based on harbor installation. Both legacy encodings stay
byte-identical. Maximum total input is now
`2*kMaximumSessionSaveBytes + 4096 + 4*(131072+4)` bytes; SVSG remains v1.

In the workshop, **K / Load recovered design** loads the selected design through
the normal editor. Launch reuses owned parts and prices missing parts normally.
The design can be exported through the existing blueprint library. **J / Next
recovery** cycles selection; the browser also has Previous. Selection is transient
and reload selects the newest retained design. **F / Remove recovery design**
explicitly removes only that backup, then pauses and saves before Resume. Removal
requires the editor to match the accepted boat, preventing an unlaunched design
from being closed and lost. It does not remove owned parts or exported blueprints.
Storage failure uses the same locked retry path as starter rebuild.

## Complete physical roots (schema 4)

A v4 archive retains the 419-byte base, the 22-byte harbor section and the
schema-3 recovery-design count/records. Its recovery count may be **0–4**;
v3 still requires 1–4. Immediately after those design records, v4 appends:

| Field | Encoding |
|---|---|
| Controlling helm part | Durable ID, 24 bytes |
| Rider's root key | Durable ID, 24 bytes; zero ashore |
| Root count | u32, 1–32 |
| Each root | Durable root key (24 bytes), authored-root motion (64 bytes) |
| Logical checkpoints | Existing current length/bytes, parent length/bytes |
| Whole-envelope checksum | Existing SHA-256, 32 bytes |

With no protected designs, the helm ID begins at 445, rider key at 469,
root count at 493 and first root at 497. Every root record is 88 bytes.
The root extension is 52 + 88 × count bytes. At 32 roots, all fixed fields,
root records, nested lengths and checksum still fit the existing 4096-byte
physical allowance, so the total archive ceiling does not increase.

A root key is `AssemblyMassRoot::key`: the least member part ID in the accepted
build/revision. Compile the accepted build's enabled weld graph to obtain the
expected root set and order. The table must contain every resulting root in
that order, including isolated nonfunctional parts. Missing, duplicated,
reordered, foreign and merely-member-but-not-root keys are invalid. The build,
its topology revision and the completed tick come from the existing logical
checkpoint/physical join; another tick or build cannot supply the root set.

Every record contains absolute authored-origin motion, not COM/principal
motion. Position must be finite and within ±1,000,000 metres per axis, rotation
must be canonical and each velocity component must be finite and within
±10,000 in the existing motion units. The host remains responsible for actual
same-tick evidence and representable GPU body frames before admission.

The controlling ID must name an accepted Helm part. The base `boatMotion`
field must exactly match that part's root-table motion; it is a compatibility
mirror, never a substitute for the table. A disabled helm may still identify
the primary root while the player is ashore, but cannot be saved in Helm mode.

Aboard player feet retain **authored build coordinates**, as in older archives.
The additional rider key selects which root transforms those coordinates into
the world. Walking/airborne riders may belong to another surviving root. Helm
mode requires the rider's root to contain the enabled controlling helm.
Ashore/swimming state requires a zero rider key. Geometry support/reach remains
an independent live-restore check; the codec does not create a player or body.

The tow winch's accepted part ID already selects its root; it need not be the
helm's root. Saved line limits and cargo ownership retain their existing rules.
The present profile-1 harbor lift supports one rigid craft, so a multi-root
archive requires detached harbor lines. Installed but detached harbor state is
valid. Supporting suspension across fragments requires explicit per-line root
ownership and corresponding runtime work; no line is silently discarded.

Writers select v4 whenever `CovePhysicalSave::boatRoots` is nonempty, including
an explicitly represented single root. An empty table selects the unchanged
v1–v3 encoding and requires zero new binding IDs and exactly one compiled
physical root. A fragmented logical checkpoint cannot be encoded or re-signed
as a legacy single-motion archive. Existing playable single-root archives keep
their original bytes. Logical SVSC/SVJB versions and outer SVSG v1 remain
independent; a physical v4 archive does not upgrade those formats implicitly.

Counts and remaining bytes are checked before root-vector allocation. Decode
validates the current/retired lineage and re-encodes the entire envelope for
canonical byte comparison. On failure, encode preserves its caller's output
and neither direction invokes the preparation adapter or allocates durable IDs.
The same rules apply after fresh-writer session recovery with an exact retired
parent. `tests/test_cove_save.cpp` includes root-loss/downgrade attacks, all
32 roots plus four backups, rider/control violations, retained cut bonds and
fixed byte/hash fixtures for portable native/WASM verification.

Live restoration uses the accepted build's disabled weld records to preserve
separation. Disabled welds are excluded only from the derived scene's active
connection list; they remain in the authoritative build. Ordinary workshop
Launch still rejects disconnected designs. Absent starter slots stay absent
from player collision rather than reappearing as fixed scenery.

Player state now binds an aboard rider to one durable root. The root's rigid
pose transforms the saved build-space feet. Only collision boxes owned by that
root support the rider, and helm controls require the helm's root. The tow
anchor is converted through the winch root's mass frame and attaches to that
root's runtime body. Reeling is available only while aboard the winch section.
These are local kinematic bindings; walking/jumping across independently
moving sections and inherited jump velocity remain character-system work.

## Ownership and identity

`CoveSaveContext` supplies the independently selected world/content identity,
boat/cargo/job IDs, full cargo definition and cove origin. Never adopt these
expected values from an unvalidated imported archive. A selected storage slot
must retain its own world identity; a missing known slot is not a fresh world.

The application derives content identity from the exact admitted installed
registry SHA-256, u32 cove profile 1, actual terrain width/height, f32 height/cell
scales, u32 LEGO-surface flag, and every u16 terrain sample. All numbers are
little-endian. The manifest key uses the 16-byte namespace `voxys-cove-save1`,
ID 1, version 1. Registry bytes bind its selected cooked bundle hashes, layout,
navigation and prototypes. Profile 1 additionally binds the present cove
catalog/compiler/physics/unit conventions; incompatible changes must bump it.
The digest remains attached to the original installed registry, before edits.
Runtime render slots, GPU handles and pointers are not saved.

## Wire layout

Unsigned integers are little-endian. Floats are IEEE f32/f64; nonfinite values
are forbidden. Writers canonicalize zero; readers reject alternate encodings
by re-encoding and comparing. Quaternions are canonical xyzw f32. IDs are a
16-byte world namespace plus u64 counter; content keys append u32 version.
No struct padding, host-native serialization or JavaScript Number u64s.

| Offset | Field |
|---|---|
| 0 | `SVCE` (4 bytes) |
| 4 | u32 schema = 1 |
| 8 | u64 completed simulation tick |
| 16 | cove origin, xyz f64 |
| 40 | boat ID |
| 64 | cargo ID |
| 88 | job ID |
| 112 | cargo definition key |
| 140 | boat root motion (64 bytes) |
| 204 | cargo root motion (64 bytes) |
| 268 | player feet xyz f64, vertical speed f64, tick/interactions u64 |
| 316 | u8 player mode, u8 aboard, yaw/pitch f32 |
| 326 | u32 water model, elapsed seconds f64 |
| 338 | 13 water f32 values, ordered below |
| 390 | u8 cargo state |
| 391 | winch part ID |
| 415 | rope target length f32 |
| 419 | u32 current SVSC length |
| 423 | current SVSC bytes; then u32 parent length and optional parent SVSC |
| end − 32 | SHA-256 of all preceding bytes |

Root motion is absolute authored-root xyz f64, canonical quaternion xyzw f32,
world-axis origin velocity xyz f32 and angular velocity xyz f32. This is not
COM/principal-frame motion. Reconstruct the mass frame through the existing
authored-body path when admitting GPU bodies.

Player feet use authored boat coordinates aboard and cove-local coordinates
otherwise. Modes: Walking=0, Airborne=1, Swimming=2, Helm=3. Aboard is a closed
0/1 bool. No pending input or fractional player accumulator is serialized.

Water floats: height, strength, significant wave height, direction radians,
choppiness, peak enhancement, wind alignment, animation speed, two patch
lengths, two cascade amplitudes, directional sine scale. Model 1 identifies the
existing two-cascade algorithm and its fixed seed recipe. It is not a general
weather seed or a claim that SIM-05 fixed-tick WaterField is complete.

Cargo states: Loose=0, Towed=1, BrokenTow=2, Banked=3. Towed/BrokenTow requires
an enabled accepted winch part and a rope length inside its authored limits.
Other states require a zero winch ID and zero rope length. Derive anchors,
strength and limits from installed content and the accepted build on load.
Banked requires a completed job, no logical cargo, and zero cargo velocities.
Its physical cargo is secured scenery; do not add it to boat mass or pay again.

The current and optional parent checkpoint are each bounded by the existing
4 MiB SVSC ceiling. The total bound includes the schema 3 backup allowance above.
There is exact byte exhaustion; counts/lengths are checked before allocation.
Encoding failures preserve the caller's old output. Corruption, unknown schema,
noncanonical representations and invalid physical/logical joins refuse.

## Harbor extension (schema 2)

The first 419 bytes keep the v1 field layout except the schema at offset 4.
Then v2 inserts exactly 22 bytes:

| Offset | Field |
|---|---|
| 419 | u32 harbor mechanism profile = 1 |
| 423 | u8 mode: Detached=0, Attached=1, Broken=2 |
| 424 | u8 broken-line bit mask, bits 0–3 only |
| 425 | four actual target lengths, f32, in canonical sling order |
| 441 | u32 current SVSC length |
| 445 | current SVSC bytes, then parent length/bytes as in v1 |
| end − 32 | SHA-256 of all preceding bytes |

Profile 0 is the in-memory default for v1: no installed hoist, Detached, zero
mask and zero lengths. With no recovery designs it writes v1. Installing profile 1
writes v2 when there are no recovery designs, including while detached or unpowered. This avoids silently adding
new collision geometry when loading an old v1 world. Profile 1 pins the fixed
structure and rig recipe in `cove_harbor_lift.*`; incompatible changes after
release must use another profile. Installation and its publication through the
owning host happen through the explicit dock action after durable banking.
Unknown profiles are rejected.

Detached requires four zero lengths and a zero broken mask. Attached requires
a zero broken mask; Broken requires at least one and at most four broken bits.
Every attached/broken length must be finite and within 2.5–12 m. Only Banked
cargo paired with its completed logical job permits Attached/Broken. Profile 1
may be installed but detached before banking; that does not grant power or
permission to operate. The installed craft's taller-part clearance may raise
its actual minimum cable length above the wire-level minimum. Live restoration
must independently compile and enforce that tighter limit.

The wire stores no motor speed, force setting, body/attachment handles or
untrusted anchors. Resolve canonical line order from the two admitted pontoon
parts, ordered left then right and front then rear, through the accepted build.
All four body/rope observations, broken bits and the session must join the
same paused tick. `CoveRestoreCandidate` compiles the fixed structure, installs
its player collision and verifies each saved length against the accepted boat's
rig. An unsupported attached craft refuses before physical admission. A detached
craft may be edited without moving or removing the fixed structure. The owned
`CoveHarborRuntime` uploads and admits the static body, binds fresh boat/gantry
handles to the derived line descriptors, and waits for the actual observations
and contiguous events. Saved broken lines remain broken and create no live
constraint. No serialized motor, force, anchor or GPU handle is trusted.

Installation first pauses the existing world and checks actual boat, banked
cargo and player occupancy against the ten fixed structural boxes. It then
admits the gantry through one additional neutral tick. The whole v2 checkpoint
must publish through the exclusive storage host before the structure becomes
visible and controls unlock. A failed prepublication save stays paused for
explicit retry; the in-memory installation does not announce success.

Explicit installation also moves the already banked generator onto a supported,
unoccupied outer section of the existing pier. Support uses the unchanged LEGO
terrain and fixed deck collision; the inner walking lane remains clear. The old
static cargo body must be observed retired and its replacement admitted before
capture. The same v2 archive saves the actual replacement pose and upgrade. This
creates no second cargo or reward. Loading an older v1/v2 archive never performs
this relocation implicitly. Installed player collision includes the banked cargo
at its saved pose.

The current profile-1 sling recipe includes 5e-6 m/N axial compliance (200 kN/m)
with critical damping derived from actual endpoint effective mass. Fresh and
restored ropes derive that material from the recipe. The four target lengths
remain the saved payout lengths; measured elastic stretch belongs to the actual
physical separation. Force/break limits stay 30/45 kN. This adds no SVCE field or
trusted saved solver parameter; zero-compliance ordinary tow ropes keep their
existing behavior. Native and fresh/resumed browser lift journeys pass.

Controls: K installs after the generator's durable delivery; F requests attachment
or releases from the dock. An unsafe attachment waits at most 600 simulation
ticks for a fresh safe pose, preserving all existing speed/alignment limits.
Stop/Release, leaving the dock or Pause cancels the request; it is transient
and cannot be captured or restored. Browser blur/cleanup also cancels it; hold Q to raise or Z to lower. Browser buttons map to
player actions 52–57 (Install, Attach, Raise, Lower, Stop, Release). Releasing a
held control, leaving the dock controls, Pause or focus loss stops the motor.
Launch is refused while lines retain the boat. Reset releases the rig before
moving the boat. Leave destroys all four lines and the fixed body, observes
their retirement, then retires the uploaded shape. Native/browser action 7
acknowledges either pending delivery or pending installation by exact archive
SHA; installing the lift does not issue another material reward.

## Logical join and retirement

The accepted tick must equal the physical tick. Exactly one nonempty cove boat
(up to 32 parts), one job and the expected full cargo definition are required.
Only welded build connections are in this profile. Unbanked cargo must retain
its exact ID, owner, job and definition. Its historical logical position need
not equal its current physical position. Pending transactions are refused.

A recovered current checkpoint has exactly one retired-parent checkpoint.
The parent must have closed admission, no pending commands, matching world and
content, and the exact predecessor metadata named by `current.origin`.
Current and parent travel together in one storage generation. Retain only the
immediate parent, not an unbounded ancestor chain. This packages the existing
SessionRecovery lineage; it does not create a new token or perform recovery.
After actual restore, publish `recovered.initial` plus `recovered.retired`
atomically before accepting new durable work. Never treat the RAM journal test
acknowledgment as a disk acknowledgment.

## Current application boundary

`Application::salvageExpeditionAction(1, "")` returns lowercase SVCE hex only
at the actual joined Paused boundary. It rechecks scheduled/encoded/submitted/
completed/boat/cargo/event ticks and the allocated rope tick, refuses pending
transactions, workshop/launch/retirement/device loss, and captures the actual
player, body observations, neutral rope, water settings and logical session.
Capture is read-only with respect to accepted state and authority.

Action 2 consumes bounded lowercase hex and checks it against this world's
independently constructed context. `CoveRestoreCandidate::prepare` owns the
validated archive, rebuilt boat/scene/player and independent cargo. It derives
paid render slots and tow anchors from installed content and accepted parts,
validates the one-helm/one-propeller/at-most-one-winch profile, and checks the
saved player against the actual boat transform. No backend or authority is
created by candidate preparation. Invalid input leaves the live world intact.
The catalog must outlive the candidate's validated checkpoint.

Action 2 requires a paused matching world during ordinary gameplay. It is also
available during startup's `awaiting-storage` phase. The browser export is
`voxy_salvage_expedition_action`; return is hex/`ok` or empty on refusal.

## Startup restoration and durable owner transfer

`Application::stageCoveResume(expectedWorld, bytes)` is a host-only pre-init
entry. The host must own the selected storage slot exclusively and retire the
previous application/backend first. Bounded size, schema and SHA checks admit
the source; the u64 tick is a startup hint until full content/domain validation.
`voxy_stage_cove_resume(worldHex, archiveHex)` provides the browser bridge.
It cannot replace an initialized application.

A fresh WebGPU backend receives `GpuConfig::initialTick` before any commands.
A nonzero base cannot rebase a live backend or fall back to CPU physics.
Overflow near the u64 ceiling is rejected. CPU frontiers retain full u64 time;
GPU low32 counters wrap normally without relabeling earlier work.

After content loads, the application prepares an owning candidate, creates a
fresh session using `SessionRecovery::restore`, and retains its finalized
immediate parent. The world remains frozen. The host then:

1. Reads action 3 (empty input): the recovered initial session + retired parent
   + unchanged saved physical state in one SVCE archive.
2. Publishes that archive as the next storage generation; waits for actual
   transaction completion, retaining the exclusive world lock.
3. Supplies action 4 with the exact lowercase SHA-256 of the whole published
   SVCE. Only then can the application stage the saved physical bodies.

Action 4 is a trusted host acknowledgment, not independent disk verification.
Action 5 revokes the storage owner, closes session admission and freezes further
input/time. Failure or page closure cannot activate an uncommitted owner.

Boat and cargo motion enter through their reconstructed mass frames. A banked
cargo body is static and has no buoyancy driver. A towed cargo gets a freshly
allocated rope with derived anchors/limits, saved length and neutral motor;
a broken tow remains inert. Water settings/time, camera and player state are
restored. Exactly one neutral initialization tick admits physical objects, then
the normal pause boundary joins actual bodies, rope, events and session time.
The loaded game reports Ready and Paused only after this tick completes.
This is semantic resume with one tick of settling, not bit-exact continuation.

Browser `web/cove_saves.js` connects this protocol to the real Save expedition
button and IndexedDB store. Pause, then Save; success appears after publication.
The URL receives `world=<32 lowercase hex digits>` and reload selects that
world before main runs. Keep/bookmark that address on the same browser origin.
A missing or malformed selected world fails visibly; it never grants a fresh
starter. A fresh route without `world` creates a new expedition. Recovered
workshop/job commands use the current session epoch, not the fresh-game default.

Native `NativeCoveSaves` connects the same protocol to Linux disk storage.
Selected-world loading happens before `Application::init`; the worker keeps its
exclusive slot lock through recovery and play. Each recovered initial/retired
pair must finish mirrored publication before action 4. During gameplay the
application thread captures an immutable paused archive, then polls a bounded
disk worker. **P**, then **F10** saves; the title reports success only after
the worker's publication completion. `--expedition-world` selects a saved world;
`--expedition-root` optionally selects an absolute save folder. Missing selected
worlds fail before game initialization. Shutdown revokes admission and drains
an already accepted write without delivering a late success message.

## Delivery acknowledgment

Action 6 (empty input) registers a trusted save host on the initialized cove.
Delivery is refused until registration succeeds. Action 5 revocation cannot be
undone through registration. Actions 6/7 are host operations, not player inputs.

An admitted delivery blocks new player actions, neutralizes movement and winch
input, and secures the cargo through the existing physical/session transaction.
Canonical completion requests Pause. Once the normal completed-tick join passes,
the host captures action 1 and publishes the whole Banked SVCE. During delivery,
capture retains the SHA-256 of those exact bytes without changing accepted state.

After publication finishes, action 7 accepts that exact lowercase whole-archive
digest only for the same healthy, paused, secured pending delivery. It marks
the delivery durable and releases Resume. Browser success text requires this
durable flag; an in-memory inventory increase alone is not a saved receipt.
The native host polls `salvageCheckpointNeedsSave()` and makes one automatic save
attempt. The browser coordinator does the same through its state refresh.
Failures require explicit Save/F10 retry; closing/revoking cannot deliver a late
acknowledgment. Wrong acknowledgment fences the live owner for recovery.

Reloading a validated Banked archive restores the completed job, inventory and
static cargo without another payout. The browser's actual build/lift/return/
delivery/reload/repeated-H/sail journey passes. Native actual control journeys
also pass ordinary automatic delivery saving/restart and a real permission
failure followed by explicit retry/restart, without another reward.
[Native delivery evidence](validation/salvage/PLAY-04/native-delivery-r01/README.md).
[Delivery evidence and continuation](validation/salvage/PLAY-04/delivery-r01/README.md).

Manual checkpoints and this delivery checkpoint barrier do not implement general
autosave, a separate durable append journal/compaction, Windows storage, world
listing/export/import, latching or complete live save-fault acceptance. The old
archive-only evidence below is historical; reload evidence is separate.

[Archive checkpoint](validation/salvage/SAVE-04/archive-r01/README.md).
[Native host and actual process restarts](validation/salvage/SAVE-04/native-host-r01/README.md).

## Explicit rescue checkpoint (PLAY-05 component)

Shipped Rescue/R uses the existing accepted boat and cargo bodies. It does not
submit an economic command, allocate another part/cargo/entitlement identity,
change a blueprint, or grant inventory. A completed generator remains static;
an undelivered generator returns to its authored recovery position. The current
accepted boat returns upright to its launch berth at the actual wave height.
All fitted paid parts and starter provenance stay in the same canonical build.

The Application owns `None → Requested → Releasing → Moving → Saving → None`.
Requested obtains the ordinary paused physics/session/event join. Releasing
retires the tow rope and all harbor slings through another neutral tick. The
tow handle is retained until post-command dead evidence arrives (retired GPU
slots report generation zero). Only then does Moving enqueue boat/cargo poses,
zero velocities and reset the player. Another joined neutral tick observes the
actual result; a root farther than 0.25 m from its requested return refuses the
checkpoint. Saving captures that result, including actual residual wave/gravity
motion, through the existing bounded SVCE codec. No save-format bytes change.

Resume, another Rescue, Leave and economic controls are refused during recovery.
Intermediate release/move phases cannot be manually captured. Native and browser
hosts attempt the Saving checkpoint once, even when cargo is unbanked. Exact
whole-archive SHA-256 acknowledgment through host action 7 unlocks Resume only
after publication; failure stays paused for explicit Save/F10 retry. Storage
revocation and ambiguous publication retain the existing recovery rules.
`checkpointPending`/`checkpointDigest` now name the shared delivery/harbor/rescue
barrier; `deliveryDurable` continues to mean banked cargo only. JSON `rescue`
exposes phase/pending/savePending and a transient completion counter for UI
acknowledgment. The counter is not economy or persistent progression.

The current recipe has one live accepted craft and one progression cargo; rescue
creates neither replacements nor abandoned duplicates. It does not replenish
loans deliberately removed in the workshop, recover general cut/damaged
fragments, provide a general free-berth search or implement periodic autosave.
Those remain PLAY-05/MECH work. See the scoped
[actual rescue evidence](validation/salvage/PLAY-05/rescue-r01/README.md).

## Additive brick catalogue (LEGO-02)

The original Cove registry remains the world-layout identity. The optional
`asset_fixture_catalog` loads a closed schema-1 object containing only `schema`
and `bundles`, using the same bounded, no-follow, hash-verified content admission.
It may add unused part IDs; it cannot replace an installed ID, even at a new
version, or change placements/navigation. Every saved part still resolves its
full exact content key through the canonical catalogue on restore.

Scene placements now have a separate 96-slot budget; rigid roots stay at 32.
The four recovery-design slots each accept at most the existing blueprint
codec's 128 KiB ceiling, with at most 96 parts and 1,024 connections. This
widens admission bounds without changing version-1 through version-4 field
order or bytes. Existing byte compatibility, an older pre-brick native save,
and actual native/browser brick-build reloads pass; see the
[builder evidence](validation/salvage/LEGO-02/builder-r01/README.md). A prior
executable may refuse new larger designs or unknown brick keys; it must not
silently omit them. Starter entitlement recipes retain their own 32-part,
64-weld bounds.
