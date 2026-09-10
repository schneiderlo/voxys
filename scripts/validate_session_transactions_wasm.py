#!/usr/bin/env python3
"""Compile shared GameSession/journal tests with shipping JS EH/Asyncify; run in Node."""
import argparse
import hashlib
import json
import os
import re
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
    parser.add_argument("--expected-tests", type=int, default=193)
    args = parser.parse_args()
    if args.exception_mode != "js" or not args.asyncify:
        parser.error("session acceptance requires --exception-mode js --asyncify")
    root = Path(__file__).resolve().parents[1]
    sdk, node, output = args.sdk.resolve(), args.node.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)  # Preserve every earlier attempt.
    shutil.copyfile(__file__, output / "executed-runner.py")
    work = Path(tempfile.mkdtemp(prefix="voxys-session-wasm-"))
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
    manifest = {"work_directory": str(work), "repository": str(root), "commands": commands,
                "sdk": str(sdk), "node": str(node), "source_hashes": {}, "runtime_hashes": {},
                "artifacts": {}, "passed": False, "exception_mode": args.exception_mode,
                "asyncify": args.asyncify, "fixed_heap_bytes": 67108864, "fixed_stack_bytes": 1048576}

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
               "src/game/construction/build_model.cpp", "src/game/construction/build_refit.cpp", "src/game/construction/assembly_compiler.cpp", "src/game/expedition/session_transactions.cpp",
               "src/game/expedition/session_events.cpp", "src/game/expedition/game_session.cpp", "src/game/expedition/session_recovery.cpp", "src/game/expedition/session_save.cpp", "src/game/expedition/save_generation.cpp", "src/game/expedition/cove_save.cpp", "tests/test_cove_save.cpp", "tests/test_cove_water_clock.cpp", "tests/test_save_generation.cpp", "tests/test_game_session.cpp", "tests/test_session_transactions.cpp", "tests/test_session_events.cpp"]
    headers = ["src/game/expedition/cove_water_clock.hpp", "src/game/expedition/cove_save.hpp", "src/game/expedition/cove_harbor_state.hpp", "src/game/expedition/save_generation.hpp", "src/core/sha256.hpp", "src/game/expedition/session_save.hpp", "src/game/construction/construction_types.hpp", "src/game/construction/part_catalog.hpp",
               "src/game/construction/build_model.hpp", "src/game/construction/build_refit.hpp", "src/game/construction/assembly_compiler.hpp", "src/game/expedition/game_session.hpp",
               "src/game/expedition/session_transactions.hpp", "src/game/expedition/session_recovery.hpp", "src/game/expedition/session_events.hpp"]
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
                  "-isystem", str(gtest / "include")]
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
        binary = output / "game_session_tests.js"
        link_flags = ["-O1", "-g", exception_flag, "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1",
                      "-sALLOW_MEMORY_GROWTH=0", "-sINITIAL_MEMORY=67108864", "-sSTACK_SIZE=1048576"]
        if args.asyncify:
            link_flags.append("-sASYNCIFY=1")
        run("link", compiler + link_flags + ["-sABORTING_MALLOC=0", *objects, "-o", str(binary)])
        run("tests", [str(node), str(binary)])  # No filter and no disabled test cases.
        log = (output / "tests.log").read_text()
        count = re.search(r"\[  PASSED  \] (\d+) tests?\.", log)
        if not count or int(count.group(1)) != args.expected_tests or "[  SKIPPED ]" in log or "DISABLED TEST" in log:
            raise RuntimeError(f"expected all {args.expected_tests} shared cases, zero skips/disabled")
        manifest["tests"] = {"passed": int(count.group(1)), "skipped": 0, "disabled": 0}
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
