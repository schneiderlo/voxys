# Native save storage — Linux implementation

Implemented Linux component of SAVE-02, connected to the playable cove's manual
save/restart host. Complete host-validated SVCE archives preserve logical and
physical expedition state. Windows, durable banking/journal acknowledgment and
the remaining recovery cases are still required by `GAME_IMPLEMENTATION_TODO.md`.

## Public boundary

`src/engine/platform/native/save_store.hpp` provides `NativeSaveStore`:

- `defaultRoot(issue)` resolves Linux `$XDG_DATA_HOME/voxys/saves`, when XDG is an
  absolute path, otherwise `$HOME/.local/share/voxys/saves`. It creates nothing.
- `open(absoluteSlotDirectory, expectedWorld, issue, optionalTestObserver)` creates
  the directories and holds an exclusive process lock for the object's lifetime.
- `load(output, issue)` returns the newest intact generation, or generation zero
  for a genuinely empty slot. Damaged saves are never treated as new worlds.
- `publish(expectedGeneration, payload, issue)` writes the next generation only
  when the current generation matches. Success follows complete publication of
  both replicas. Generation overflow, empty/oversized payloads and stale requests
  reject before publication.

The save worker must serialize all calls on an instance. These operations block;
never run them in the simulation/render loop. File locking also excludes other
cooperating processes; it is not a mutex for concurrent calls on the same object.
Other platforms explicitly return UnsupportedPlatform until their backend exists.

The caller supplies the trusted world identity and validates the payload's content,
schema, ownership, lineage and physical coherence. The file wrapper does not turn
arbitrary bytes into an authorized game checkpoint. Do not expose opaque payload
publication through player commands. The cove uses the complete
checkpoint/lineage/physical SVCE archive specified in `salvage-cove-save-format.md`.

## Portable outer envelope

`src/game/expedition/save_generation.hpp/.cpp` is shared native/WASM code. SVSG v1
contains:

| Offset | Field |
| --- | --- |
| 0 | ASCII SVSG, 4 bytes |
| 4 | Envelope version u32, 1 |
| 8 | World namespace, 16 raw bytes |
| 24 | Generation u64, nonzero |
| 32 | Payload length u64 |
| 40 | Payload, 1 through 16 MiB |
| End minus 32 | SHA-256 of all preceding bytes |

Integers are little endian and never pass through JavaScript Number. The total
size is payload size plus 72. Unknown versions, mismatched independently expected
worlds, checksum errors, size mismatch, zero generations and trailing/truncated
bytes reject. Encode/decode publish output only after success; allocation failure
returns Capacity. This is an integrity wrapper, not an authentication signature.

The cove payload is SVCE, containing SVSC logical state, a versioned physical
snapshot and atomic recovery lineage. General journal schemas remain defined
in `salvage-session-save-format.md`.

## Files and publication order

One slot contains only these backend-owned names:

- `writer.lock`: stable lock inode. Never remove or replace it while the slot is
  usable; doing so could permit two process owners.
- `current`, `mirror`: independent complete SVSG files. After acknowledgment they
  contain the same latest generation, including its digest.
- `next`: bounded staging file, ignored by loading and replaced by the next write.

The mirror protects the latest acknowledged generation. It is not a user-visible
history of older saves. During a new publication it remains the prior good copy
until the new current copy is safely published.

Publication writes `next` in bounded 64 KiB chunks, flushes and closes it, replaces
`current`, then flushes the directory. It repeats that sequence for `mirror` and
only then returns success. Directories created while opening the slot also have
their parent directories flushed. On Linux, flushing a file alone does not ensure
its directory entry is persisted. [Linux fsync contract](https://man7.org/linux/man-pages/man2/fsync.2.html).

Replacement uses same-directory `renameat`. Linux replaces an existing target
atomically, preserving a complete visible pathname across the replacement.
[Linux rename contract](https://man7.org/linux/man-pages/man2/rename.2.html).

The process takes a nonblocking exclusive `flock` on the stable lock descriptor.
The kernel releases it when its last owning descriptor is closed, including after
process death. Tests verify both competing opens and killed writers. This backend
targets Linux local filesystems; network-filesystem locking/rename behavior is not
certified. [Linux flock contract](https://man7.org/linux/man-pages/man2/flock.2.html).

Paths must be absolute, non-root, contain no parent traversal or embedded NUL, and
have no symlink components. Staging/lock files must be regular and singly linked.
A stale staging symlink or hard link is refused before truncating its target.
Files are created private (0600), directories private (0700, subject to umask).

## Read and failure semantics

Load checks both copies, their expected world, version, exact size and digest. It
chooses the larger intact generation. If only one copy is valid, or generations
differ after an interrupted publication, `needsRepair` is true. The caller must
validate that payload and use a successful publication to restore redundancy
before claiming a new durable result.

Equal generations with different payloads are a conflict. Two damaged copies
reject. An unsupported version or an actual read/access/allocation error refuses
loading instead of silently rolling back to an older supported copy. Outputs
remain unchanged on these failures. The latest intact generation after an
interrupted write may be either the old generation or a newly published but
unacknowledged one; the caller must reconcile its exact request ledger.

If a failure occurs after replacement may have begun,
`publicationMayHaveHappened` is true. The store is poisoned for future publication
and reports RecoveryRequired. Close and reopen it, load and validate the actual
stored generation, reconcile authority/lineage, and then resume. Never translate
this result into “nothing happened,” issue a replacement reward, reuse old
requests or refill initial resources.

Disk-full/quota errors become NoSpace; access/read-only errors become Permission;
other system failures become Io with the original error number. Bounded staging
left by failures is not a checkpoint. Capacity/conflict/pre-publication failures
do not replace the accepted files.

## Verification and remaining work

The focused native suite covers nine cases: mirrored writes/revision checks,
corruption and unknown-version refusal, exclusive ownership, link safety,
16 injected I/O failures, actual SIGKILL at all 16 write boundaries, four injected
space/permission errors, conflicting/wrong-world files, and an actual SVSC
session close/reopen preserving a purchased part and rejecting its old request.

The final tests run on the workspace's ext4 filesystem. Earlier `/tmp` runs used
tmpfs and are retained as process/filesystem tests, not disk evidence. SIGKILL
proves process-interruption recovery; it does not emulate power loss or prove a
drive/controller honors flushes. Full-disk/quota/access faults are injected at the
real I/O boundary; no claim is made that the device was physically filled.

The cove's archive and physical restoration now connect this backend to actual
native save/restart controls. `NativeSaveWorker` owns the blocking store and its
destruction on a dedicated thread. At most one operation may be accepted,
including its unconsumed result. Game code polls completions; accepted writes
drain on shutdown. Publication uncertainty keeps the writer poisoned, and the
host revokes live ownership until restart. The optional fault observer must
outlive the worker and synchronize cross-thread state.

Four worker cases verify delayed flush, bounded admission, completion ordering,
shutdown/drain, disk-full retry and uncertainty. The real native host journey
buys a pontoon, saves, terminates its process, reloads, buys another pontoon,
saves and reloads again. Competing-process and missing-selected-world refusal
also pass. [Native host evidence](validation/salvage/SAVE-04/native-host-r01/README.md).

The host also registers delivery support and automatically publishes a joined
Banked archive once physical securing and canonical banking reach Pause. It
acknowledges the exact captured digest only after the worker completes mirrored
publication. Failed prepublication writes remain pending for F10 retry; uncertain
or rejected acknowledgments revoke the live owner. Both native build systems
pass. Actual native control journeys verify ordinary H/automatic save/restart
and a real unwritable-folder failure followed by F10 retry/restart. Both retain
the paid build and reward, reject repeated delivery and sail away from the
secured load. [Native delivery evidence](validation/salvage/PLAY-04/native-delivery-r01/README.md).
The corresponding [browser journey](validation/salvage/PLAY-04/delivery-r01/README.md)
also passes.

Continue with remaining live fault recovery, durable journal compaction,
Windows and world selection/export/import. Native
towing/latching restart acceptance also remains open. No full SAVE gate passes
from these scoped components alone.

## Read-only native validation

`--expedition-observe /absolute/new-directory` opts into a bounded diagnostic
snapshot. The directory must not exist and its parent must exist. The host
replaces `state.json` atomically at most ten times per second, with a 64 KiB
payload ceiling and at most one temporary file. It calls the same read-only
application state getter as browser validation; it adds no GPU readback, input
action, state setter or screenshot. Ordinary launches do none of this I/O.
These samples are observations, not saves. Their write cost is diagnostic
overhead and cannot establish gameplay performance. Observer errors stop the
observer without acknowledging storage or changing game authority.

`scripts/validate_native_cove_delivery.py` uses those observations with actual
X11 keys targeted only at its own child window. It reads GLFW's physical key
scancodes, which avoids swapping the game's Q/W/A/Z actions on AZERTY hosts.
`--permission-failure` makes only its new isolated save root unwritable, checks
that pending delivery stays frozen, restores permission and retries with F10.
Successful completion also requires actual mirrored save bytes, process restart,
unchanged paid parts/reward, duplicate-delivery refusal and sailing afterward.
