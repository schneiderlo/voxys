"""Isolated paired native correctness builds. No Bazel/cache/production writes."""
from pathlib import Path
import ast, hashlib, json, os, shutil, subprocess, concurrent.futures, sys

REPO = Path('/home/lschneid/workspace/schneiderlo/voxys')
PROTOTYPE = Path(__file__).parent
OUT = Path('/tmp/voxys-fps-20260929/cove-actual-cache-native')
OUT.mkdir(exist_ok=True)
CXX = '/nix/store/a245z3cvf9x9sn0xlk6k8j9xhxbhda1z-gcc-wrapper-15.2.0/bin/g++'
sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()

# These are reconstructed from the checked-in fastbuild configuration, not a
# retained Bazel compile argv. Both variants are compiled with exactly these.
copts = (REPO / 'settings/copts.bzl').read_text()
def list_value(name):
    start = copts.index(name + ' = [') + len(name + ' = ')
    end = copts.index('\n]', start) + 2
    return ast.literal_eval(copts[start:end])
flags = ['-std=c++20', *list_value('CLANG_WARNINGS'),
         '-Wmisleading-indentation', '-Wduplicated-cond', '-Wduplicated-branches',
         '-Wlogical-op', '-Wuseless-cast', '-Werror', '-fPIC',
         '-Wno-error=useless-cast',
         '-DGLM_FORCE_DEPTH_ZERO_TO_ONE', '-DGLM_FORCE_LEFT_HANDED',
         '-DGLM_ENABLE_EXPERIMENTAL']

libs = OUT / 'solibs'
libs.mkdir(exist_ok=True)
for source_root in [Path('/tmp/voxys-fps-20260929/mesh-inplace/solibs'),
                    Path('/tmp/voxys-fps-20260929/final-native-cpu/_solib_k8')]:
    for path in source_root.rglob('*'):
        if path.is_file():
            dest = libs / path.relative_to(source_root)
            dest.parent.mkdir(parents=True, exist_ok=True)
            if dest.exists():
                dest.unlink()
            shutil.copy2(path, dest)
assert sha(libs / 'libsrc_Sgame_Slibadventure_Ucore.so') == 'ec3f0399abda6af8aac6b1353f33352f7e1970569230cb95c3e073e075316141'
assert sha(libs / 'libsrc_Srender_Slibrender.so') == 'b7177a37a85617188397f72352d4688829f387a7777e485d8a273aca0394e4ef'
include_root = OUT / 'shared-include'
for path in (REPO / 'src').rglob('*'):
    if path.is_file() and path.suffix in ['.hpp', '.h', '.inc']:
        target = include_root / path.relative_to(REPO / 'src')
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)

original_link = (REPO / 'bazel-out/k8-fastbuild/bin/tests/mesh_path_test-0.params').read_text().splitlines()
guide_source = REPO / 'bazel-out/k8-fastbuild/bin/src/render/_objs/salvage_asset_fixture/inspection_guides.pic.o'
guide_object = OUT / 'frozen-inspection-guides.o'
if guide_object.exists():
    guide_object.unlink()
shutil.copy2(guide_source, guide_object)
identities = json.loads((PROTOTYPE / 'source-identities.json').read_text())['files']
proof = {'scope': 'Native llvmpipe pipeline/lifecycle/full-frame correctness only. No FPS claim.',
         'compiler': {'path': CXX, 'sha256': sha(CXX),
                      'version': subprocess.check_output([CXX, '--version'], text=True).splitlines()[0]},
         'compileFlagsReconstructed': True,
         'documentedWarningException': 'Existing gpu/resources.hpp::saturatingSize LP64 same-type cast triggers -Wuseless-cast on both original and candidate. Keep diagnostic visible but nonfatal on both; do not mutate production.',
         'reconstruction': 'Native fastbuild (no explicit optimization), PROJECT_DEFAULT_COPTS/PROJECT_TEST_COPTS and .bazelrc GLM definitions; exact original compile argv unavailable.',
         'flags': flags,
         'linkClosure': 'Current mesh_path_test-0.params + unchanged frozen inspection_guides.pic.o; old blit_path-0.params omitted current MeshPath/water dependency closure.',
         'frozenGuideObject': {'source': str(guide_source.resolve()), 'sha256': sha(guide_object)},
         'frozenLibraries': {str(p.relative_to(libs)): sha(p) for p in libs.rglob('*') if p.is_file()},
         'sources': {}, 'compileCommands': [], 'executables': {}}
commands = []
for label in ['baseline', 'candidate']:
    variant = OUT / label
    variant.mkdir(exist_ok=True)
    shutil.copytree(REPO / 'shaders', variant / 'shaders', dirs_exist_ok=True)
    if not (variant / 'data').exists():
        (variant / 'data').symlink_to(REPO / 'data', target_is_directory=True)
    for name in ['src/render/blit_path.cpp', 'src/render/blit_path.hpp',
                 'src/render/environment_lighting.cpp', 'shaders/ray_blit.wgsl',
                 'tests/test_blit_path.cpp']:
        source = PROTOTYPE / label / name
        if name in identities:
            assert sha(source) == identities[name][label + 'Sha256'], name
        target = variant / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        proof['sources'][str(target.relative_to(OUT))] = sha(target)
    includes = ['-I' + str(variant / 'src'), '-I' + str(include_root)]
    for directory in ['third_party/glm', 'third_party/wgpu-native/dist/include/webgpu',
                      'third_party/stb', 'third_party/glfw/include', 'third_party/JoltPhysics',
                      'third_party/box3d/include', 'third_party/tinygltf']:
        includes += ['-isystem', str(REPO / directory)]
    includes += ['-isystem', '/home/lschneid/.cache/bazel/_bazel_lschneid/32316bdb2f5c1949f03106d5cf38a916/external/googletest+/googletest/include']
    for name in ['blit_path', 'environment_lighting', 'test_blit_path']:
        test = name.startswith('test_')
        source = variant / ('tests' if test else 'src/render') / (name + '.cpp')
        output = variant / (name + '.o')
        command = [CXX, *flags, *( ['-DVOXY_NATIVE=1'] if test else []), *includes,
                   '-c', str(source), '-o', str(output)]
        if '--reuse-render-objects' not in sys.argv or test or not output.exists():
            commands.append((label, name, command))
        else:
            # This retry follows a successful six-TU compile whose subsequent
            # link failed on stale dependency params. Rendering sources/flags
            # are identical; only newer test-only capture assertions changed.
            proof.setdefault('reusedSuccessfulRenderObjects', {})[str(output)] = sha(output)
        proof['compileCommands'].append(command)
    params = []
    index = 0
    while index < len(original_link):
        arg = original_link[index]
        if arg == '-o':
            params += ['-o', str(variant / 'blit-tests')]
            index += 2
            continue
        if arg == 'bazel-out/k8-fastbuild/bin/tests/_objs/mesh_path_test/test_mesh_path.pic.o':
            params += [str(variant / (name + '.o')) for name in ['test_blit_path', 'blit_path', 'environment_lighting']]
            params.append(str(guide_object))
        elif arg.startswith('-Lbazel-out/k8-fastbuild/bin/_solib_k8'):
            params.append('-L' + str(libs) + arg[len('-Lbazel-out/k8-fastbuild/bin/_solib_k8'):])
        else:
            params.append(arg)
        index += 1
    (variant / 'link.params').write_text('\n'.join(params) + '\n')

def compile_one(task):
    label, name, command = task
    result = subprocess.run(command, cwd=REPO, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (OUT / label / (name + '-compile.log')).write_text(result.stdout)
    print(label, name, 'exit', result.returncode, flush=True)
    if result.returncode:
        print(result.stdout, flush=True)
    return result.returncode

with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
    statuses = list(pool.map(compile_one, commands))
if any(statuses):
    (OUT / 'build-proof.json').write_text(json.dumps(proof, indent=2) + '\n')
    raise SystemExit(1)
(OUT / 'build-proof.json').write_text(json.dumps(proof, indent=2) + '\n')
for label in ['baseline', 'candidate']:
    variant = OUT / label
    result = subprocess.run([CXX, '@' + str(variant / 'link.params')], cwd=REPO,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (variant / 'link.log').write_text(result.stdout)
    print(label, 'link exit', result.returncode, flush=True)
    if result.returncode:
        print(result.stdout[:2000], flush=True)
        raise SystemExit(result.returncode)
    proof['executables'][label] = {'path': str(variant / 'blit-tests'), 'sha256': sha(variant / 'blit-tests')}
    proof[label + 'objects'] = {name: sha(variant / (name + '.o')) for name in ['test_blit_path', 'blit_path', 'environment_lighting']}
(OUT / 'build-proof.json').write_text(json.dumps(proof, indent=2) + '\n')
print('PAIRED_NATIVE_BUILD_COMPLETE', flush=True)
