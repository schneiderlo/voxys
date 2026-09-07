# BOOT-02 — reproducible build baseline

Status: complete, 2026-09-07. Root reviewed the packaging/build changes and
integrated evidence from render_architecture and lego_gameplay. Base revision
is `7563f61fd536df7de5d209ff35d3cd2099ebcbb6`, with the reviewed working changes
on `codex/salvage-implementation`. Source and artifact hashes accompany the
reports below. This task is not a full-suite, performance, or game gate.

| Required evidence | Result and reproduction |
|---|---|
| Nix, compiler, native build | Passed; [native-build.md](native-build.md) |
| Current CMake native integration | Latest `voxy_native` and `voxy_tests` built; 77 selected CPU cases passed in `native-final.log`; hashes in `native-final-artifacts.json` |
| Native incremental runtime files | Passed for 36 files and no-op executable preservation; [native-assets.md](native-assets.md) |
| Native Bazel | Actual application and importer builds passed; 102 cases across five focused targets passed, including six Radeon GPU cases; `native-wasm-bazel-final-build.log` and native test logs |
| Fresh CMake WASM | Passed with latest application/construction source; source fingerprint stable; [cmake-wasm-fresh.md](cmake-wasm-fresh.md) |
| Fresh Bazel WASM | Passed, including final application/DATA-01 integration; `native-wasm-bazel-final-build.log` |
| Package/HTTP/source parity | All four route configs, 57 source-matching preloads, 27 raw shaders, actual control exports and six negative controls passed; [packaging-build.md](packaging-build.md) |
| Packaged terrain decoding | Actual engine loader passed 256² shore and 8192² landscape; shore's 65,536 samples match source; `cmake-wasm-terrain-check.log` |
| Shared LEGO surface | `python3 scripts/sync_lego_surface.py --check` passed: `LEGO shader surface: consistent` |

Final root commands after the source fixes:

```sh
nix-shell --run 'cmake --build build-salvage-native --target voxy_native voxy_tests --parallel 4 && build-salvage-native/bin/voxy_tests --gtest_filter=ConfigDefaultsTest.*:ConfigFileTest.*:ConfigUtilsTest.*:CommandLineArgsTest.*:LegoSurface.*:ConstructionTypes.*'
nix-shell --run 'bazel build -c opt --jobs=4 //:voxy_native //tools:gltf_vmesh_tool && bazel build --config=wasm --jobs=4 //:voxy_wasm //tools:serve_wasm'
```

The earlier failed builds remain in the evidence directory. Fixes addressed
the unsorted authority allowlist, missing Bazel header/config declarations,
incomplete browser staging, SDK dependency compatibility, one ignored return
value and explicit numeric conversions in an existing test. No hook, assertion,
shader-validation rule or warning was disabled to pass.

BOOT-03 owns visible frame/memory baselines. BOOT-05 owns all-route startup
and the new salvage preview. The full repository hook suite must still pass
before a successful gate commit. The available machine is Linux/Radeon;
these build results do not certify Windows or other release hardware.
