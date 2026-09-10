#!/usr/bin/env python3
"""Build the real authored-shape C++ compute tests for browser WebGPU; preserve evidence."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True, type=Path, help="Emscripten upstream directory")
    parser.add_argument("--node", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    sdk, output = args.sdk.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    work = Path(tempfile.mkdtemp(prefix="voxys-shape-diagnostic-build-"))
    cache = work / "cache"
    subprocess.run(["cp", "-a", "--reflink=auto", str(sdk / "emscripten/cache"), str(cache)], check=True)
    config = work / ".emscripten"
    config.write_text(f"LLVM_ROOT = {str(sdk / 'bin')!r}\nBINARYEN_ROOT = {str(sdk)!r}\n"
                      f"NODE_JS = {str(args.node.resolve())!r}\nFROZEN_CACHE = True\n")
    env = dict(os.environ, EM_CONFIG=str(config), EM_CACHE=str(cache), PYTHONDONTWRITEBYTECODE="1")
    compiler = [sys.executable, str(sdk / "emscripten/em++.py")]
    sources = ["tests/test_gpu_authored_shapes.cpp", "src/physics/gpu/gpu_authored_shapes.cpp",
               "tests/test_authored_body_frame.cpp", "src/physics/authored_body_frame.cpp",
               "src/physics/authored_shape.cpp", "src/physics/authored_shape_pool.cpp",
               "src/physics/rigid_mass_frame.cpp", "src/geometry/box_union.cpp",
               "src/gpu/resources.cpp", "src/core/log.cpp", "src/physics/physics_types.cpp",
               "src/physics/character/cpu_capsule_mover.cpp", "src/terrain/mip_generator.cpp"]
    sources += ["src/physics/gpu/" + name + ".cpp" for name in [
        "gpu_physics_backend", "debug_readback_ring", "deterministic_primitives",
        "gpu_attachments", "gpu_broad_phase", "gpu_buffer_arena", "gpu_ccd",
        "gpu_dynamic_solver", "gpu_event_readback", "gpu_islands", "gpu_narrow_phase", "gpu_queries"]]
    gtest = root / "third_party/googletest/googletest"
    inputs = [root / p for p in sources]
    inputs += [p for folder in ["src/physics", "src/physics/gpu", "src/physics/character", "src/terrain", "src/geometry", "src/gpu", "src/core"]
               for p in (root / folder).glob("*.hpp")]
    inputs += [p for p in (root / "third_party/glm/glm").rglob("*") if p.is_file()]
    inputs += [p for folder in [gtest / "src", gtest / "include"] for p in folder.rglob("*") if p.is_file()]
    inputs += sorted((root / "shaders").glob("*.wgsl"))
    inputs += [Path(__file__), root / "scripts/shape_diagnostics.html"]
    record = {"schema": 1, "status": "building", "work_directory": str(work), "commands": [],
              "purpose": "actual C++ authored shape geometry, checked sector/COM motion, mass, backend ownership and queue-lifetime tests; not authored-body activation or visual acceptance",
              "sources": {str(p.relative_to(root)): sha(p) for p in inputs},
              "settings": {"optimization": "O2", "exceptions": "JS", "asyncify": True,
                           "heap_bytes": 67108864, "stack_bytes": 1048576, "closure": False},
              "tools": {str(p): sha(p) for p in [args.node.resolve(), sdk / "bin/clang",
                        sdk / "bin/wasm-ld", sdk / "bin/wasm-opt", sdk / "emscripten/em++.py"]}}

    def save():
        (output / "build.json").write_text(json.dumps(record, indent=2) + "\n")

    def run(name, command):
        print(name, flush=True)
        with (output / f"{name}.log").open("wb") as log:
            result = subprocess.run(command, cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT)
        record["commands"].append({"name": name, "argv": command, "exit_code": result.returncode})
        save()
        if result.returncode:
            raise RuntimeError(f"{name} failed; see {output / (name + '.log')}")

    save()
    try:
        run("compiler-version", compiler + ["--version"])
        common = ["-std=c++20", "-O2", "-fexceptions", "-DVOXY_WASM=1",
                  "-DGLM_FORCE_DEPTH_ZERO_TO_ONE", "-DGLM_FORCE_LEFT_HANDED", "-Isrc",
                  "-isystem", "third_party/glm", "-isystem", str(gtest / "include"),
                  "--use-port=emdawnwebgpu"]
        warnings = ["-Wall", "-Wextra", "-Wpedantic", "-Wshadow", "-Wcast-align", "-Wunused",
                    "-Wconversion", "-Wsign-conversion", "-Wnull-dereference", "-Wdouble-promotion",
                    "-Wformat=2", "-Wno-missing-field-initializers", "-Werror",
                    # Existing physics_types.hpp defaulted comparison of GLM
                    # vectors is deleted by Clang; unrelated to shape resource tests.
                    "-Wno-defaulted-function-deleted"]
        objects = []
        for source in sources + [str(gtest / "src/gtest-all.cc"), str(gtest / "src/gtest_main.cc")]:
            obj = work / (Path(source).stem + ".o")
            flags = warnings if not Path(source).is_absolute() else ["-I", str(gtest)]
            run("compile-" + Path(source).stem, compiler + common + flags + ["-c", source, "-o", str(obj)])
            objects.append(str(obj))
        run("link", compiler + ["-O2", "-fexceptions", "--use-port=emdawnwebgpu",
            "-sENVIRONMENT=web", "-sMODULARIZE=1", "-sEXPORT_NAME=ShapeDiagnosticModule",
            "-sEXPORTED_RUNTIME_METHODS=['FS','callMain','ENV']", "-sEXIT_RUNTIME=1", "-sASYNCIFY=1",
            "-sALLOW_MEMORY_GROWTH=0", "-sINITIAL_MEMORY=67108864", "-sSTACK_SIZE=1048576",
            "-sSTACK_OVERFLOW_CHECK=2", "-sABORTING_MALLOC=0", "-sASSERTIONS=1",
            "--embed-file", "shaders", *objects, "-o", str(output / "shape_diagnostics.js")])
        shutil.copyfile(root / "scripts/shape_diagnostics.html", output / "index.html")
        record["artifacts"] = {p.name: {"sha256": sha(p), "bytes": p.stat().st_size}
                               for p in output.iterdir() if p.suffix in [".html", ".js", ".wasm"]}
        record["sources_unchanged"] = all(sha(root / p) == h for p, h in record["sources"].items())
        if not record["sources_unchanged"]:
            raise RuntimeError("source changed during compilation")
        record["status"] = "built; browser execution required"
    except Exception as error:
        record["status"] = "failed"
        record["failure"] = str(error)
        raise
    finally:
        save()


if __name__ == "__main__":
    main()
