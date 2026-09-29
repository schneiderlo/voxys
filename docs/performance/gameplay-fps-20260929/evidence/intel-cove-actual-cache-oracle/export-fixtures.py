"""Prepare full-entry actual Cove background/cache-copy fixtures. No GPU dispatch.

Do not execute exports during a clean gameplay/component timing window.
"""
from pathlib import Path
import ast, hashlib, json, re

BASE_EXPORTER = Path('/tmp/voxys-fps-20260929/intel-image-oracles/export-fixtures.py')
BASE_EXPORTER_SHA = '53cbefba12148d70df5d324407b2a37713c40ac389792f4deec04424d4063115'
original_exporter = BASE_EXPORTER.read_text()
assert hashlib.sha256(original_exporter.encode()).hexdigest() == BASE_EXPORTER_SHA
prefix = original_exporter[:original_exporter.index("water=load_functions('water_image_oracle.py')")]
exec(compile(prefix, str(BASE_EXPORTER), 'exec'), globals())
terrain_path = SOURCE / 'terrain_image_oracle.py'
assert sha(terrain_path.read_bytes()) == '9c897baaa31c02b892a06a24e7fe78f6a738e7a030c4b2ee7e127b85dc0137ef'
functions = [node for node in ast.parse(terrain_path.read_text()).body if isinstance(node, ast.FunctionDef)]
terrain = {'__file__': str(terrain_path), 'ROOT': SOURCE, 'W': 16, 'H': 16,
    'np': np, 'wgpu': wgpu, 're': re, 'device': device,
    'U': wgpu.TextureUsage, 'B': wgpu.BufferUsage, 'PIPELINES': {}}
exec(compile(ast.Module(body=functions, type_ignores=[]), str(terrain_path), 'exec'), terrain)
source_paths = {
    'ray-baseline': SOURCE / 'baseline/ray_blit.wgsl',
    'cove-actual-background-cache': SOURCE / 'cove-actual-background-cache/ray_blit.wgsl',
}
expected_source_sha = {
    'ray-baseline': 'c2a2b432bd99b29af1f8baca92c690e92f147857004e9477aa5531069df3c97d',
    'cove-actual-background-cache': '54ca0d68b9e241bfd6064599ac50f6710e2b8575ec87d928cad1cd89b9c8a793',
}
source_text = {name: path.read_text() for name, path in source_paths.items()}
for label, source in source_text.items():
    assert sha(source.encode()) == expected_source_sha[label], ('Full shader source changed', label)
fixture = {'purpose': 'Actual Cove cache whole shipping-fragment equivalence; final live color/depth comparison; no FPS claim',
    'provenance': {'recordingExporter': {'path': str(BASE_EXPORTER), 'sha256': BASE_EXPORTER_SHA},
        'pythonOracles': {'terrain_image_oracle.py': {'path': str(terrain_path), 'sha256': sha(terrain_path.read_bytes())}},
        'shaderSources': {name: {'path': str(path), 'sha256': expected_source_sha[name]} for name, path in source_paths.items()},
        'notes': [
            'Both sides execute unchanged full original shader source except the declared candidate edits.',
            'Baseline uses the actual existing fsBackground plus live fsSceneTerrainCove; candidate bakes actual fsBackgroundCove, then executes the live cache guard.',
            'Compare final live RGBA16Float/R32Float; the deliberately improved cache image is retained as an exercised-path control, not compared against the legacy cache.',
            'The sole permitted uploaded buffer difference is private waterMotion.w in the live CameraUniforms; all other uploaded buffer/texture bytes are equal.',
            'Cove specialization is true, Frontier false. Underwater/invalid-cache mode keeps legacy background and original live shading.',
            'This shader oracle does not validate CPU cache invalidation, callback ordering or submission rollback; those require engine tests before FPS use.',
        ]}, 'renders': [], 'comparisons': []}
current_case = None; current_static = False; current_candidate = False
live_uniform_inputs = []
create_buffer = device.create_buffer_with_data
def camera_with_private_flag(**descriptor):
    data = np.ascontiguousarray(descriptor['data']).copy()
    if data.dtype == np.float32 and data.size == 136:
        if not current_static:
            data[108] = current_case.get('time', 1.25)
            data[109] = current_case.get('waveOffset', 0.)
            data[119] = current_case.get('exposure', 1.1)
            data[111] = float(current_candidate and current_case.get('cacheFlag', True))
        normalized = data.copy(); normalized[111] = 0.
        live_uniform_inputs.append(sha(normalized.tobytes()))
    descriptor['data'] = data
    return create_buffer(**descriptor)
device.create_buffer_with_data = camera_with_private_flag
original_resources = terrain['resources']
def varied_resources(case):
    mapping = original_resources(case)
    pattern = case.get('depthPattern')
    if pattern:
        depth = np.full((16,16), {'zero':0., 'sky':-1., 'failure':-2., 'tiny':.0001, 'mixed':.0001}[pattern], np.float32)
        if pattern == 'mixed':
            depth[:8,:8] = -1.; depth[:8,8:] = -2.; depth[8:,:8] = 0.
        mapping[0][1] = terrain['texture'](depth, 'r32float').create_view()
    if 'materialNormal' in case:
        data = np.empty((16,16,4), np.float16); data[:] = [*case['materialNormal'], case.get('materialKind',0)]
        mapping[0][3] = terrain['texture'](data, 'rgba16float').create_view()
    if 'environment' in case:
        color = case['environment']
        spec = terrain['rgba']([*color,1.],6); diff = terrain['rgba']([*reversed(color),1.],6)
        mapping[2][0] = spec.create_view(dimension='cube'); mapping[2][1] = diff.create_view(dimension='cube')
    return mapping
terrain['resources'] = varied_resources

def render(source, entry, case, background=None):
    global current_static
    current_static = bool(case.get('static'))
    mapping = terrain['resources'](case)
    if background is not None: mapping[0][11] = background.create_view()
    targets = [{'format':'rgba16float'}]
    if not entry.startswith('fsBackground'): targets.append({'format':'r32float'})
    module = device.create_shader_module(code=source)
    pipe = device.create_render_pipeline(layout='auto', vertex={'module':module, 'entry_point':'vs'},
        fragment={'module':module, 'entry_point':entry, 'targets':targets,
                  'constants':{'COVE_VISUALS':True, 'FRONTIER_VISUALS':False}})
    used = terrain['reachable'](source,entry)
    groups = [device.create_bind_group(layout=pipe.get_bind_group_layout(i),
        entries=[{'binding':j,'resource':mapping[i][j]} for j in used[i]])
        for i in range(max(i for i,u in enumerate(used) if u)+1)]
    outs = [device.create_texture(size=(16,16,1), format=t['format'],
        usage=wgpu.TextureUsage.RENDER_ATTACHMENT|wgpu.TextureUsage.COPY_SRC|wgpu.TextureUsage.TEXTURE_BINDING) for t in targets]
    enc = device.create_command_encoder()
    p = enc.begin_render_pass(color_attachments=[{'view':t.create_view(), 'load_op':'clear',
        'store_op':'store','clear_value':(.25,.25,.25,.25)} for t in outs])
    p.set_pipeline(pipe)
    for i,group in enumerate(groups): p.set_bind_group(i,group)
    p.draw(3); p.end(); device.queue.submit([enc.finish()])
    raw = b''.join(bytes(device.queue.read_texture({'texture':texture},
        {'bytes_per_row':16*(8 if i==0 else 4),'rows_per_image':16}, (16,16,1))) for i,texture in enumerate(outs))
    return raw, outs[0]

def normalized_uploads(operations):
    uploads=[]
    for op in operations:
        if op['op'] not in ['createBuffer','writeTexture']: continue
        raw=(OUT/op['data']['$blob']).read_bytes()
        if op['op']=='createBuffer' and len(raw)==544:
            values=np.frombuffer(raw,dtype=np.float32).copy(); values[111]=0.; raw=values.tobytes()
        uploads.append({'op':op['op'],'sha256':sha(raw),'bytes':len(raw)})
    return sha(json.dumps(uploads,sort_keys=True,separators=(',',':')).encode())

def capture(candidate, case):
    global current_case, current_candidate
    current_case=case; current_candidate=candidate
    device.reset(); live_uniform_inputs.clear()
    label='cove-actual-background-cache' if candidate else 'ray-baseline'
    actual_cache=candidate and case.get('cacheFlag',True) and not case.get('underwater')
    background_entry='fsBackgroundCove' if actual_cache else 'fsBackground'
    background_raw, background=render(source_text[label], background_entry, {**case,'static':1})
    live_raw,_=render(source_text[label], 'fsSceneTerrainCove', case, background)
    index=len(fixture['renders']); operations=device.operations
    fixture['renders'].append({'id':index,'kind':'terrain','shader':label,'entry':'fsSceneTerrainCove',
        'backgroundEntry':background_entry,'actualCache':actual_cache,'case':case,
        'expectedBytes':len(background_raw)+len(live_raw),'compareByteOffset':len(background_raw),
        'operations':operations,'uploadSequenceSha256':normalized_uploads(operations),
        'normalizedCameraSha256':live_uniform_inputs.copy()})
    return index

cases=[]
for shadow,depth in [('unshadowed',1.),('shadowed',0.),('mixed-shadow',.48)]:
    for name,extra in [('lego',{}),('smooth',{'lego':0}),('study',{'lego':2}),
            ('invalid-normal',{'materialNormal':[0.,0.,0.]}),
            ('wet-shore',{'waterenabled':1,'water':.2}),('sky',{'sky':1})]:
        cases.append({'name':shadow+'-'+name,'sun':1.4,'hour':12,'shadowdepth':depth,**extra})
for pattern in ['zero','sky','failure','tiny','mixed']:
    cases.append({'name':'depth-'+pattern,'sun':1.4,'hour':12,'shadowdepth':1.,'depthPattern':pattern})
for name,extra in [
        ('night-moon',{'sun':.28,'hour':0}),
        ('direct-zero',{'sun':0.,'hour':23}),
        ('tiny-sun',{'sun':1e-8,'hour':23}),
        ('environment-red',{'environment':[2.,.03,.01]}),
        ('environment-blue',{'environment':[.02,.04,3.]}),
        ('live-surface-time',{'time':37.,'waveOffset':.7,'exposure':2.3}),
        ('cache-invalid',{'cacheFlag':False}),
        ('underwater',{'waterenabled':1,'underwater':1,'water':4,'time':1.25}),
        ('underwater-caustics-later',{'waterenabled':1,'underwater':1,'water':4,'time':37.,'foam':.9}),
        ('underwater-shadow',{'waterenabled':1,'underwater':1,'water':4,'shadowdepth':0.}),
    ]:
    cases.append({'name':name,'sun':1.4,'hour':12,'shadowdepth':1.,**extra})
for case in cases:
    before=capture(False,case); after=capture(True,case)
    assert fixture['renders'][before]['uploadSequenceSha256']==fixture['renders'][after]['uploadSequenceSha256'],case
    assert fixture['renders'][before]['normalizedCameraSha256']==fixture['renders'][after]['normalizedCameraSha256'],case
    fixture['comparisons'].append({'kind':'terrain','candidate':'cove-actual-background-cache',
        'entry':'fsSceneTerrainCove','case':case['name'],'baselineRender':before,'candidateRender':after})
fixture['blobs']=blob_manifest
raw=json.dumps(fixture,separators=(',',':')).encode(); (OUT/'fixtures.json').write_bytes(raw)
summary={'fixtureSha256':sha(raw),'fixtureBytes':len(raw),'renders':len(fixture['renders']),
    'comparisons':len(fixture['comparisons']),'blobFiles':len(blob_manifest),
    'blobBytes':sum(x['bytes'] for x in blob_manifest.values()),'exporterSha256':sha(Path(__file__).read_bytes())}
(OUT/'export-result.json').write_text(json.dumps(summary,indent=2)+'\n'); print(json.dumps(summary,indent=2))
