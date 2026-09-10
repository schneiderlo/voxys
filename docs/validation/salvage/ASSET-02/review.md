# ASSET-02 independent review

**No open correctness finding remains within the offline ASSET-02 scope.**
Two reproduced defects were fixed during review, followed by an implementation
worker's dependency-URI correction. The final independent results are 9 C++
tests and 20 Python integration tests, using the actual converter
and rebuilt validator. This is CPU/tool acceptance; it is not runtime, visual,
full glTF-profile or cross-platform acceptance.

Reviewer: `render_architecture`, 2026-09-07. Base checkpoint:
`7f28fabfabf3d63726f6cfa1001c3ce1ed557911`. No product code, shared build file,
shader, task checkbox or Git state was changed by this reviewer.

## Findings resolved

| Finding | Observed failure | Fix and final check |
|---|---|---|
| P2: nonregular input could block before validation | Opening a FIFO in `read_bounded` waited for a writer before `fstat` could reject it. An independent subprocess timed out after one second. The converter timeout did not cover this input open. | The wrapper now opens with `O_NONBLOCK` as well as `O_NOFOLLOW`, then requires a regular file. Both FIFO sidecar and FIFO GLB paths reject within a bounded subprocess test, with no publication or staging residue. |
| P2: an empty model could be published as a catalog visual | A digest-bound GLB containing only `asset.version` passed the real converter and validator. The published VMESH had zero vertices, indices, meshes and submeshes. A valid VMESH container alone was insufficient for a usable visual. | The resolver now requires nonempty drawable geometry/material records, finite vertex fields and nonzero normals, plus valid triangle/index ranges. The real empty-GLB cook rejects; additional malformed triangle-count and out-of-range-index cases reject without publishing. |

The original 9/16 test run passed before these discoveries. Those logs remain
in `review-cpp-initial.log` and `review-python-initial.log`; they are not evidence
that the two missing cases previously passed. `cooked-probe/` also remains the
historical publication made before the validator changed.

## Reviewed boundaries

- Closed C++ schema validation bounds bytes, nesting, strings and record counts;
  rejects duplicate JSON keys, unknown fields, lossy/noncanonical durable IDs,
  invalid units and frames; and preserves every accepted field in normalized
  metadata. ID ordering is numeric and independent of exported node order.
- Physical validation goes through the existing `PartCatalog::create` with
  prototype visuals disabled. Mass, inertia, proxy/buoyancy overlap, socket
  references, module parameters, strength, material and cost invariants use the
  shared rules rather than a second Python implementation.
- Each actual source snapshot must match its declared byte length and SHA-256.
  Standard buffer/image dependencies must be embedded; an external URI fails
  even when the containing GLB has the expected digest. The wrapper validates
  bounded GLB container/JSON structure before invoking the converter.
- The per-LOD exported-frame basis remains separate from canonical gameplay
  frames. Mesh-local vertices and stored hierarchy are not rewritten. Canonical
  sockets, proxies, buoyancy and anchors must receive only part placement and
  rotation when the runtime adapter is implemented.
- Private staging, bounded tool execution, converter failure, validator failure,
  malformed outputs and destination races are covered. Linux publication uses
  atomic no-replace rename; an existing destination is preserved. Trusted local
  converter/validator programs are an explicit boundary.
- Final manifests bind the original and normalized sidecar, source GLBs, actual
  converter/validator binaries and exact VMESH output bytes. This is stronger
  than accepting the source digest as proof of cooked output identity.

The extra output checks are basic drawable-data validation. They do not repair
the existing converter's matrix/TRS behavior, attribute-count validation or
unsupported features; ASSET-03 still owns that profile. ASSET-04 must evaluate
the hierarchy, apply the recorded root basis once and verify complete runtime
bundle identities. Separate bundles claiming the same content key/version,
Windows behavior, hard-crash cleanup and power-loss durability remain outside
this milestone as documented.

## Independent verification

Executed from the repository root after the final source/tool freeze:

```sh
/tmp/salvage-sidecar-build/gameplay_sidecar_tests \
  > docs/validation/salvage/ASSET-02/review-cpp-final.log 2>&1
python3 tools/salvage_assets/test_cook_gameplay_asset.py \
  > docs/validation/salvage/ASSET-02/review-python-final.log 2>&1
```

Both commands exited 0. [C++ log](review-cpp-final.log): 9/9 pass using the
strict-warning, undefined-behavior/float-cast-overflow-sanitized standalone
binary. [Python log](review-python-final.log): 19/19 pass, including real
conversion, deterministic repeated cooking, node-reordering/source-rebind,
publication failures and the newly added rejection cases. The reviewer reran
the binaries; the implementation worker performed the recorded final compile.

The reviewer also independently recomputed all final bundle and executable
hashes, source byte bindings and normalized-sidecar size/hash, and compared the
complete output file map with `/tmp/salvage-asset02-final-repeat`. All matched.
The [verification record](review-final-verification.json) pins the inspected
source and tested binary hashes. Final publication:
[`cooked-probe-final/`](cooked-probe-final/).

| Final artifact | SHA-256 |
|---|---|
| Validator | `592d5958376f783119a5b1fc476abc18f44074a82bb98de005105dc60541bb0d` |
| Converter | `79785b8fdde4c1488b10b5c2b312be9e855aa613fb988e36d3fe89c0db3627cb` |
| Cook manifest | `2a77ea227a8a20994070c92759cedd98046c99a9b91762711d14edd47f253e5e` |
| Normalized metadata | `d023d402e4747f9ed69e78d9b5b1e1e2e330f972b2c87374ae7af6b4565c5f5e` |
| VMESH | `4444fd14cfc967b46f3dcbe1079c04f1d0329ed890754ce98f367e71c1c36689` |

Shared CMake/Bazel integration and its required tests remain root-owned. No
in-engine loading, image comparison, gameplay or performance claim follows
from this review.

## Final dependency-URI delta

After the 19-case review, the implementation worker identified that TinyGLTF
recognizes specific `data:` prefixes and otherwise falls back to treating a URI
as an external filename (`third_party/tinygltf/tiny_gltf.h:3339`). A plain
`startswith("data:")` check therefore did not establish that a dependency was
embedded. The wrapper now permits a deliberate subset of four recognized
base64 forms (binary, glTF buffer, PNG and JPEG), strictly decodes the payload,
and rejects empty data. The reviewer checked these prefixes against the actual
vendored loader and reviewed the unknown-prefix/malformed-payload regression.

The fresh command was:

```sh
python3 tools/salvage_assets/test_cook_gameplay_asset.py \
  > docs/validation/salvage/ASSET-02/review-python-uri-final.log 2>&1
```

It exited 0 with **20/20 cases passing**. The previous 19-case log and source
verification record remain unchanged. C++ code and tested binaries did not
change. The final wrapper source is
`d4473e82cdcb8d095ceff6498c6d12ccd56fbe6cb1d29ac49fe165bc3dcbe01e`;
the final Python test source is
`1ed5e7976a1cfa5e2f1e9bf41cb19e0eb10bc4c1d0c81d99a88d2431b7eb6462`.
The [delta verification record](review-uri-final-verification.json) also checks
that `/tmp/salvage-asset02-uri-repeat`, cooked with the final wrapper, is
byte-identical to the retained `cooked-probe-final/` bundle. No further finding
remains, and this independent ASSET-02 review is frozen at these source hashes.
