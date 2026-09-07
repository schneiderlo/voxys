# BOOT-02: native runtime file freshness

Status: isolated staging checks and actual incremental native builds passed.
Root's full-build checks are recorded below; this report does not claim a
gameplay-performance pass.

## Change

The native shader copy previously ran only after executable linking. Editing
WGSL alone left the staged shader stale. Native launch configs were not staged.

`CMakeLists.txt` now gives `voxy_native` an always-run
`voxy_native_runtime_files` dependency. It copies the shader directory and the
four current launch configs (`voxy.cfg`, `ridgebreak.cfg`, `lego_shore.cfg`,
`lego_world.cfg`) into the executable's directory. Config copies use
`copy_if_different`. Quoted paths and `VERBATIM` handle spaces. Target-relative
paths place these files beside each configuration's executable.

The existing optional `assets/` and `data/` POST_BUILD blocks are unchanged.
No source files, test registrations, Bazel files, or Makefile were changed by
this task. Add `salvage.cfg` to the explicit config list when BOOT-05 introduces it.

## Evidence

- Checked at `2026-09-07T21:32:16Z` against baseline HEAD
  `7563f61fd536df7de5d209ff35d3cd2099ebcbb6` plus this uncommitted CMake change.
- Tools: cached CMake 4.1.2, Ninja 1.13.2, GCC wrapper 15.2.0. The fixture uses
  `cmake_minimum_required(VERSION 3.20)`; a CMake 3.20 binary was not run.
- CMake's installed CMP0112 documentation confirms `TARGET_FILE_DIR` has no
  reverse target dependency under the policy introduced in 3.19. The repository
  minimum of 3.20 selects that behavior, avoiding a target dependency cycle.
- Exact native runtime-copy block, including the unchanged assets/data blocks,
  was extracted from the working `CMakeLists.txt` into a minimal compiled fixture.
- Extracted block SHA-256:
  `37544a405e9ba5d4548e6479f2c358b330761549985380a6ab466aef99cdb41d`.
- Fixture source and build paths contained spaces.
- Raw command log and result: `/tmp/voxys native assets n853byxo/report.json`.
  This temporary artifact is local evidence; the reproduction below recreates it.
- No CMake or Ninja command targeted `build-salvage-native` during this task.
  Its cache was read only to locate the already provisioned tools.

| Check | Result |
| --- | --- |
| Initial shader/config copies; original assets/data POST_BUILD copies | Passed |
| No-op build preserves executable and unchanged config hash and timestamp | Passed |
| Shader-only change copied without executable relink | Passed |
| Config-only change copied without executable relink | Passed |
| Newly added shader copied without reconfiguration | Passed |
| Deleted staged shader/config restored without relink | Passed |
| Missing required source config makes the build fail | Passed |
| Ninja Multi-Config Debug and Release use their executable directories | Passed |
| Repeated Debug/Release builds do not relink unchanged executables | Passed |

## Reproduce the isolated check

Run from the repository root after `build-salvage-native/CMakeCache.txt` exists.
The script reads tool paths from that cache, creates its own temporary project,
and never configures or builds the real native build tree. It compiles a trivial
executable using the exact runtime-copy block from the current checkout.

```bash
python3 - <<'PY'
from pathlib import Path
import hashlib
import json
import re
import subprocess
import tempfile

root = Path.cwd()
cache = (root / 'build-salvage-native/CMakeCache.txt').read_text()
def cached(name):
    return re.search(r'^' + name + r':[^=]+=(.+)$', cache, re.M).group(1)
cmake = cached('CMAKE_COMMAND')
ninja = cached('CMAKE_MAKE_PROGRAM')
compiler = cached('CMAKE_CXX_COMPILER')
fixture = Path(tempfile.mkdtemp(prefix='voxys native assets '))
source = fixture / 'source tree'
source.mkdir()
original = (root / 'CMakeLists.txt').read_text()
start = original.index('        # Runtime files must refresh')
end = original.index('        # Apply compiler warnings', start)
block = original[start:end]
(source / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(native_assets_fixture LANGUAGES CXX)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_executable(voxy_native main.cpp)
''' + block)
(source / 'main.cpp').write_text('int main() { return 0; }\n')
for relative in ('shaders/nested/test.wgsl', 'assets/fixture.txt', 'data/fixture.txt'):
    target = source / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text('original ' + relative + '\n')
configs = ('voxy.cfg', 'ridgebreak.cfg', 'lego_shore.cfg', 'lego_world.cfg')
for name in configs:
    (source / name).write_text('[window]\ntitle = "' + name + '"\n')
log = []
def run(*args, succeeds=True):
    result = subprocess.run([str(arg) for arg in args], text=True, capture_output=True)
    log.append({'args': [str(arg) for arg in args], 'code': result.returncode,
                'stdout': result.stdout, 'stderr': result.stderr})
    if (result.returncode == 0) != succeeds:
        raise RuntimeError(json.dumps(log[-1], indent=2))
    return result

def configure(build, generator):
    run(cmake, '-S', source, '-B', build, '-G', generator,
        '-DCMAKE_MAKE_PROGRAM=' + ninja, '-DCMAKE_CXX_COMPILER=' + compiler)

def build(build_dir, config=None, succeeds=True):
    args = [cmake, '--build', build_dir, '--target', 'voxy_native']
    if config:
        args += ['--config', config]
    return run(*args, succeeds=succeeds)

def signature(path):
    return (hashlib.sha256(path.read_bytes()).hexdigest(), path.stat().st_mtime_ns)

def assert_copies(output):
    for relative in (*configs, 'shaders/nested/test.wgsl'):
        assert (output / relative).read_bytes() == (source / relative).read_bytes(), relative

try:
    single = fixture / 'single build'
    configure(single, 'Ninja')
    build(single)
    output = single / 'bin'
    assert_copies(output)
    for relative in ('assets/fixture.txt', 'data/fixture.txt'):
        assert (output / relative).read_bytes() == (source / relative).read_bytes()
    executable = signature(output / 'voxy_native')
    unchanged_config = signature(output / 'voxy.cfg')
    build(single)
    assert signature(output / 'voxy_native') == executable
    assert signature(output / 'voxy.cfg') == unchanged_config
    (source / 'shaders/nested/test.wgsl').write_text('changed shader only\n')
    build(single)
    assert_copies(output)
    assert signature(output / 'voxy_native') == executable
    (source / 'lego_world.cfg').write_text('[window]\ntitle = "changed config only"\n')
    build(single)
    assert_copies(output)
    assert signature(output / 'voxy_native') == executable
    (source / 'shaders/new.wgsl').write_text('new shader without reconfiguration\n')
    (output / 'voxy.cfg').unlink()
    (output / 'shaders/nested/test.wgsl').unlink()
    build(single)
    assert_copies(output)
    assert (output / 'shaders/new.wgsl').read_bytes() == (source / 'shaders/new.wgsl').read_bytes()
    assert signature(output / 'voxy_native') == executable
    source_config = (source / 'ridgebreak.cfg').read_bytes()
    (source / 'ridgebreak.cfg').unlink()
    failed = build(single, succeeds=False)
    assert 'ridgebreak.cfg' in failed.stdout + failed.stderr
    assert signature(output / 'voxy_native') == executable
    (source / 'ridgebreak.cfg').write_bytes(source_config)
    build(single)
    assert_copies(output)
    multi = fixture / 'multi build'
    configure(multi, 'Ninja Multi-Config')
    for config in ('Debug', 'Release'):
        build(multi, config)
        assert_copies(multi / 'bin' / config)
        old = signature(multi / 'bin' / config / 'voxy_native')
        build(multi, config)
        assert signature(multi / 'bin' / config / 'voxy_native') == old
    report = {'status': 'passed', 'fixture': str(fixture),
              'tools': {'cmake': cmake, 'ninja': ninja, 'compiler': compiler},
              'source_block_sha256': hashlib.sha256(block.encode()).hexdigest(),
              'checks': ['initial shader/config copies and unchanged assets/data POST_BUILD',
                         'no-op build preserves executable and unchanged config hash/mtime',
                         'shader-only refresh without relink', 'config-only refresh without relink',
                         'new shader and deleted output restored without reconfiguration/relink',
                         'missing required source config fails build',
                         'Ninja Multi-Config Debug and Release target-relative output'],
              'commands': log}
    (fixture / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k:v for k,v in report.items() if k != 'commands'}, indent=2))
except Exception:
    (fixture / 'failed-commands.json').write_text(json.dumps(log, indent=2) + '\n')
    print('Fixture failure artifacts:', fixture)
    raise
PY
```

## Limits and main-build follow-up

- Root ran the actual incremental native target twice. The first reconfigured
  and relinked after the build-rule change. The second passed without changing
  the executable hash or modification time. All 36 staged source files (four
  configs and 32 shader-directory files) matched their source bytes. See
  `native-assets.log`, `native-assets-noop.log` and `native-staging.json`.
  The earlier successful native launch is documented in `native-build.md`.
- The fixture does not exercise Windows or a Visual Studio generator.
- Like the previous shader copy, `copy_directory` does not delete stale staged
  files whose source was removed. A clean build output removes such leftovers.
- Large `assets/` and `data/` directories retain their existing post-link copy
  behavior. This bounded fix establishes freshness for shaders and launch
  configs; it does not introduce a general asset synchronization system.
- No task checkbox was marked and no commit was created by this task.
