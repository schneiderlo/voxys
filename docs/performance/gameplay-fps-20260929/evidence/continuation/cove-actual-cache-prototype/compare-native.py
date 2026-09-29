"""Exact shipping target comparison after the paired native oracle completes."""
from pathlib import Path
import array, hashlib, json

OUT = Path('/tmp/voxys-fps-20260929/cove-actual-cache-native')
baseline = OUT / 'baseline/captures'
candidate = OUT / 'candidate/captures'
names = {path.name for path in baseline.glob('*.bin')}
candidate_names = {path.name for path in candidate.glob('*.bin')
                   if not path.name.startswith('CoveCacheTracksRebakesDiscardAndLegacyTransitions-')}
assert names and names == candidate_names, (len(names), len(candidate_names))
sha = lambda data: hashlib.sha256(data).hexdigest()
pixel_count = 320 * 240
report = {'scope': 'All shipping BGRA8 presentation, R32 linear depth, RGBA16 opaque-target bytes; strict equality. Native Vulkan llvmpipe correctness only.',
          'frameCount': len(names), 'pixelsPerFrame': pixel_count, 'comparisons': []}
for name in sorted(names):
    original = (baseline / name).read_bytes()
    changed = (candidate / name).read_bytes()
    assert len(original) == len(changed) == pixel_count * 16
    sections = [('presentation', 0, pixel_count * 4),
                ('linearDepth', pixel_count * 4, pixel_count * 8),
                ('opaqueHdr', pixel_count * 8, pixel_count * 16)]
    comparison = {'name': name, 'baselineSha256': sha(original), 'candidateSha256': sha(changed), 'targets': {}}
    for target, begin, end in sections:
        a, b = original[begin:end], changed[begin:end]
        mismatches = sum(x != y for x, y in zip(a, b))
        comparison['targets'][target] = {'byteMismatches': mismatches}
        if target in ['linearDepth', 'opaqueHdr']:
            words = array.array('I' if target == 'linearDepth' else 'H')
            words.frombytes(b)
            exponent = 0x7f800000 if target == 'linearDepth' else 0x7c00
            nonfinite = sum((word & exponent) == exponent for word in words)
            comparison['targets'][target]['nonfinite'] = nonfinite
        else:
            nonfinite = 0
        assert mismatches == 0 and nonfinite == 0, (name, target, mismatches, nonfinite)
    report['comparisons'].append(comparison)
report['allTargetsExact'] = True
report['rawBytesCompared'] = len(names) * pixel_count * 16
(OUT / 'frame-equivalence.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({key: value for key, value in report.items() if key != 'comparisons'}, indent=2))
