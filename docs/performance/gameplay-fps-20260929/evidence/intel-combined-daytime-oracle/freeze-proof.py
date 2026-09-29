"""Reverify linked shipping-fragment three-shader composition independently."""
from pathlib import Path
import collections, hashlib, json, shutil
out = Path(__file__).parent
windows = Path('/mnt/c/Users/Public/voxys-fps-20260929/combined-daytime-image-oracle')
sha = lambda data: hashlib.sha256(data).hexdigest()
shutil.copy2(windows / 'hardware-result.json', out / 'hardware-result.json')
shutil.copytree(windows / 'outputs', out / 'outputs', dirs_exist_ok=True)
report = json.loads((out / 'hardware-result.json').read_text())
fixture = json.loads((out / 'fixtures.json').read_text())
summary = json.loads((out / 'export-result.json').read_text())
result = report['result']
assert result['ok'] and not result['errors'] and not result['lost']
assert sha((out / 'fixtures.json').read_bytes()) == result['fixtureSha256'] == summary['fixtureSha256']
assert sha((out / 'export-fixtures.py').read_bytes()) == summary['exporterSha256']
assert sha((out / 'oracle-browser.js').read_bytes()) == report['harness']['browserSha256']
assert sha((out / 'run-windows-oracle.mjs').read_bytes()) == report['harness']['runnerSha256']
for name, identity in fixture['blobs'].items():
    raw = (out / name).read_bytes()
    assert sha(raw) == identity['sha256'] and len(raw) == identity['bytes']
sources = {}
for label, identity in fixture['provenance']['shaderSources'].items():
    raw = Path(identity['path']).read_bytes()
    assert sha(raw) == identity['sha256']; sources[label] = raw
    target = out / 'full-sources' / (label + '.wgsl')
    target.parent.mkdir(exist_ok=True); target.write_bytes(raw)
for render in fixture['renders']:
    labels = set(render['sources'].values()); seen = set()
    for operation in render['operations']:
        if operation['op'] == 'createShaderModule':
            raw = (out / operation['code']['$blob']).read_bytes()
            matching = {label for label in labels if raw.startswith(sources[label])}
            assert len(matching) == 1; seen |= matching
    assert seen == labels
outputs = {}; actuals = {}; render_results = {render['id']: render for render in result['renders']}
for render in result['renders']:
    path = out / 'outputs' / ('render-' + str(render['id']) + '.bin')
    raw = path.read_bytes(); recorded = fixture['renders'][render['id']]
    assert sha(raw) == render['sha256'] and len(raw) == render['bytes'] == recorded['expectedBytes']
    assert render['nonFiniteComponents'] == 0
    assert render['uploadSequenceSha256'] == recorded['uploadSequenceSha256']
    outputs[str(path.relative_to(out))] = {'sha256': sha(raw), 'bytes': len(raw)}; actuals[render['id']] = raw
for pair in result['comparisons']:
    assert pair['differentBytes'] == 0 and actuals[pair['baselineRender']] == actuals[pair['candidateRender']]
    assert fixture['renders'][pair['baselineRender']]['uploadSequenceSha256'] == fixture['renders'][pair['candidateRender']]['uploadSequenceSha256']
for control in result['stageControls']:
    assert control['accepted']
    render = fixture['renders'][control['render']]; raw = actuals[control['render']]
    targets = render_results[control['render']]['targets']; offsets = []; offset = 0
    for target in targets:
        offsets.append(offset); offset += target['size'][0] * target['size'][1] * target['size'][2] * target['bytesPerPixel']
    def color(name):
        index = next(phase['readStart'] for phase in render['phaseReads'] if phase['name'] == name)
        target = targets[index]; assert target['format'] == 'rgba16float'
        count = target['size'][0] * target['size'][1] * target['size'][2] * target['bytesPerPixel']
        return raw[offsets[index]:offsets[index] + count]
    differences = lambda before, after: sum(a != b for a, b in zip(before, after))
    assert differences(color('terrain'), color('mesh')) == control['meshDifferentBytes']
    assert differences(color('mesh'), color('water')) == control['waterDifferentBytes']
    if control['required']:
        assert control['meshDifferentBytes'] > 0 and control['waterDifferentBytes'] > 0
proof = {'accepted': True, 'scope': 'Actual Intel linked shipping-fragment combined daytime equivalence; no gameplay FPS or timing claim',
    'adapter': result['adapter'], 'fixtureSha256': result['fixtureSha256'],
    'hardwareReportSha256': sha((out / 'hardware-result.json').read_bytes()),
    'sourceSha256': {label: identity['sha256'] for label, identity in fixture['provenance']['shaderSources'].items()},
    'comparisons': len(result['comparisons']), 'renders': len(result['renders']), 'outputs': outputs,
    'stageControls': result['stageControls'], 'differentBytes': 0, 'nonFiniteComponents': 0,
    'errors': result['errors'], 'lost': result['lost']}
(out / 'verified-proof.json').write_text(json.dumps(proof, indent=2) + '\n')
(out / 'README.md').write_text('''# Linked combined daytime shader oracle

Actual Intel gen12lp browser WebGPU executes the three candidate replacements together: mesh shadow gating, deferred water shadow, and lazy Cove cached-background loading. Each original/candidate pair shares all exact uploaded buffers, textures, and descriptors. Raw outputs from the background, live terrain, opaque mesh, and scene-water stages are byte-identical.

The actual terrain fragment's color/depth output feeds mesh rejection. Opaque mesh draws a raised receiver over the left half of the scene, loading targets seeded from terrain. Water samples that composited color/depth; its targets are seeded and loaded so discarded pixels preserve the earlier scene. Mandatory dry controls prove that both mesh and water change predecessor color pixels in each visual mode and water entry. Thus the oracle exercises a linked composition instead of only comparing independent constant-input modules.

Both actual Cove and Frontier specializations run, with dry, wet, backlit, night, land, and opaque resource cases. Mesh uses fsOpaqueHdr and both scene-water entries run. All color targets use shipping RGBA16Float and depth targets use R32Float. Original functions and all shader bodies remain intact except the explicit candidate edits; added probe geometry, linked resources, copy/load attachment preparation, and partial viewport are documented in the exporter.

Every full source, descriptor/input blob, harness, and raw output is hash-guarded and independently reverified. Earlier isolated Intel oracles additionally cover mesh fs with stricter RGBA32Float and every legacy water entry. These linked fixture scenes are controlled fragment-composition probes, not an exhaustive engine-scene oracle or a performance result. Production shaders and the FPS harness remain unchanged.
''')
print(json.dumps({key: proof[key] for key in ['accepted', 'adapter', 'comparisons', 'renders', 'differentBytes', 'hardwareReportSha256']}, indent=2))
