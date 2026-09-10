# Independent source review — cooked part admission

**Root follow-up, 2026-09-08:** the historical review below incorrectly calls
`sidecar.lods` threshold-ordered. The parser sorts that array by ID; only the
catalog's `part.visuals` is threshold-ordered. A later real-pontoon test using
IDs 91/7/42 caught the resulting selector defect. `selectFixtureLod` now compares
explicit thresholds, including the eye-plane fallback. This correction is
root's finding, not a new independent review or change to the original verdict.

2026-09-08, `render_architecture`. Reviewed root-owned
`src/game/assets/cooked_part_bundle.hpp/.cpp`, the complete 15-case bundle test,
the final consuming-subspan `src/core/sha256.hpp`, and the WRECKWATER extraction
diff. This is source review; root owns the integrated test execution and hashes.

**No confirmed blocker found.** The loader requires an expected manifest digest
from outside the bundle. A rewritten payload plus nearby rewritten manifest
does not authorize itself. Every provider result is an owned, length-bounded
snapshot hashed before decoding; those bytes are not reopened after hashing.
Derived leaf filenames, exact profile/interface/schema, canonical IDs, normalized
sidecar bytes, selected version and complete LOD/source bindings are checked.
Per-file and cumulative ceilings precede or bound decode, then the shared prefab
validator establishes drawable geometry and node semantics. Admission publishes
an immutable CPU owner only on success, leaving a prior active owner untouched.

Shared sidecar/catalog validation remains the single physical-schema authority.
The resolver checks the admitted file set, and the later stable-ID join binds
each unique sidecar visual key to its validated VMESH/basis. Source GLB/tool hashes
are intentionally bound cook provenance; absent tool/source bytes are not claimed
to be rehashed at runtime.

The SHA extraction preserves rounds, endian encoding, initial state and domain
serialization. Its final update loop consumes bounded subspans without the
previous optimizer-sensitive offset expression. `finish()` hashes a copy, so it
does not consume the current prefix. WRECKWATER converts only the digest wrapper;
its byte representation and domain inputs remain unchanged. The documented
bounded-input assumption remains necessary for the u64 bit-length counter.

Integration notes, not current admission defects:

- `bundle.lods()` sorts by stable ID. Application must map the sidecar's
  threshold-ordered LOD entries by ID; it must not assume matching array ordinals.
- `decodedBytes()` reports owned decoded VMESH payload vectors. JSON documents,
  metadata, manifests, snapshots, allocator and GPU-driver overhead are separate
  from that counter; it is not a process working-set measurement.
- Provider security is an explicit caller contract. The native/preload adapter
  still needs to enforce the promised root, no-follow/regular-file and pre-read
  allocation cap behavior; an in-memory provider test cannot certify that adapter.
