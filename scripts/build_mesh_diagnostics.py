#!/usr/bin/env python3
"""Build the real MeshPath C++ pixel tests for browser WebGPU; preserve evidence."""
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
    parser.add_argument("--kind", choices=["mesh", "environment", "mesh-environment", "mesh-opaque"], default="mesh")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    sdk, output = args.sdk.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    work = Path(tempfile.mkdtemp(prefix="voxys-mesh-diagnostic-build-"))
    cache = work / "cache"
    subprocess.run(["cp", "-a", "--reflink=auto", str(sdk / "emscripten/cache"), str(cache)], check=True)
    config = work / ".emscripten"
    config.write_text(f"LLVM_ROOT = {str(sdk / 'bin')!r}\nBINARYEN_ROOT = {str(sdk)!r}\n"
                      f"NODE_JS = {str(args.node.resolve())!r}\nFROZEN_CACHE = True\n")
    env = dict(os.environ, EM_CONFIG=str(config), EM_CACHE=str(cache), PYTHONDONTWRITEBYTECODE="1")
    compiler = [sys.executable, str(sdk / "emscripten/em++.py")]
    sources = ["tests/test_mesh_path.cpp", "src/render/mesh_path.cpp", "src/render/environment_lighting.cpp", "src/render/opaque_scene.cpp", "src/gpu/resources.cpp",
               "src/moto/vmesh_io.cpp", "src/core/log.cpp"]
    shaders = ["shaders/mesh_path.wgsl", "shaders/environment_lighting.wgsl", "shaders/opaque_scene.wgsl"]
    page = "scripts/mesh_diagnostics.html"
    if args.kind == "environment":
        sources = ["tests/test_environment_lighting.cpp", "src/render/environment_lighting.cpp",
                   "src/gpu/resources.cpp", "src/core/log.cpp"]
        shaders = ["shaders/environment_lighting.wgsl"]
        page = "scripts/environment_diagnostics.html"
    elif args.kind == "mesh-environment":
        page = "scripts/mesh_environment_diagnostics.html"
    elif args.kind == "mesh-opaque":
        page = "scripts/mesh_opaque_diagnostics.html"
    gtest = root / "third_party/googletest/googletest"
    inputs = [root / p for p in sources]
    inputs += [p for folder in ["src/render", "src/gpu", "src/moto", "src/core"]
               for p in (root / folder).glob("*.hpp")]
    inputs += [p for p in (root / "third_party/glm/glm").rglob("*") if p.is_file()]
    inputs += [p for folder in [gtest / "src", gtest / "include"] for p in folder.rglob("*") if p.is_file()]
    inputs += [root / shader for shader in shaders] + [Path(__file__), root / page]
    record = {"schema": 1, "status": "building", "work_directory": str(work), "commands": [],
              "purpose": f"actual C++ {args.kind} GPU tests; correctness only, not performance or final art acceptance",
              "kind": args.kind,
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
                    # vectors is deleted by Clang; unrelated to material tests.
                    "-Wno-defaulted-function-deleted"]
        objects = []
        for source in sources + [str(gtest / "src/gtest-all.cc"), str(gtest / "src/gtest_main.cc")]:
            obj = work / (Path(source).stem + ".o")
            flags = warnings if not Path(source).is_absolute() else ["-I", str(gtest)]
            run("compile-" + Path(source).stem, compiler + common + flags + ["-c", source, "-o", str(obj)])
            objects.append(str(obj))
        run("link", compiler + ["-O2", "-fexceptions", "--use-port=emdawnwebgpu",
            "-sENVIRONMENT=web", "-sMODULARIZE=1", "-sEXPORT_NAME=MeshDiagnosticModule",
            "-sEXPORTED_RUNTIME_METHODS=['FS','callMain','ENV']", "-sEXIT_RUNTIME=1", "-sASYNCIFY=1",
            "-sALLOW_MEMORY_GROWTH=0", "-sINITIAL_MEMORY=67108864", "-sSTACK_SIZE=1048576",
            "-sSTACK_OVERFLOW_CHECK=2", "-sABORTING_MALLOC=0", "-sASSERTIONS=1",
            *[flag for shader in shaders for flag in ["--embed-file", shader]],
            *objects, "-o", str(output / "mesh_diagnostics.js")])
        shutil.copyfile(root / page, output / "index.html")
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
