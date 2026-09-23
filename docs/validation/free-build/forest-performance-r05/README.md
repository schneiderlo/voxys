# Forest deployment recovery validation

Checked on 2026-09-23 after the fourth forest LOD and shadow proxies reached
`main` in `cd0b9f74`.

- The Pages workflow for `cd0b9f74` built WASM but failed the full browser
  startup test before deployment. Its artifact showed Chrome 152 SwiftShader's
  GPU process exit with code 139. The preceding main commit `4f0763ce` had
  failed at the same step with the same device-loss message.
- A local run reproduced that failure. Replacing only the narrow-phase WGSL
  with the last passing single-patch version then rendered 35 frames and
  returned GPU timing. The final automatic compatibility selection passed the
  same SwiftShader startup test with no shader override and no GPU error.
- The hardware path passed the full startup test on an AMD RDNA 3 adapter,
  rendering 149 frames with the current multi-patch shader.
- The 1920 × 1080 startup request produced a 1946 × 1095 render surface. Its
  AMD sample reported a 6.35 ms GPU frame interval, 9,217 visible forest trees,
  and 1.39 million colour triangles. It does not establish 200 FPS.
- The optimized Blacksmith VMesh is 19,606,015 bytes and 173,475 triangles,
  down from 81,656,239 bytes and 848,467 triangles. The ten movable meshes
  are unchanged. Regenerating the original GLB from the source assembly,
  applying the checked-in fixed-remainder reduction, and cooking the VMesh
  reproduced the installed SHA-256 exactly. The assembly validator passed.
- The focused renderer, adventure, and full-terrain Free Build runtime targets
  passed (three Bazel targets). The browser WASM target built successfully.

The repository-wide pre-commit suite still has the two unrelated failures
recorded in [the previous validation note](../forest-performance-r04/README.md).
