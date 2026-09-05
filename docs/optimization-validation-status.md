# Integration validation status at main promotion

The owner explicitly requested merging all branches into main and continuing
optimization. This is an integration release, not an Intel FPS acceptance claim.

## Passed before promotion

- Combined conflict-resolved CMake WASM build: run 33933267595,
  job 101216133627.
- New shading round and complete combined WebGPU differential tests:
  run 33933604201, artifact 9959417418. Fifteen CPU regression groups passed.
- New-round production WASM build and preload packaging:
  run 33934079919, job 101218515057 (build/package steps passed).
- All native targets compiled and linked after standalone protocol and
  X11-only GLFW dependency fixes: run 33934163133, job 101218752400
  (build step passed).

## Not passed or not completed at promotion

- Full native CTest run was still in progress.
- First sequential browser smoke run reached both application modes but failed
  when SwiftShader lost its device during the second. Reaching eight queued
  frames alone does not prove GPU completion. Do not count this run as a pass.
- The tightened smoke test now launches each scene in a separate browser and
  requires more than the queue limit plus a returned GPU timestamp sample.
  Its isolated follow-up was still in progress.
- No full-scene image equivalence, target Intel/Windows performance, or
  recovery from the user's 150-to-10 FPS regression is established.

The raw shader timings include regressions and are described in
`optimization-integration.md`; all paired samples remain available in the
artifact. Source-level work avoidance is not a measured whole-game speedup.

Further runtime results should be read from those runs or subsequent main CI,
not inferred from compilation success. This snapshot deliberately distinguishes
known failures and pending runtime tests from passed build/shader checks.
