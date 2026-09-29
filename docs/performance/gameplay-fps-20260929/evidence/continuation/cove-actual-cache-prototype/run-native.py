"""Run only in an explicitly cleared correctness window; no timing/FPS claim."""
from pathlib import Path
import hashlib, json, os, subprocess, sys, xml.etree.ElementTree as ET

OUT = Path('/tmp/voxys-fps-20260929/cove-actual-cache-native')
sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
proof_path = OUT / 'build-proof.json'
proof = json.loads(proof_path.read_text())
assert all(label in proof['executables'] for label in ['baseline', 'candidate']), 'Build incomplete'
runtime = OUT / 'runtime'
runtime.mkdir(exist_ok=True, mode=0o700)
cache = OUT / 'mesa-cache'
cache.mkdir(exist_ok=True)
env = dict(os.environ)
env['LD_LIBRARY_PATH'] = ':'.join([
    str(OUT / 'solibs'),
    str(OUT / 'solibs/_U_S_Sthird_Uparty_Cwgpu___Uthird_Uparty_Swgpu-native_Sdist_Slib'),
    '/nix/store/p571ddsdkd75dilqibr5ly79yb6v88n3-vulkan-loader-1.4.335.0/lib',
    '/nix/store/ikgcxdw09g63rxb40w73m36474rds29c-mesa-26.0.0/lib',
    '/nix/store/hpdf5fwl5arkc8d625cxba604i8dwnvp-glfw-3.4/lib',
    '/nix/store/wb6rhpznjfczwlwx23zmdrrw74bayxw4-glibc-2.42-47/lib',
    '/nix/store/j2kgllgds4w7na8zqv1msi0mpvpjxda8-gcc-15.2.0-lib/lib'])
env['VK_DRIVER_FILES'] = '/nix/store/ikgcxdw09g63rxb40w73m36474rds29c-mesa-26.0.0/share/vulkan/icd.d/lvp_icd.x86_64.json'
env['VK_ICD_FILENAMES'] = env['VK_DRIVER_FILES']
env['WGPU_BACKEND'] = 'vulkan'
env['LP_NUM_THREADS'] = '4'
env['XDG_RUNTIME_DIR'] = str(runtime)
env['MESA_SHADER_CACHE_DIR'] = str(cache)
env['VOXY_CACHE_TEST_TIMESTAMPS'] = '1'

results = {'scope': 'Native Vulkan llvmpipe exact full-frame/lifecycle correctness only; all runtimes excluded from timing claims.',
           'buildProofSha256': sha(proof_path), 'runs': []}
def run(label, name, selection, bindings=False):
    variant = OUT / label
    exe = Path(proof['executables'][label]['path'])
    assert sha(exe) == proof['executables'][label]['sha256']
    run_env = dict(env)
    run_env['VOXY_CACHE_CAPTURE_DIR'] = str(variant / 'captures')
    if bindings:
        run_env['LD_DEBUG'] = 'bindings'
    command = [str(exe), '--gtest_filter=' + selection,
               '--gtest_output=xml:' + str(variant / (name + '.xml'))]
    log_path = variant / (name + '.log')
    print('START', label, name, flush=True)
    with log_path.open('w') as log:
        status = subprocess.run(command, cwd=variant, env=run_env,
                                stdout=log, stderr=subprocess.STDOUT, timeout=1800).returncode
    result = {'label': label, 'name': name, 'command': command, 'exitCode': status,
              'logSha256': sha(log_path)}
    if (variant / (name + '.xml')).exists():
        xml = ET.parse(variant / (name + '.xml')).getroot()
        result['tests'] = xml.get('tests')
        result['failures'] = xml.get('failures')
        result['skipped'] = xml.get('disabled')
        result['xmlSha256'] = sha(variant / (name + '.xml'))
    if bindings:
        lines = [line for line in log_path.read_text().splitlines()
                 if 'binding file' in line and str(exe) in line
                 and ('EnvironmentLighting' in line or 'BlitPath' in line)]
        (variant / 'owned-bindings.txt').write_text('\n'.join(lines) + '\n')
        result['ownedDsoBindingLines'] = len(lines)
    results['runs'].append(result)
    (OUT / 'native-results.json').write_text(json.dumps(results, indent=2) + '\n')
    print('FINISH', label, name, 'exit', status, flush=True)
    if status:
        print(log_path.read_text()[-8000:], flush=True)
        raise SystemExit(status)

run('candidate', 'lifecycle', 'BlitPathTest.CoveCacheTracksRebakesDiscardAndLegacyTransitions', True)
run('baseline', 'existing-suite', 'BlitPathTest.*')
run('candidate', 'existing-suite', 'BlitPathTest.*-BlitPathTest.CoveCacheTracksRebakesDiscardAndLegacyTransitions')
results['allRunsPassed'] = True
(OUT / 'native-results.json').write_text(json.dumps(results, indent=2) + '\n')
print('NATIVE_LIFECYCLE_SUITE_COMPLETE', flush=True)
