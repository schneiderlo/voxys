# ASSET-03 integrated strict-profile acceptance

The offline converter and salvage cooker have passed the required integrated
checks on Linux. The explicit profile is `salvage-rigid-v1`. New gameplay cooks
use it without retrying in legacy mode, and the manifest records the profile,
CLI interface, Python cooker source, actual tool binaries and each input/output
hash. The retained ASSET-02 bundle/source remains historical evidence.

| Executed check | Result | Evidence |
|---|---|---|
| Optimized Bazel converter | 33 cases passed | [Cases](bazel-converter.log), [XML](bazel-converter.xml) |
| Optimized Bazel actual cooker | 23 cases passed | [Cases](bazel-cooker.log) |
| CMake converter and actual cooker | 33 C++ cases plus one Python suite containing23 cases passed | [CTest cases](cmake-cases.log), [Command](cmake-command.log) |
| Repeated complete cook | All three bundle files byte-identical | [Commands](repeat-cook-commands.json), [Result](repeat-cook-result.json) |
| Unknown CLI profile | Exit2, no output created | [Observed result](unknown-profile.json) |

No case was skipped. These are focused offline checks; they do not claim a full
repository test run, runtime native/browser material review or a gate commit.
The mandatory hook remains enabled for the next passed gate.

[Summary](summary.json) pins source, shared build and executable identities.
The retained [new bundle](cooked-probe-bazel/cook-manifest.json) has manifest
SHA-256 `f595f6b490fbbcc6f947eb1ac31f7328cc4dedf9d84896268a8aed45dea80898`.
Normalized metadata and VMESH remain byte-identical to ASSET-02 for this
factor-only probe; its unchanged output does not test texture orientation.
The separate colored-quadrant fixture supplies that proof.

[Independent converter review](../root-review.md) covers accessor/feature/image
limits, matrix orientation, texture rows and the final tangent/RAII correction.
[Independent wrapper review](review.md) covers explicit invocation, manifest
identity and publication behavior. The author report and retained failure logs
provide the separate strict sanitizer checks and the preceding failed fixtures.

The supported profile is a bounded opaque rigid kit. It rejects skins,
animation, unsupported material/texture features, extra vertex attributes,
external dependencies and malformed transforms. It preserves exported local
geometry and node transforms; the per-LOD canonical basis is still metadata.
ASSET-04 must evaluate the full hierarchy, apply that basis exactly once, load
the new bundle in both runtimes and inspect actual rendered dimensions,
textures, socket fit and LODs. These checks do not improve or approve the cove's
current placeholder appearance.
