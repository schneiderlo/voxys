# Surface acquisition timeout — bounded source review

Reviewed 2026-09-12 during the bounded four-hour implementation session. Read-only code/header/log inspection; no test, application, GPU, browser or image runs by this reviewer.

## Finding and exact backend evidence

`native-journey-r02/process-1.log:61` records `Failed to get current surface texture: status=1` at 29.604 s. The native build uses **wgpu-native v22.1.0.5**, not Dawn: `build-native-save-host/CMakeCache.txt:534–538` names the bundled native include/library paths and `third_party/wgpu-native/dist/wgpu-native-git-tag` identifies the version.

The actual native header `third_party/wgpu-native/dist/include/webgpu/webgpu.h:514–521` declares Success=0, **Timeout=1**, Outdated=2, Lost=3, OutOfMemory=4, DeviceLost=5. Native suboptimal status is the separate `WGPUSurfaceTexture.suboptimal` boolean at line 1167.

Both installed emdawn headers (the Emscripten SDK cache and the active Bazel Emscripten cache) instead declare SuccessOptimal=1, SuccessSuboptimal=2, Timeout=3 at lines 628–635. They apply to the WASM branch. Treating the native value 1 as a Dawn success would be incorrect.

`Application::render`, lines 2224–2231, returns immediately on a null acquired target. The physics ticket, frame guard and encoder are created afterward. The original timeout therefore skipped a frame without opening a scene ticket or encoding/submitting another physics frame. The same r02 process subsequently logged boarding at 31.390/31.509 s and helm interaction at 43.457 s. This is evidence that presentation resumed; it is not a passing journey. The later navigation failure and the cause of the one timeout are not established by this finding.

## Patch review

The live patch in `src/gpu/context_surface.cpp` explicitly recognizes the named `WGPUSurfaceGetCurrentTextureStatus_Timeout`, warns once while an outage remains pending, releases any returned texture, and returns null. A later successful non-null acquisition emits a recovery message and clears the flag. Existing Success/SuccessOptimal/SuccessSuboptimal acceptance, missing-texture error, view-creation error, and other acquisition error branches remain intact.

The native journey scanner remains strict: its existing uncaptured/WebGPU-validation/error/fatal regular expression is unchanged between the candidate driver and live driver. No old r02 failure evidence was reclassified as a pass.

One minor bookkeeping correction was requested: the new `surfaceTimeoutPending_` state should follow the surface through both Context move operations and be cleared by shutdown. Without that, moving or reusing a Context after a timeout can produce an extra/missing first warning or stale recovery diagnostic. This does not change GPU submission safety. Root applied that narrow correction in `src/gpu/context.cpp`: both move operations copy the flag with the surface and clear the moved-from flag; shutdown resets it alongside `lastSurfaceConfig_`. Source inspection confirms all five assignments. No remaining actionable finding in the final patch.

## Limits

No forced-timeout experiment or patched runtime result is claimed here. Root owns the current native r03 journey and all build/test/publication results. Do not weaken the process scanner or mark the prior r02 journey successful because the status was recoverable.

## Final integration boundary

Root reports that the ongoing native journey uses its separately frozen executable; a concurrent rebuild cannot replace that process image. The final native source adds only the diagnostic flag transfer/reset in `context.cpp` relative to the already-reviewed timeout handling. Do not relabel the frozen executable as containing that final lifecycle delta. Root plans a final rebuilt native startup check and the final browser package; this reviewer did not execute or certify either. No additional gameplay matrix is needed for the diagnostic bookkeeping alone.
