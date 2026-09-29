"""Freeze the accepted native CPU closure, restore original queries, replay it."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path('/tmp/voxys-fps-20260929')
REPO = Path('/home/lschneid/workspace/schneiderlo/voxys')
OUT = ROOT / 'final-native-cpu'
OUT.mkdir(exist_ok=False)
CURRENT = ROOT / 'native-candidate'
ORIGINAL = ROOT / 'native-baseline'
CORE = 'libsrc_Sgame_Slibadventure_Ucore.so'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
sources = json.loads((ROOT / 'candidate-cpu-build.json').read_text())['sources']
for name, digest in sources.items():
    if name.endswith('spatial_queries.cpp'):
        assert (REPO / name).read_bytes() == (ORIGINAL / 'sources' / name).read_bytes()
    else:
        assert sha(REPO / name) == digest, name
shutil.copytree(CURRENT / '_solib_k8', OUT / '_solib_k8')
destination = OUT / '_solib_k8' / CORE
destination.chmod(0o644)
shutil.copy2(ORIGINAL / '_solib_k8' / CORE, destination)
executables = OUT / 'executables'
executables.mkdir()
EXE = executables / 'frontier_runtime_performance'
shutil.copy2(CURRENT / 'executables/frontier_runtime_performance', EXE)
for path in (OUT / '_solib_k8').iterdir():
    if path.name != CORE:
        assert sha(path) == sha(CURRENT / '_solib_k8' / path.name)
assert sha(destination) == sha(ORIGINAL / '_solib_k8' / CORE)
xdg = OUT / 'xdg'
xdg.mkdir(mode=0o700)
ICD = Path('/nix/store/ikgcxdw09g63rxb40w73m36474rds29c-mesa-26.0.0/share/vulkan/icd.d/lvp_icd.x86_64.json')
env = dict(os.environ, LD_BIND_NOW='1',
    LD_LIBRARY_PATH=str(OUT / '_solib_k8') + ':' + str(REPO / 'third_party/wgpu-native/dist/lib') + ':/nix/store/p571ddsdkd75dilqibr5ly79yb6v88n3-vulkan-loader-1.4.335.0/lib',
    WGPU_BACKEND='vulkan', VK_ICD_FILENAMES=str(ICD), VK_DRIVER_FILES=str(ICD),
    LP_NUM_THREADS='2', XDG_RUNTIME_DIR=str(xdg), BUILD_WORKSPACE_DIRECTORY=str(REPO),
    VOXY_ADVENTURE_TEST_WORKSPACE=str(REPO), VOXY_ADVENTURE_TEST_TERRAIN=str(ROOT / 'terrain.r16'))
for key in ('LD_PRELOAD', 'VOXY_FRONTIER_RUNTIME_ORACLE', 'VOXY_FRONTIER_RUNTIME_ORACLE_NEARBY',
            'VOXY_FRONTIER_QUERY_FIXTURE', 'VOXY_FRONTIER_HUD_BENCHMARK', 'VOXY_MESH_CAPTURE_DIR'):
    env.pop(key, None)
proof = {'scope': 'Final accepted CPU/native integration correctness only. Vulkan llvmpipe; no timing/FPS claim.',
    'startedUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'sources': {name: sha(REPO / name) for name in sources},
    'executable': {'path': str(EXE), 'sha256': sha(EXE)},
    'sharedLibraries': {path.name: sha(path) for path in sorted((OUT / '_solib_k8').iterdir())},
    'restoredCoreMatchesOriginal': True, 'allOtherLibrariesMatchCpuCandidate': True,
    'runs': []}
for label, test, nearby in (
    ('idle-hud', 'IdleHudReusesUploadsAndVisibleChangesRemainAuthoritative', False),
    ('default-oracle', 'DeterministicPublicReplayProducesExactStateAndHudOracle', False),
    ('nearby-oracle', 'DeterministicPublicReplayProducesExactStateAndHudOracle', True)):
    this_env = dict(env)
    if label == 'idle-hud':
        this_env['LD_DEBUG'] = 'libs'
    else:
        this_env['VOXY_FRONTIER_RUNTIME_ORACLE'] = str(OUT / label)
    if nearby:
        this_env['VOXY_FRONTIER_RUNTIME_ORACLE_NEARBY'] = '1'
    command = [str(EXE), '--gtest_filter=FrontierRuntimePerformance.' + test,
               '--gtest_output=xml:' + str(OUT / (label + '.xml'))]
    with (OUT / (label + '.log')).open('w') as stream:
        subprocess.run(command, cwd=REPO, env=this_env, stdout=stream,
                       stderr=subprocess.STDOUT, timeout=600, check=True)
    report = ET.parse(OUT / (label + '.xml')).getroot()
    assert int(report.attrib['tests']) == 1 and not list(report.iter('skipped')) and not list(report.iter('failure'))
    record = {'label': label, 'command': command, 'testsPassed': 1}
    if label == 'idle-hud':
        log = (OUT / (label + '.log')).read_text()
        expected = 'calling init: ' + str(OUT / '_solib_k8' / CORE)
        assert expected in log, 'Loader did not resolve the frozen restored core'
        record['originalCoreLoaderResolutionVerified'] = True
    else:
        subprocess.run(['python3', str(REPO / 'tools/benchmarks/compare_frontier_runtime.py'),
            str(ORIGINAL / label), str(OUT / label), '--output', str(OUT / (label + '-comparison.json'))],
            cwd=REPO, check=True, stdout=subprocess.DEVNULL)
        record['comparison'] = json.loads((OUT / (label + '-comparison.json')).read_text())
    proof['runs'].append(record)
    (OUT / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n')
    print('FINAL_NATIVE_EXACT ' + label, flush=True)
proof['finishedUtc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
proof['outputs'] = {path.name: sha(path) for path in OUT.iterdir() if path.is_file() and path.name != 'proof.json'}
(OUT / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n')
