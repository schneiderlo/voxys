#!/usr/bin/env python3
"""Compile shared asset admission/hash/prefab tests with real JS EH and run in Node."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--node", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--exception-mode", choices=["native", "js"], default="js")
    parser.add_argument("--asyncify", action="store_true")
    parser.add_argument("--stack-probe", action="store_true")
    parser.add_argument("--functional-kit", action="store_true", help="Include installed kit and assembly compiler checks")
    args = parser.parse_args()
    if args.exception_mode != "js" or not args.asyncify:
        parser.error("asset acceptance requires --exception-mode js --asyncify")
    root = Path(__file__).resolve().parents[1]
    sdk, node, output = args.sdk.resolve(), args.node.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)  # Preserve every earlier attempt.
    shutil.copyfile(__file__, output / "executed-runner.py")
    work = Path(tempfile.mkdtemp(prefix="voxys-asset04-wasm-"))
    cache = work / "cache"
    # SDK cache is read-only input. Do not use or modify Bazel's shared cache.
    subprocess.run(["cp", "-a", "--reflink=auto", str(sdk / "emscripten/cache"), str(cache)], check=True)
    config = work / ".emscripten"
    config.write_text("\n".join([
        f"LLVM_ROOT = {str(sdk / 'bin')!r}", f"BINARYEN_ROOT = {str(sdk)!r}",
        f"NODE_JS = {str(node)!r}", "FROZEN_CACHE = True", "",
    ]))
    env = dict(os.environ, EM_CONFIG=str(config), EM_CACHE=str(cache), PYTHONDONTWRITEBYTECODE="1")
    compiler = [sys.executable, str(sdk / "emscripten/em++.py")]
    commands = []
    heap_bytes = 134217728 if args.functional_kit else 67108864
    manifest = {"work_directory": str(work), "repository": str(root), "commands": commands,
                "sdk": str(sdk), "node": str(node), "source_hashes": {}, "runtime_hashes": {},
                "artifacts": {}, "passed": False, "exception_mode": args.exception_mode,
                "asyncify": args.asyncify, "fixed_heap_bytes": heap_bytes, "fixed_stack_bytes": 1048576,
                "functional_kit": args.functional_kit}

    def save():
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    def run(name, command, expect_failure=False):
        print(name, flush=True)
        started = time.monotonic()
        with (output / f"{name}.log").open("wb") as stream:
            result = subprocess.run(command, cwd=root, env=env, stdout=stream, stderr=subprocess.STDOUT)
        commands.append({"name": name, "argv": command, "exit_code": result.returncode,
                         "expected_exit": "nonzero" if expect_failure else 0,
                         "seconds": round(time.monotonic() - started, 3)})
        save()
        if bool(result.returncode) != expect_failure:
            raise RuntimeError(f"{name} failed ({result.returncode}); preserved {output / (name + '.log')}")

    sources = ["src/game/construction/construction_types.cpp", "src/game/construction/part_catalog.cpp",
               "src/game/construction/build_model.cpp", "tests/test_pontoon_pipeline.cpp", "tests/test_hierarchy_pipeline.cpp",
               "src/game/assets/gameplay_sidecar.cpp", "src/game/assets/rigid_prefab.cpp",
               "src/game/assets/cooked_part_bundle.cpp", "src/game/assets/cooked_part_directory.cpp", "src/game/assets/fixture_registry.cpp", "src/moto/vmesh_io.cpp",
               "src/game/wreckwater_content_manifest.cpp", "tests/test_sha256.cpp",
               "tests/test_wreckwater_content_manifest.cpp", "tests/test_rigid_prefab.cpp",
               "tests/test_cooked_part_bundle.cpp", "tests/test_cooked_part_directory.cpp", "tests/test_fixture_registry.cpp", "tools/salvage_assets/test_gameplay_sidecar.cpp"]
    headers = ["src/game/construction/construction_types.hpp", "src/game/construction/part_catalog.hpp",
               "src/game/construction/build_model.hpp",
               "src/game/assets/gameplay_sidecar.hpp", "src/game/assets/rigid_prefab.hpp",
               "src/game/assets/cooked_part_bundle.hpp", "src/game/assets/cooked_part_directory.hpp", "src/game/assets/fixture_registry.hpp", "src/core/sha256.hpp",
               "src/moto/vmesh.hpp", "src/game/wreckwater_content_manifest.hpp",
               "tools/salvage_assets/gameplay_sidecar.hpp", "third_party/tinygltf/json.hpp",
               "tools/salvage_assets/fixtures/probe.gameplay.json"]
    headers += [str(path.relative_to(root)) for path in sorted((root / "third_party/glm/glm").rglob("*")) if path.is_file()]
    headers += [str(path.relative_to(root)) for path in sorted((root / "data/salvage/runtime_probe_v1").glob("*")) if path.is_file()]
    headers += ["data/salvage/fixture-pontoon-v2.json", "data/salvage/fixture-pontoon-assembly.json",
                "data/salvage/fixture-pontoon-rotations-a.json", "data/salvage/fixture-pontoon-rotations-b.json"]
    headers += [str(path.relative_to(root)) for path in sorted((root / "data/salvage/pontoon/release-candidates/v2-rc01/cooked").glob("*")) if path.is_file()]
    headers += [str(path.relative_to(root)) for path in sorted((root / "data/salvage/hierarchy_probe/candidates/v1-r02/cooked").glob("*")) if path.is_file()]
    headers += ["data/salvage/hierarchy_probe/candidates/v1-r02/source/golden.json"]
    kit_files = []
    if args.functional_kit:
        sources += ["tests/test_functional_kit.cpp", "src/game/construction/assembly_compiler.cpp",
                    "src/geometry/box_union.cpp", "src/game/construction/assembly_collision.cpp",
                    "src/game/construction/orthogonal_coverage.cpp", "src/game/construction/assembly_buoyancy.cpp",
                    "src/game/construction/assembly_functions.cpp", "src/game/construction/compiled_assembly.cpp"]
        headers += [str(p.relative_to(root)) for directory in ("src/geometry", "src/game/construction")
                    for p in sorted((root / directory).glob("*.hpp"))]
        for label in ("narrow", "broad", "cargo"):
            filename = f"data/salvage/fixture-kit-{label}.json"
            kit_files.append(filename)
            registry = json.loads((root / filename).read_text())
            for bundle in registry["bundles"]:
                directory = root / "data/salvage" / bundle["directory"]
                kit_files += [str((directory / leaf).relative_to(root)) for leaf in
                              ("gameplay.json", "cook-manifest.json", "lod-1.vmesh", "lod-2.vmesh", "lod-3.vmesh")]
        kit_files = sorted(set(kit_files))
        headers += kit_files
    gtest = root / "third_party/googletest/googletest"
    for path in [*(root / source for source in sources + headers),
                 *sorted((gtest / "src").glob("*")), *sorted((gtest / "include").rglob("*.h"))]:
        if path.is_file():
            manifest["source_hashes"][str(path.relative_to(root))] = digest(path)
    for path in [node, sdk / "bin/clang", sdk / "bin/wasm-ld", sdk / "bin/wasm-opt",
                 sdk / "emscripten/em++.py", sdk / "emscripten/emcc.py",
                 sdk / "emscripten/emscripten-version.txt"]:
        manifest["runtime_hashes"][str(path)] = digest(path)
    manifest["runner_hash"] = digest(__file__)
    save()
    try:
        run("compiler-version", compiler + ["--version"])
        run("node-version", [str(node), "--version"])
        # A compiler macro proof prevents GTest's EXPECT_THROW from being
        # silently compiled as a no-exceptions fallback.
        probe = work / "exception-probe.cpp"
        probe.write_text('#include <gtest/gtest.h>\nstatic_assert(GTEST_HAS_EXCEPTIONS == 1);\n'
                         'static_assert(__has_feature(cxx_exceptions));\n')
        exception_flag = "-fwasm-exceptions" if args.exception_mode == "native" else "-fexceptions"
        common = ["-std=c++20", "-O1", "-g", exception_flag, "-Isrc",
                  "-isystem", str(gtest / "include"), "-Itools", "-isystem", "third_party/glm",
                  "-isystem", "third_party/tinygltf", "-DGLM_FORCE_DEPTH_ZERO_TO_ONE", "-DGLM_FORCE_LEFT_HANDED"]
        warnings = ["-Wall", "-Wextra", "-Wshadow", "-Wnon-virtual-dtor", "-Wold-style-cast",
                    "-Wcast-align", "-Wunused", "-Woverloaded-virtual", "-Wpedantic",
                    "-Wconversion", "-Wsign-conversion", "-Wnull-dereference", "-Wdouble-promotion",
                    "-Wformat=2", "-Werror"]
        run("exception-feature-probe", compiler + common + ["-c", str(probe), "-o", str(work / "probe.o")])
        objects = []
        for index, source in enumerate(sources):
            obj = work / f"project-{index}.o"
            run(f"compile-{index}-{Path(source).stem}", compiler + common + warnings + ["-c", source, "-o", str(obj)])
            objects.append(str(obj))
        # Compile actual third-party GTest sources with their normal flags;
        # project warnings are deliberately not imposed on vendored source.
        for source in [gtest / "src/gtest-all.cc", gtest / "src/gtest_main.cc"]:
            obj = work / (source.stem + ".o")
            run("compile-" + source.stem, compiler + common + ["-I", str(gtest), "-c", str(source), "-o", str(obj)])
            objects.append(str(obj))
        binary = output / "asset_admission_tests.js"
        link_flags = ["-O1", "-g", exception_flag, "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1",
                      "-sALLOW_MEMORY_GROWTH=0", f"-sINITIAL_MEMORY={heap_bytes}",
                      "--embed-file", "tools/salvage_assets/fixtures/probe.gameplay.json",
                      "--embed-file", "data/salvage/runtime_probe_v1",
                      "--embed-file", "data/salvage/fixture-pontoon-v2.json",
                      "--embed-file", "data/salvage/fixture-pontoon-assembly.json",
                      "--embed-file", "data/salvage/fixture-pontoon-rotations-a.json",
                      "--embed-file", "data/salvage/fixture-pontoon-rotations-b.json",
                      "--embed-file", "data/salvage/pontoon/release-candidates/v2-rc01/cooked",
                      "--embed-file", "data/salvage/hierarchy_probe/candidates/v1-r02/cooked",
                      "--embed-file", "data/salvage/hierarchy_probe/candidates/v1-r02/source/golden.json"]
        # Pontoon files are already embedded by the unchanged base suite.
        for filename in kit_files:
            if "/pontoon/" not in filename:
                link_flags += ["--embed-file", filename]
        if args.asyncify:
            link_flags.append("-sASYNCIFY=1")
        if args.stack_probe:
            diagnostic = output / "stack-64k-diagnostic.js"
            run("link-stack-64k", compiler + link_flags + ["-sABORTING_MALLOC=0",
                "-sSTACK_SIZE=65536", "-sSTACK_OVERFLOW_CHECK=2", *objects, "-o", str(diagnostic)])
            run("tests-stack-64k", [str(node), str(diagnostic)], expect_failure=True)
            message = (output / "tests-stack-64k.log").read_text().lower()
            if "stack overflow" not in message:
                raise RuntimeError("64 KiB diagnostic failed without confirmed stack overflow")
            manifest["default_stack_failure_confirmed"] = True
        run("link", compiler + link_flags + ["-sABORTING_MALLOC=0", "-sSTACK_SIZE=1048576",
            "-sSTACK_OVERFLOW_CHECK=2", *objects, "-o", str(binary)])
        run("tests", [str(node), str(binary)])  # No filter and no disabled test cases.
        test_log = (output / "tests.log").read_text()
        expected_cases = 75 if args.functional_kit else 70
        if f"[  PASSED  ] {expected_cases} tests." not in test_log or "[  SKIPPED ]" in test_log:
            raise RuntimeError(f"expected all {expected_cases} real WASM CPU cases with zero skips")
        manifest["tests_passed"] = expected_cases
        manifest["tests_skipped"] = 0
        for path in [binary, binary.with_suffix(".wasm")]:
            manifest["artifacts"][path.name] = {"sha256": digest(path), "bytes": path.stat().st_size}
        # Verify inputs remained frozen for the entire compilation.
        manifest["sources_unchanged"] = all(digest(root / path) == value
            for path, value in manifest["source_hashes"].items())
        if not manifest["sources_unchanged"]:
            raise RuntimeError("source changed during validation")
        manifest["passed"] = True
    except Exception as error:
        manifest["failure"] = str(error)
        raise
    finally:
        save()


if __name__ == "__main__":
    main()
