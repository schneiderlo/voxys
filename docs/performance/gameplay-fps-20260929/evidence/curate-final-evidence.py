"""Append reviewable records, excluding binaries, terrain and bulk cache assets."""
from pathlib import Path
import gzip
import hashlib
import json

ROOT = Path('/tmp/voxys-fps-20260929')
DEST = Path('/home/lschneid/workspace/schneiderlo/voxys/docs/performance/gameplay-fps-20260929/evidence')
manifest = json.loads((DEST / 'manifest.json').read_text())
records = {item['file']: item for item in manifest['records']}
sha = lambda value: hashlib.sha256(value).hexdigest()

def add(source, relative=None, compress=False):
    assert source.is_file(), source
    assert source.suffix not in ('.so', '.a', '.o', '.wasm', '.data', '.r16')
    relative = Path(relative or source.relative_to(ROOT))
    assert '..' not in relative.parts and not relative.is_absolute()
    if compress:
        relative = Path(str(relative) + '.gz')
    raw = source.read_bytes()
    stored = gzip.compress(raw, compresslevel=9, mtime=0) if compress else raw
    destination = DEST / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(stored)
    records[str(relative)] = {'file': str(relative), 'originalSource': str(source),
        'uncompressedBytes': len(raw), 'uncompressedSha256': sha(raw),
        'storedBytes': len(stored), 'storedSha256': sha(stored)}

for name in (
    'spatial-final-decision.json', 'initial-user-changes-preserved.json',
    'windows-interop-block.json', 'foot-primary-screen-v4-summary.json',
    'combined-daytime-v5-launch.json', 'combined-daytime-unpromoted.patch', 'night-gating-scope.md',
    'cove-actual-cache-audit.md', 'terrain-cache-audit.md',
    'build-final-cpu.py', 'verify-final-native-cpu.py', 'curate-final-evidence.py'):
    add(ROOT / name)
add(ROOT / 'foot-primary-screen-v4-report.json', compress=True)
for source in sorted((ROOT / 'foot-primary-screen-v4-proof').glob('*.json')):
    add(source, compress=source.name == 'report.json')
for source in sorted((ROOT / 'clean-streaming-queries').glob('*.json')):
    add(source, compress=source.name != 'summary.json')
for source in sorted((ROOT / 'final-focused-cpu-proof').iterdir()):
    if source.is_file() and source.suffix in ('.json', '.md', '.xml'):
        add(source)
for name in ('README.md', 'change.patch', 'source-preparation.json', 'source-mesh_path.hpp',
             'correctness-result.json', 'lowering-result.json',
             'benchmark-build-recipe.json', 'component-result.json'):
    add(ROOT / 'mesh-direct-pack' / name)
for name in ('captures-hashes.json', 'records-hashes.json', 'baseline-warm-copy.s',
             'candidate-warm-construct.s'):
    add(ROOT / 'mesh-direct-pack' / name, compress=True)
for source in sorted((ROOT / 'mesh-direct-pack').glob('component-*.log')):
    add(source, compress=True)
for name in ('new-boundary-tests-proof.json', 'baseline-new-tests.xml',
             'partial-bool-new-tests.xml', 'streaming-new-tests.xml'):
    add(ROOT / 'query-streaming' / name)
add(ROOT / 'query-streaming/runtime/proof.json')
for source in sorted((ROOT / 'fps-harness-v6').iterdir()):
    if source.is_file():
        add(source)
for group in ('intel-combined-daytime-oracle', 'intel-cove-actual-cache-oracle'):
    for name in ('verified-proof.json', 'hardware-result.json', 'software-result.json',
                 'export-result.json', 'fixtures.json', 'final-status.json',
                 'export-fixtures.py', 'freeze-proof.py', 'oracle-browser.js',
                 'replay-software.py', 'run-windows-oracle.mjs', 'README.md'):
        source = ROOT / group / name
        if source.exists():
            add(source, compress=name == 'fixtures.json')
    output_directory = ROOT / group / ('outputs' if group == 'intel-combined-daytime-oracle' else 'software-outputs')
    for source in sorted(output_directory.glob('*.bin')):
        add(source, compress=True)
for name in ('README.md', 'candidate.patch', 'integration-tests.patch', 'source-guards.json',
             'source-preparation.json', 'full-type-consumers.json'):
    source = ROOT / 'cove-actual-cache-prototype' / name
    if source.exists():
        add(source)

final_build = ROOT / 'final-cpu-build/proof.json'
assert json.loads(final_build.read_text()).get('finishedUtc'), 'Final WASM build is not finished'
for name in ('proof.json', 'link.params', 'glue-difference.json'):
    add(ROOT / 'final-cpu-build' / name)
final_native = ROOT / 'final-native-cpu/proof.json'
assert json.loads(final_native.read_text()).get('finishedUtc'), 'Final native replay is not finished'
for source in sorted((ROOT / 'final-native-cpu').iterdir()):
    if source.is_file() and source.suffix in ('.json', '.xml', '.save', '.jsonl'):
        add(source, compress=source.suffix == '.jsonl')
for name in ('final-wasm-interface-proof.json', 'final-review.json', 'final-archive-replay-check.json'):
    add(ROOT / name)
manifest['records'] = list(records.values())
manifest['scope'] = 'Frozen experiment evidence. Accepted CPU changes, rejected/diagnostic runs and blocked hardware follow-ups are explicit. Game/terrain binaries and bulk prototype assets excluded.'
(DEST / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
for record in manifest['records']:
    stored = (DEST / record['file']).read_bytes()
    assert sha(stored) == record['storedSha256']
    raw = gzip.decompress(stored) if record['file'].endswith('.gz') else stored
    assert sha(raw) == record['uncompressedSha256']
print(json.dumps({'records': len(records), 'storedBytes': sum(r['storedBytes'] for r in records.values()),
    'everyStoredAndDecodedHashVerified': True}, indent=2))
