# Native catalog mesh thumbnails — r01

Native catalog and building previews now draw the installed building mesh, using the same orthographic projection and shaded warm palette as the browser SVGs. The generator removes backfaces, sorts visible triangles, and emits compact coordinates at 1/256 pixel precision. Browser SVG bytes remain unchanged. This replaces the prior collision-box diagrams.

The separate Adventure triangle pass draws only the visible icons after the existing panel/text pass. Icon rectangles exclude labels, costs, focus borders and neighboring controls. The Cove font atlas and its existing buffer limit remain unchanged. Native catalog rows display their authoritative material-cost details.

## Verification

- **8 CPU tests passed, 88 ms:** text scales 100%, 125%, 150%; small and large viewports; selected/disabled row intent preservation; source identity and packed mesh hash; seven-card capacity; image bounds; failed initialization and repeat shutdown.
- **1 GPU test passed, 378 ms:** Vulkan on AMD Radeon 890M Graphics (RADV STRIX1), a 1920 × 480 target. All **454 independently sampled interior mesh colors** matched readback. The maximum page rendered after an earlier encoder was discarded. Unchanged content did not upload the icon buffer again. Releasing the HUD before submitted work completed preserved the image. No WebGPU validation errors were recorded.
- The deterministic generator's `--check` verified native and browser outputs against the installed VMESH file. Loading-copy checks parsed all inline page scripts and verified that the Adventure route uses its own five loading phrases and ready label.

These are geometry, capacity and resource-lifetime checks. Their durations are test runtimes, **not gameplay frame-rate evidence**. No screenshots or production preview reload were used.

## Fixed limits

| Quantity | Bound |
| --- | ---: |
| Visible catalog icons | 7 |
| Installed visible triangles across all 14 pieces | 3,350 |
| Largest individual icon | 812 triangles |
| Maximum staged/drawn triangle instances | 5,684 |
| Maximum emitted vertices | 17,052 |
| Dedicated GPU icon buffer | 159,152 bytes |
| Total declared HUD GPU residency, including original font pass | 327,088 bytes |

The triangle table is generated from `data/adventure/building-kit-r01/cooked/building-kit-lod0.vmesh`, SHA-256 `08ce2ff933bdc9d86d68d62e29b15f62d7f22984753f20b51fdb89348fd3236b`. Recipe identity is `adventure-installed-mesh-thumbnails-r02`. The generator, generated header, shader, implementation and test source hashes are recorded in [results.json](results.json). Compressed build logs, test logs and XML are in `checks/`.

Reproduce from the repository root:

```sh
python3 tools/adventure_assets/generate_piece_thumbnails.py --check
nix-shell --run 'bazel test //tests:adventure_hud_layout --jobs=8 --test_arg=--gtest_filter=AdventureHud.* --test_output=errors'
nix-shell --run 'bazel test //tests:adventure_hud_layout --jobs=8 --test_arg=--gtest_filter=AdventureHudGpu.* --test_output=errors'
```

The GPU log includes an environment warning about `XDG_RUNTIME_DIR`; the Vulkan headless context initialized, readback succeeded, and the validation-error counter remained zero. Gate acceptance and the broader native/browser player journey remain the parent task's responsibility.

## Browser-header compatibility follow-up

The integrated browser build exposed a WebGPU header difference: Emscripten places `nextInChain` before the vertex-attribute fields. The implementation now zero-initializes each attribute and assigns its shared fields by name. Adjacent blend fields also use named assignments, and two layout conditions use explicit positive-height checks. Geometry, palette, shader and capacity remain unchanged.

The parent task rebuilt the **WASM target successfully** and repeated the focused native GPU check: **1 test passed, 416 ms**, on the same RADV/Vulkan adapter. All **454 mesh-color samples** matched at 5,684 triangle instances; submitted shutdown remained safe and no WebGPU validation errors were recorded. The browser build retains an unrelated existing comparison-operator warning in `physics_types.hpp`.

The original results above remain intact. [Follow-up results and updated source hashes](compatibility-follow-up-r01/results.json) record the corrected implementation separately, with the failed browser diagnostic, successful browser build, native build log and final GPU test log/XML under `compatibility-follow-up-r01/checks/`. This archive step did not run additional tests or change code. It does not claim completion of the broader browser player journey.
