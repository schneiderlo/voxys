# Independent review of strict cooker integration

Reviewer: `simulation_production`, 2026-09-07. **No blocking correctness defect
found in this wrapper integration.** Reviewed the complete 281-line cooker,
294-line Python test file, preceding ASSET-02 version, retained integration
reports and the published probe bundle. Converter internals and visual/runtime
acceptance remain outside this review.

Reviewed source SHA-256:

- `tools/salvage_assets/cook_gameplay_asset.py`:
  `88a32ac9824e2a9864f4d97e4311eeb27a312d917e3ce23d41fd5953c0e7023a`
- `tools/salvage_assets/test_cook_gameplay_asset.py`:
  `d424bc3ea34918cb62ccac3cd0559f7e073d19c6dea89d37dab5fa1d21a97056`

## Findings

1. **The strict profile is mandatory.** The single converter call at
   `cook_gameplay_asset.py:231` always supplies `--profile salvage-rigid-v1`.
   Converter failure leaves the try block and removes the private stage; no
   second legacy invocation exists. The new rejection fixture records the exact
   four arguments and a single invocation. Two other new cases use the actual
   converter to reject unsupported required and optional appearance extensions.
2. **The manifest records the selected behavior.** It includes converter profile
   and exact CLI shape, converter/validator executable hashes, Python cooker
   source hash, input-sidecar hash, per-LOD source/output hashes and lengths, and
   normalized-sidecar hash/length (`cook_gameplay_asset.py:247`). The pre/post
   tool/source comparison at lines 243–246 rejects a persistent change during
   cooking. The existing caller-provided tools remain trusted build inputs;
   complete dependency/authoring provenance belongs to ASSET-07.
3. **Publication remains atomic and does not replace existing output.** Both
   tools write only inside a private sibling staging directory. Validation
   succeeds before metadata/manifest publication. Linux `renameat2` uses
   `RENAME_NOREPLACE`, including a destination created after the initial check.
   Failed conversion/validation removes staging. This is atomic visibility of an
   offline bundle; no crash-durable save/checkpoint guarantee is claimed.
4. **ASSET-02 semantics were preserved.** Source snapshots remain size/digest
   bound and self-contained; malformed JSON, external dependencies, final-path
   symlinks and FIFOs still reject. C++ remains the physical/schema authority and
   rejects unknown fields, invalid mass and invalid VMESH contents. Normalized
   sidecar data, lossless durable IDs, source-frame metadata and exported node
   reorder behavior retain the earlier assertions. Only fake converter fixtures'
   output argument changed from `$2` to `$4`, matching the new explicit CLI.
5. **Existing successful evidence corresponds to these sources.** Root's
   `summary.json` source hashes match both reviewed files. The CMake case log
   records the 23 Python cases passing, and repeat-cook evidence records identical
   bundle bytes with the same tools. This review did not rerun shared builds or
   relabel their results as an independently executed test suite.

## Independent bundle audit

Read the actual `cooked-probe-bazel/cook-manifest.json` and recomputed its bound
file hashes/lengths. All checks passed for the current wrapper source, actual
Bazel converter and validator executables, input sidecar, source GLB, normalized
gameplay JSON and VMESH. The manifest's profile and CLI contract match the code.

The retained VMESH hash is
`4444fd14cfc967b46f3dcbe1079c04f1d0329ed890754ce98f367e71c1c36689`;
normalized gameplay JSON is
`d023d402e4747f9ed69e78d9b5b1e1e2e330f972b2c87374ae7af6b4565c5f5e`.

No production, test or shared build files were changed. This review uses no GPU
and makes no claim about current native/browser appearance, material fidelity,
runtime prefab instantiation or owner visual approval.
