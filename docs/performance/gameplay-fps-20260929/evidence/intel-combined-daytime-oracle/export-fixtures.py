"""Prepare linked terrain -> opaque mesh -> water full-fragment fixtures.

This exports descriptors and exact buffers only. Execute in a cleared window.
"""
from pathlib import Path
import ast, hashlib, json, re

BASE_EXPORTER = Path('/tmp/voxys-fps-20260929/intel-image-oracles/export-fixtures.py')
BASE_EXPORTER_SHA = '53cbefba12148d70df5d324407b2a37713c40ac389792f4deec04424d4063115'
original_exporter = BASE_EXPORTER.read_text()
assert hashlib.sha256(original_exporter.encode()).hexdigest() == BASE_EXPORTER_SHA
prefix = original_exporter[:original_exporter.index("water=load_functions('water_image_oracle.py')")]
exec(compile(prefix, str(BASE_EXPORTER), 'exec'), globals())
oracle_sha = {
    'water_night_oracle.py': '2ff43e352e46a96a0276bdb998cd1251e1a2de255b58fb43156f1bff898608dc',
    'mesh_image_oracle.py': '2a336f1e57edcf260c6860659c6b577608c1a62826c730c9fd7e08eb12c1f3d7',
    'terrain_image_oracle.py': '9c897baaa31c02b892a06a24e7fe78f6a738e7a030c4b2ee7e127b85dc0137ef',
}
for name, expected in oracle_sha.items():
    assert sha((SOURCE / name).read_bytes()) == expected, ('Oracle changed', name)
water = load_functions('water_night_oracle.py'); mesh = load_functions('mesh_image_oracle.py')
terrain_path = SOURCE / 'terrain_image_oracle.py'
functions = [node for node in ast.parse(terrain_path.read_text()).body if isinstance(node, ast.FunctionDef)]
terrain = {'__file__': str(terrain_path), 'ROOT': SOURCE, 'W': 32, 'H': 32,
    'np': np, 'wgpu': wgpu, 're': re, 'device': device,
    'U': wgpu.TextureUsage, 'B': wgpu.BufferUsage, 'PIPELINES': {}}
exec(compile(ast.Module(body=functions, type_ignores=[]), str(terrain_path), 'exec'), terrain)
source_paths = {
    'mesh-baseline': SOURCE / 'baseline/mesh_path.wgsl',
    'mesh-shadow-gating': SOURCE / 'mesh-shadow-gating/mesh_path.wgsl',
    'water-baseline': SOURCE / 'baseline/water_clipmap.wgsl',
    'water-deferred-shadow': SOURCE / 'water-deferred-shadow/water_clipmap.wgsl',
    'ray-baseline': SOURCE / 'baseline/ray_blit.wgsl',
    'cove-lazy-background-load': SOURCE / 'cove-lazy-background-load/ray_blit.wgsl',
}
expected_source_sha = {
    'mesh-baseline': '75c974f8062e38a836d5e83852f5892a90cdc81af312e7fb62fb376a67e6f67b',
    'mesh-shadow-gating': '6faf8b0152425a8d01f68e35892d1404040cf17e49e7ba86b445518fb49d0836',
    'water-baseline': 'f38d46349279b39076de523732623f1a4cb16d949c7fbbd8d9b63c4fbabe9abb',
    'water-deferred-shadow': 'd92c968dec06454e2729434a2d732e0d9217498446f443da25db4cade1ba8e43',
    'ray-baseline': 'c2a2b432bd99b29af1f8baca92c690e92f147857004e9477aa5531069df3c97d',
    'cove-lazy-background-load': '75366ef320c798db98c298c539b89b37f3993c6f8d6955ec32dc53d0b1eae84a',
}
source_text = {name: path.read_text() for name, path in source_paths.items()}
for label, source in source_text.items():
    assert sha(source.encode()) == expected_source_sha[label], ('Full shader source changed', label)
fixture = {'purpose': 'Linked full shipping-fragment terrain/mesh/water combined candidate correctness; no gameplay timing',
    'provenance': {'recordingExporter': {'path': str(BASE_EXPORTER), 'sha256': BASE_EXPORTER_SHA},
        'pythonOracles': {name: {'path': str(SOURCE / name), 'sha256': digest} for name, digest in oracle_sha.items()},
        'shaderSources': {name: {'path': str(path), 'sha256': expected_source_sha[name]} for name, path in source_paths.items()},
        'probeNotes': [
            'All production shader functions execute unchanged except the three separately identified candidate edits.',
            'The actual cached-background and live terrain pass feed opaque mesh rejection and scene-water refraction/occlusion inputs.',
            'The opaque mesh draws a raised probe receiver over the left half. Color/depth attachments are initialized from terrain and loaded.',
            'The water pass samples that mesh-composited color/depth, initializes its targets from those values, and loads them so shader discards preserve the earlier scene.',
            'All color targets use shipping RGBA16Float and depth targets R32Float. This is controlled fragment composition, not a claim that probe cameras/geometries are a full engine scene.',
        ]}, 'renders': [], 'comparisons': []}

current_phase = None; current_case = None; prior_targets = None
refs = {}; current_source_labels = []
original_make = device.make
def tracked_make(kind, op, descriptor=None, **values):
    ref = original_make(kind, op, descriptor, **values)
    refs[ref.id] = ref
    if op == 'createTextureView': ref.texture = values['texture']
    return ref
device.make = tracked_make
create_texture = device.create_texture
def sampled_output_texture(**descriptor):
    if descriptor['usage'] & wgpu.TextureUsage.COPY_SRC:
        descriptor['usage'] |= wgpu.TextureUsage.COPY_DST | wgpu.TextureUsage.TEXTURE_BINDING
    return create_texture(**descriptor)
device.create_texture = sampled_output_texture
create_buffer = device.create_buffer_with_data
def shared_camera_buffer(**descriptor):
    data = np.ascontiguousarray(descriptor['data']).copy()
    if data.dtype == np.float32 and data.size == 40:
        data[16:19] = [0., 3., 0.]
    if data.dtype == np.float32 and data.size == 136:
        data[48:52] = [32., 32., 1/32., 1/32.]
        if current_phase == 'water':
            matrix = np.array([[1,0,0,0],[0,0,-1,3],[0,1,0,0],[0,0,0,1]], np.float32)
            data[32:48] = matrix.T.ravel(); data[56:59] = [0., 3., 0.]
            light = data[92:95].copy(); data[64:67] = [light[0], light[2], -light[1]]
    descriptor['data'] = data
    return create_buffer(**descriptor)
device.create_buffer_with_data = shared_camera_buffer
create_pipeline = device.create_render_pipeline
def shipping_pipeline(**descriptor):
    fragment = descriptor.get('fragment', {})
    if current_phase == 'mesh':
        fragment['targets'][0]['format'] = 'rgba16float'
    elif current_phase == 'water':
        fragment.setdefault('constants', {})['COVE_VISUALS'] = current_case['visuals'] == 'cove'
        fragment.setdefault('constants', {})['OPAQUE_SCENE_WATER'] = True
    else:
        fragment.setdefault('constants', {})['COVE_VISUALS'] = current_case['visuals'] == 'cove'
        fragment.setdefault('constants', {})['FRONTIER_VISUALS'] = current_case['visuals'] == 'frontier'
    return create_pipeline(**descriptor)
device.create_render_pipeline = shipping_pipeline
begin_pass = Ref.begin_render_pass
def begin_linked_pass(self, **descriptor):
    attachments = descriptor.get('color_attachments', [])
    if attachments and current_phase in ['mesh', 'water']:
        for attachment, previous in zip(attachments, prior_targets):
            destination = attachment['view'].texture
            assert previous.descriptor['format'] == destination.descriptor['format']
            device.record('copyTextureToTexture', encoder=self, source={'texture': previous},
                          destination={'texture': destination}, size=(32,32,1))
            attachment['load_op'] = 'load'; attachment.pop('clear_value', None)
    return begin_pass(self, **descriptor)
Ref.begin_render_pass = begin_linked_pass
draw = Ref.draw
def draw_partial_mesh(self, *arguments):
    if current_phase == 'mesh':
        device.record('setScissorRect', pass_=self, arguments=[0,0,16,32])
    return draw(self, *arguments)
Ref.draw = draw_partial_mesh
mesh['VERTEX'] = mesh['VERTEX'].replace(
    'vec3<f32>(ndc.x * PROBE_EXTENT, 0.0, ndc.y * PROBE_EXTENT)',
    'vec3<f32>(ndc.x * PROBE_EXTENT, 1.0, ndc.y * PROBE_EXTENT)')
original_mesh_resources = mesh['resources']
def mesh_linked_resources(case):
    mapping, shadow = original_mesh_resources(case)
    mapping[5] = prior_targets[1].create_view()
    return mapping, shadow
mesh['resources'] = mesh_linked_resources
original_water_resources = water['resources']
terrain_targets = None
def water_linked_resources(case):
    mapping, scene = original_water_resources(case)
    mapping[1] = terrain_targets[1].create_view()
    mapping[11] = prior_targets[0].create_view(); mapping[12] = prior_targets[1].create_view()
    return mapping, scene
water['resources'] = water_linked_resources

def read_targets_since(start):
    operations = device.operations[start:]
    return [refs[op['source']['texture']['$ref']] for op in operations if op['op'] == 'readTexture']

def capture(candidate, water_entry, case):
    global current_phase, current_case, prior_targets, terrain_targets
    current_case = case; prior_targets = None
    device.reset(); refs.clear(); terrain['PIPELINES'].clear(); water['PIPELINES'].clear()
    sources = {'mesh': 'mesh-shadow-gating' if candidate else 'mesh-baseline',
               'water': 'water-deferred-shadow' if candidate else 'water-baseline',
               'ray': 'cove-lazy-background-load' if candidate else 'ray-baseline'}
    phase_reads = []; raw_parts = []
    def phase(name, function):
        global current_phase
        current_phase = name
        start = len(device.operations); before_reads = sum(op['op'] == 'readTexture' for op in device.operations)
        value = function()
        phase_reads.append({'name': name, 'readStart': before_reads,
                            'readCount': sum(op['op'] == 'readTexture' for op in device.operations[start:])})
        return value, read_targets_since(start)
    (cached, background), _ = phase('background', lambda: terrain['render'](source_text[sources['ray']], 'fsBackground', {**case, 'static': 1}))
    terrain_entry = 'fsSceneTerrainCove' if case['visuals'] == 'cove' else 'fsSceneTerrain'
    (scene, _), terrain_targets = phase('terrain', lambda: terrain['render'](source_text[sources['ray']], terrain_entry, case, background))
    prior_targets = terrain_targets
    opaque, mesh_targets = phase('mesh', lambda: mesh['render'](source_text[sources['mesh']], 'fsOpaqueHdr', {**case, 'extent': 2., 'normal': case.get('normal', [0.,1.,0.])}))
    prior_targets = mesh_targets
    ocean, _ = phase('water', lambda: water['render'](source_text[sources['water']], water_entry, {**case, 'extent': 2.}))
    operations = device.operations
    uploads = [{key: op[key] for key in ['op', 'data'] if key in op} for op in operations if op['op'] in ['createBuffer', 'writeTexture']]
    index = len(fixture['renders'])
    fixture['renders'].append({'id': index, 'kind': 'combined', 'shader': 'combined-candidate' if candidate else 'combined-baseline',
        'sources': sources, 'entry': terrain_entry + '/fsOpaqueHdr/' + water_entry, 'case': case,
        'phaseReads': phase_reads, 'expectedBytes': len(cached) + len(scene) + len(opaque) + len(ocean),
        'operations': operations, 'uploadSequenceSha256': sha(json.dumps(uploads, sort_keys=True, separators=(',', ':')).encode())})
    return index

cases = [
    {'name': 'dry', 'sun': 1.4, 'hour': 12},
    {'name': 'wet', 'sun': 1.4, 'hour': 12, 'surface': [.7,.2,1.,0.]},
    {'name': 'backlit', 'sun': 1.4, 'hour': 12, 'normal': [0.,-1.,0.]},
    {'name': 'night', 'sun': 0., 'hour': 23},
    {'name': 'land', 'sun': 1.4, 'hour': 12, 'land_mask': 1},
    {'name': 'opaque', 'sun': 1.4, 'hour': 12, 'opaque_mask': 1},
]
for visuals in ['cove', 'frontier']:
    for water_entry in ['fsScene', 'fsSceneColor']:
        for original in cases:
            case = {**original, 'name': original['name'] + '-' + visuals, 'visuals': visuals}
            before = capture(False, water_entry, case); after = capture(True, water_entry, case)
            assert fixture['renders'][before]['uploadSequenceSha256'] == fixture['renders'][after]['uploadSequenceSha256']
            fixture['comparisons'].append({'kind': 'combined', 'candidate': 'combined-daytime',
                'entry': fixture['renders'][before]['entry'], 'case': case['name'], 'baselineRender': before, 'candidateRender': after})
fixture['blobs'] = blob_manifest
raw = json.dumps(fixture, separators=(',', ':')).encode()
(OUT / 'fixtures.json').write_bytes(raw)
summary = {'fixtureSha256': sha(raw), 'fixtureBytes': len(raw), 'renders': len(fixture['renders']),
    'comparisons': len(fixture['comparisons']), 'blobFiles': len(blob_manifest),
    'blobBytes': sum(x['bytes'] for x in blob_manifest.values()), 'exporterSha256': sha(Path(__file__).read_bytes())}
(OUT / 'export-result.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
