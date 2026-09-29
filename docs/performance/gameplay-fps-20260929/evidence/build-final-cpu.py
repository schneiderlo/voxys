"""Relink the proven CPU closure with the exact original query object and flags."""
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
ORIGINAL = ROOT / 'symbol-baseline/inputs'
OUT = ROOT / 'final-cpu-build'
OUT.mkdir(exist_ok=False)
PREFIX = 'bazel-out/wasm-opt-ST-87cf4af9f873/bin/'
ARCHIVE = PREFIX + 'src/game/libadventure_core.a'
SDK = INPUTS / 'external/emsdk++emscripten_deps+emscripten_bin_linux'
AR = SDK / 'bin/llvm-ar'
PYTHON = INPUTS / 'external/rules_python++python+python_3_11_x86_64-unknown-linux-gnu/bin/python3'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
provenance = json.loads((FROZEN / 'direct-provenance.json').read_text())
assert provenance['baselineExact'] and provenance['baselineDataExact']
sources = json.loads((ROOT / 'candidate-cpu-build.json').read_text())['sources']
for name, digest in sources.items():
    if name.endswith('spatial_queries.cpp'):
        assert (REPO / name).read_bytes() == (ROOT / 'baseline-sources/spatial_queries.cpp').read_bytes()
    else:
        assert sha(REPO / name) == digest, name
for entry in provenance['frozenInputs']:
    assert sha(INPUTS / entry['path']) == entry['sha256'], entry['path']
for group in provenance['frozenPreloads']:
    for entry in group['files']:
        assert sha(INPUTS / entry['path']) == entry['sha256'], entry['path']

before = INPUTS / ARCHIVE
original = ORIGINAL / ARCHIVE
replacement = OUT / 'libadventure_core.a'
shutil.copy2(before, replacement)
replacement.chmod(0o644)
member = 'spatial_queries.o'
object_file = OUT / member
object_file.write_bytes(subprocess.check_output([str(AR), 'p', str(original), member]))
subprocess.run([str(AR), 'r', str(replacement), str(object_file)], check=True)
subprocess.run([str(AR), 's', str(replacement)], check=True)
members_before = subprocess.check_output([str(AR), 't', str(before)], text=True).splitlines()
members_after = subprocess.check_output([str(AR), 't', str(replacement)], text=True).splitlines()
assert members_before == members_after
member_proof = []
for name in members_before:
    old = subprocess.check_output([str(AR), 'p', str(before), name])
    new = subprocess.check_output([str(AR), 'p', str(replacement), name])
    if name == member:
        assert new == object_file.read_bytes()
    else:
        assert old == new, name
    member_proof.append({'member': name, 'beforeSha256': hashlib.sha256(old).hexdigest(),
                         'afterSha256': hashlib.sha256(new).hexdigest(), 'changed': old != new})
assert sum(item['changed'] for item in member_proof) == 1
args = (FROZEN / 'original-link.params').read_text().splitlines()
args = [str(replacement) if arg == ARCHIVE else arg for arg in args]
artifacts = OUT / 'artifacts'
artifacts.mkdir()
args[args.index('-o') + 1] = str(artifacts / 'voxy_wasm_cc.js')
args.append('--emit-symbol-map')
assert args.count('-O2') == 1
assert not any(arg in args for arg in ('-msimd128', '-mrelaxed-simd', '-ffast-math'))
(OUT / 'link.params').write_text('\n'.join(args) + '\n')
env = dict(os.environ, ROOT_DIR=str(INPUTS),
    EM_BIN_PATH='external/emsdk++emscripten_deps+emscripten_bin_linux',
    EM_CONFIG_PATH='external/emsdk++emscripten_cache+emscripten_cache/emscripten_config',
    NODE_JS_PATH='external/rules_nodejs++node+nodejs_linux_amd64/bin/node',
    BAZEL_PYTHON_RELPATH='external/rules_python++python+python_3_11_x86_64-unknown-linux-gnu/bin/python3',
    EMSCRIPTEN=str(SDK / 'emscripten'),
    EM_CONFIG=str(INPUTS / 'external/emsdk++emscripten_cache+emscripten_cache/emscripten_config'),
    EMSDK_PYTHON=str(PYTHON))
record = {'scope': 'Final CPU build using original compiler flags. Original queries and all original shaders; HUD, attack-sight admission, paint cache and initial in-place record construction retained.',
    'startedUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'originalQueriesSourceSha256': sha(REPO / 'src/game/adventure/spatial_queries.cpp'),
    'acceptedSources': {name: sha(REPO / name) for name in sources},
    'frozenCpuClosureExactBeforeReplacement': True,
    'archiveMembers': member_proof,
    'originalCoreArchiveSha256': sha(original),
    'replacementArchiveSha256': sha(replacement),
    'command': [str(PYTHON), str(SDK / 'emscripten/em++.py'), *args],
    'toolchainConfigSha256': sha(Path(env['EM_CONFIG'])),
    'nativeOrWasmFpsClaim': False}
(OUT / 'proof.json').write_text(json.dumps(record, indent=2) + '\n')
with (OUT / 'link.log').open('w') as log:
    subprocess.run(record['command'], cwd=INPUTS, env=env, stdout=log,
                   stderr=subprocess.STDOUT, timeout=600, check=True)
record['artifacts'] = {ext: {'sha256': sha(artifacts / ('voxy_wasm_cc.' + ext)),
    'bytes': (artifacts / ('voxy_wasm_cc.' + ext)).stat().st_size} for ext in ('wasm', 'js', 'data')}
expected = json.loads((ROOT / 'candidate-cpu-build.json').read_text())['binaries']
assert record['artifacts']['data']['sha256'] == expected['voxy_wasm_cc.data']
original_glue = (ROOT / 'candidate-cpu-site/voxy_wasm_cc.js').read_text()
final_glue = (artifacts / 'voxy_wasm_cc.js').read_text()
original_data_path = PREFIX + 'voxy_wasm_cc.data'
final_data_path = str(artifacts / 'voxy_wasm_cc.data')
assert original_glue.count(original_data_path) == final_glue.count(final_data_path) == 3
assert original_glue.startswith('#!/usr/bin/env node\n') and not final_glue.startswith('#!')
normalized = '#!/usr/bin/env node\n' + final_glue.replace(final_data_path, original_data_path)
assert normalized == original_glue, 'Unexpected generated loader change'
record['assetsExact'] = True
record['glueRawBytesExact'] = False
record['glueReview'] = {'normalizedBytesExact': True, 'originalNodeShebangAbsentInFinal': True,
    'originalMetadataDataPath': original_data_path, 'finalMetadataDataPath': final_data_path,
    'metadataOccurrences': 3, 'unchangedRemotePackageBase': 'voxy_wasm_cc.data',
    'generatedGlueLeftUnmodified': True}
record['finishedUtc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
(OUT / 'proof.json').write_text(json.dumps(record, indent=2) + '\n')
print(json.dumps(record['artifacts'], indent=2), flush=True)
