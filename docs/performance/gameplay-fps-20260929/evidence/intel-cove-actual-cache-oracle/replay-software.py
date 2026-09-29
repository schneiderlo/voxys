"""Supplemental software-Vulkan replay. This is not Intel acceptance or timing."""
from pathlib import Path
import hashlib, json, sys
import numpy as np, wgpu
OUT=Path(__file__).parent
sha=lambda data:hashlib.sha256(data).hexdigest()
fixture=json.loads((OUT/'fixtures.json').read_text())
summary=json.loads((OUT/'export-result.json').read_text())
assert sha((OUT/'fixtures.json').read_bytes())==summary['fixtureSha256']
adapter=wgpu.gpu.request_adapter_sync(power_preference='low-power')
device=adapter.request_device_sync()
modules={}; pipelines={}; blobs={}; actuals={}; outputs={}
directory=OUT/'software-outputs'; directory.mkdir(exist_ok=True)
def blob(value):
    name=value['$blob']
    if name not in blobs:
        data=(OUT/name).read_bytes(); identity=fixture['blobs'][name]
        assert sha(data)==identity['sha256'] and len(data)==identity['bytes']; blobs[name]=data
    return blobs[name]
def replay(render):
    refs={}; keys={}; owned=[]; reads=[]
    def resolve(value):
        if isinstance(value,list): return [resolve(x) for x in value]
        if isinstance(value,dict):
            if '$ref' in value: return refs[value['$ref']]
            return {name:resolve(x) for name,x in value.items() if x is not None}
        return value
    def semantic(value):
        if isinstance(value,list): return [semantic(x) for x in value]
        if isinstance(value,dict):
            if '$ref' in value: return keys[value['$ref']]
            return {name:semantic(x) for name,x in value.items()}
        return value
    try:
        for op in render['operations']:
            name=op['op']; descriptor=resolve(op.get('descriptor',{})); result=None
            if name=='createBuffer':
                descriptor.pop('mapped_at_creation'); descriptor.pop('size')
                result=device.create_buffer_with_data(data=blob(op['data']),**descriptor); owned.append(result)
            elif name=='createTexture': result=device.create_texture(**descriptor); owned.append(result)
            elif name=='createTextureView': result=resolve(op['texture']).create_view(**descriptor)
            elif name=='createSampler': result=device.create_sampler(**descriptor)
            elif name=='writeTexture': device.queue.write_texture(resolve(op['destination']),blob(op['data']),resolve(op['layout']),op['size'])
            elif name=='createShaderModule':
                key=op['code']['$blob']; keys[op['id']]=key
                if key not in modules: modules[key]=device.create_shader_module(code=blob(op['code']).decode(),**descriptor)
                result=modules[key]
            elif name=='createRenderPipeline':
                key=json.dumps(semantic(op['descriptor']),sort_keys=True)
                if key not in pipelines: pipelines[key]=device.create_render_pipeline(**descriptor)
                result=pipelines[key]
            elif name=='getBindGroupLayout': result=resolve(op['pipeline']).get_bind_group_layout(op['index'])
            elif name=='createBindGroup': result=device.create_bind_group(**descriptor)
            elif name=='createCommandEncoder': result=device.create_command_encoder(**descriptor)
            elif name=='beginRenderPass': result=resolve(op['encoder']).begin_render_pass(**descriptor)
            elif name=='setPipeline': resolve(op['pass_']).set_pipeline(resolve(op['pipeline']))
            elif name=='setBindGroup': resolve(op['pass_']).set_bind_group(op['index'],resolve(op['group']))
            elif name=='draw': resolve(op['pass_']).draw(*op['arguments'])
            elif name=='endRenderPass': resolve(op['pass_']).end()
            elif name=='finishEncoder': result=resolve(op['encoder']).finish()
            elif name=='submit': device.queue.submit(resolve(op['commands']))
            elif name=='readTexture': reads.append(bytes(device.queue.read_texture(resolve(op['source']),resolve(op['layout']),op['size'])))
            else: raise RuntimeError('Unsupported operation '+name)
            if op.get('id'): refs[op['id']]=result
    finally:
        for resource in owned: resource.destroy()
    raw=b''.join(reads); assert len(raw)==render['expectedBytes']
    actuals[render['id']]=raw
    path=directory/('render-'+str(render['id'])+'.bin'); path.write_bytes(raw)
    targets=[op for op in render['operations'] if op['op']=='readTexture']
    for data,target in zip(reads,targets):
        values=np.frombuffer(data,dtype=np.float16 if target['format']=='rgba16float' else np.float32)
        assert np.isfinite(values).all(),(render['case']['name'],target['format'])
    outputs[str(path.relative_to(OUT))]={'bytes':len(raw),'sha256':sha(raw)}
for render in fixture['renders']:
    replay(render)
    if render['id']%8==0: print('Software images checked',render['id']+1,'/',len(fixture['renders']),flush=True)
comparisons=[]; controls=[]
for pair in fixture['comparisons']:
    before=fixture['renders'][pair['baselineRender']]; after=fixture['renders'][pair['candidateRender']]
    offset=before['compareByteOffset']; assert offset==after['compareByteOffset']
    a=actuals[before['id']]; b=actuals[after['id']]
    differences=sum(x!=y for x,y in zip(a[offset:],b[offset:])); comparisons.append({**pair,'differentBytes':differences})
    colorbytes=16*16*8
    controls.append({'case':pair['case'],'actualCache':after['actualCache'],
        'newToLegacyCacheDifferentBytes':sum(x!=y for x,y in zip(a[:colorbytes],b[:colorbytes])),
        'cacheToLiveDifferentBytes':sum(x!=y for x,y in zip(b[:colorbytes],b[offset:offset+colorbytes]))})
control=next(x for x in controls if x['case']=='unshadowed-lego')
ok=all(x['differentBytes']==0 for x in comparisons) and control['newToLegacyCacheDifferentBytes']>0 and control['cacheToLiveDifferentBytes']==0
report={'scope':'Supplemental software Vulkan exact full-entry output only; Intel hardware and engine integration acceptance pending; no FPS or timing claim',
    'adapter':dict(adapter.info),'fixtureSha256':summary['fixtureSha256'],'sourceSha256':{name:value['sha256'] for name,value in fixture['provenance']['shaderSources'].items()},
    'replaySha256':sha(Path(__file__).read_bytes()),'comparisons':comparisons,'controls':controls,'outputs':outputs,'compiledModules':len(modules),'compiledPipelines':len(pipelines),'ok':ok}
(OUT/'software-result.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'adapter':report['adapter'],'comparisons':len(comparisons),'differences':[x for x in comparisons if x['differentBytes']],'control':control,'ok':ok},indent=2))
if not ok: sys.exit(2)
