"""Build an isolated actual-Cove cache candidate from the proven CPU closure.

Only three archive members and the ray shader preload may change. Production
sources, original query code, compiler policy, and frozen inputs are untouched.
"""
from pathlib import Path
import datetime
import hashlib
import json
import os
import shutil
import subprocess

ROOT = Path('/tmp/voxys-fps-20260929')
REPO = Path('/home/lschneid/workspace/schneiderlo/voxys')
FROZEN = ROOT / 'wasm-simd-probe/game-build'
INPUTS = FROZEN / 'inputs'
LIVE = Path('/home/lschneid/.cache/bazel/_bazel_lschneid/32316bdb2f5c1949f03106d5cf38a916/execroot/_main')
PROTO = ROOT / 'cove-actual-cache-prototype'
OUT = ROOT / 'cove-cache-wasm-build'
OUT.mkdir(exist_ok=False)
PREFIX = 'bazel-out/wasm-opt-ST-87cf4af9f873/bin/'
SDK = INPUTS / 'external/emsdk++emscripten_deps+emscripten_bin_linux'
PYTHON = INPUTS / 'external/rules_python++python+python_3_11_x86_64-unknown-linux-gnu/bin/python3'
AR = SDK / 'bin/llvm-ar'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
record = {'scope': 'Isolated unpromoted actual-Cove lighting cache; no FPS claim',
          'startedUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
          'commands': [], 'sources': {}, 'archiveProof': []}
def save():
    (OUT / 'proof.json').write_text(json.dumps(record, indent=2) + '\n')

provenance = json.loads((FROZEN / 'direct-provenance.json').read_text())
assert provenance['baselineExact'] and provenance['baselineDataExact']
for entry in provenance['frozenInputs']:
    assert sha(INPUTS / entry['path']) == entry['sha256'], entry['path']
for group in provenance['frozenPreloads']:
    for entry in group['files']:
        assert sha(INPUTS / entry['path']) == entry['sha256'], entry['path']
final_proof = json.loads((ROOT / 'final-cpu-build/proof.json').read_text())
assert final_proof['assetsExact'] and final_proof['glueReview']['normalizedBytesExact']
assert (REPO / 'src/game/adventure/spatial_queries.cpp').read_bytes() == (ROOT / 'baseline-sources/spatial_queries.cpp').read_bytes()
record['originalQueriesSha256'] = sha(REPO / 'src/game/adventure/spatial_queries.cpp')

# Freeze reviewed CPU/shader files and the unchanged full-type allocator.
names = ['src/render/blit_path.cpp', 'src/render/blit_path.hpp',
         'src/render/environment_lighting.cpp', 'shaders/ray_blit.wgsl']
for name in names + ['src/app/application.cpp']:
    source = PROTO / 'candidate' / name if name in names else REPO / name
    target = OUT / 'sources' / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    record['sources'][name] = {'source': str(source), 'sha256': sha(target)}
scalar_proof = json.loads((ROOT / 'baseline-scalar-build.json').read_text())
assert record['sources']['src/app/application.cpp']['sha256'] == scalar_proof['sourceHashes']['src/app/application.cpp']
shutil.copytree(INPUTS / 'shaders', OUT / 'shaders')
shutil.copy2(OUT / 'sources/shaders/ray_blit.wgsl', OUT / 'shaders/ray_blit.wgsl')
record['shaderPreloadProof'] = []
for source in sorted((INPUTS / 'shaders').rglob('*')):
    if not source.is_file():
        continue
    relative = source.relative_to(INPUTS / 'shaders')
    target = OUT / 'shaders' / relative
    before, after = sha(source), sha(target)
    if str(relative) != 'ray_blit.wgsl':
        assert before == after, str(relative)
    record['shaderPreloadProof'].append({'path': str(relative), 'beforeSha256': before,
                                       'afterSha256': after, 'changed': before != after})
assert sum(row['changed'] for row in record['shaderPreloadProof']) == 1

env = dict(os.environ, ROOT_DIR=str(INPUTS),
    EM_BIN_PATH='external/emsdk++emscripten_deps+emscripten_bin_linux',
    EM_CONFIG_PATH='external/emsdk++emscripten_cache+emscripten_cache/emscripten_config',
    NODE_JS_PATH='external/rules_nodejs++node+nodejs_linux_amd64/bin/node',
    BAZEL_PYTHON_RELPATH='external/rules_python++python+python_3_11_x86_64-unknown-linux-gnu/bin/python3',
    EMSCRIPTEN=str(SDK / 'emscripten'),
    EM_CONFIG=str(INPUTS / 'external/emsdk++emscripten_cache+emscripten_cache/emscripten_config'),
    EMSDK_PYTHON=str(PYTHON))
def run(command, label, cwd=INPUTS):
    command = list(map(str, command))
    print('RUN ' + label, flush=True)
    record['commands'].append({'label': label, 'argv': command, 'cwd': str(cwd)})
    save()
    with (OUT / (label + '.log')).open('w') as log:
        subprocess.run(command, cwd=cwd, env=env, stdout=log,
                       stderr=subprocess.STDOUT, timeout=600, check=True)

compiled = {}
for source, object_path, archive_path in [
    ('src/render/blit_path.cpp', 'src/render/_objs/render/blit_path.o', 'src/render/librender.a'),
    ('src/render/environment_lighting.cpp', 'src/render/_objs/render/environment_lighting.o', 'src/render/librender.a'),
    ('src/app/application.cpp', 'src/app/_objs/app/application.o', 'src/app/libapp.a')]:
    captured = (LIVE / (PREFIX + object_path + '.params')).read_text().splitlines()
    label = Path(source).stem
    (OUT / (label + '-captured.params')).write_text('\n'.join(captured) + '\n')
    common, i = [], 0
    while i < len(captured):
        arg = captured[i]
        if arg in ('-MF', '-o'):
            i += 2
            continue
        if arg in ('-MD', '-c') or arg == source or arg.startswith(('-D__DATE__', '-D__TIME__', '-D__TIMESTAMP__')):
            i += 1
            continue
        if arg.startswith('--sysroot=external/'):
            arg = '--sysroot=' + str(INPUTS / arg[len('--sysroot='):])
        elif arg.startswith('external/'):
            arg = str(INPUTS / arg)
        common.append(arg)
        i += 1
    assert '-O2' in common and '-fexceptions' in common
    assert not any(arg in common for arg in ('-msimd128', '-mrelaxed-simd', '-ffast-math'))
    output = OUT / 'objects' / Path(object_path).name
    output.parent.mkdir(exist_ok=True)
    args = ['-I' + str(OUT / 'sources/src'), *common, '-MD', '-MF', str(output.with_suffix('.d')),
            '-c', str(OUT / 'sources' / source), '-o', str(output)]
    (OUT / (label + '-compile.params')).write_text('\n'.join(args) + '\n')
    run([PYTHON, SDK / 'emscripten/em++.py', *args], 'compile-' + label, REPO)
    dependencies = output.with_suffix('.d').read_text()
    if source != 'src/render/environment_lighting.cpp':
        assert str(OUT / 'sources/src/render/blit_path.hpp') in dependencies, label
    compiled.setdefault(archive_path, []).append(output)

replacements = {PREFIX + 'src/game/libadventure_core.a': str(ROOT / 'final-cpu-build/libadventure_core.a')}
for archive_path, objects in compiled.items():
    before = INPUTS / (PREFIX + archive_path)
    after = OUT / Path(archive_path).name
    shutil.copy2(before, after)
    after.chmod(0o644)
    run([AR, 'r', after, *objects], 'replace-' + after.stem)
    run([AR, 's', after], 'index-' + after.stem)
    members_before = subprocess.check_output([str(AR), 't', str(before)], text=True).splitlines()
    assert members_before == subprocess.check_output([str(AR), 't', str(after)], text=True).splitlines()
    expected = {obj.name for obj in objects}
    for member in members_before:
        old = subprocess.check_output([str(AR), 'p', str(before), member])
        new = subprocess.check_output([str(AR), 'p', str(after), member])
        if member not in expected:
            assert old == new, member
        else:
            assert new == next(obj for obj in objects if obj.name == member).read_bytes()
        record['archiveProof'].append({'archive': archive_path, 'member': member,
            'beforeSha256': hashlib.sha256(old).hexdigest(), 'afterSha256': hashlib.sha256(new).hexdigest(),
            'allowedReplacement': member in expected})
    replacements[PREFIX + archive_path] = str(after)

args = [replacements.get(arg, arg) for arg in (FROZEN / 'original-link.params').read_text().splitlines()]
assert args.count('shaders@/shaders') == 1
args[args.index('shaders@/shaders')] = str(OUT / 'shaders') + '@/shaders'
artifacts = OUT / 'artifacts'
artifacts.mkdir()
args[args.index('-o') + 1] = str(artifacts / 'voxy_wasm_cc.js')
args.append('--emit-symbol-map')
assert args.count('-O2') == 1
assert not any(arg in args for arg in ('-msimd128', '-mrelaxed-simd', '-ffast-math'))
(OUT / 'link.params').write_text('\n'.join(args) + '\n')
run([PYTHON, SDK / 'emscripten/em++.py', *args], 'link')
record['artifacts'] = {ext: {'sha256': sha(artifacts / ('voxy_wasm_cc.' + ext)),
    'bytes': (artifacts / ('voxy_wasm_cc.' + ext)).stat().st_size} for ext in ('wasm', 'js', 'data')}
record['finishedUtc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
save()
print(json.dumps(record['artifacts'], indent=2), flush=True)
