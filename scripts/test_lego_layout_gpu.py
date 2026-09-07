#!/usr/bin/env python3
"""Run production material lookups: world tags, fallback palette and grouping.

Uses a deliberately tiny wrapping atlas to exercise eviction, with an actual
height field. SwiftShader checks correctness; its timings are not an FPS claim.
"""
from pathlib import Path
import numpy as np
import wgpu

ROOT = Path(__file__).resolve().parents[1]

def run():
    device = wgpu.gpu.request_adapter_sync(power_preference='low-power').request_device_sync()
    source = (ROOT/'shaders/ray_blit.wgsl').read_text()+'''
@group(0) @binding(21) var<storage,read> testCells: array<vec2<i32>>;
@group(0) @binding(22) var<storage,read_write> testResults: array<vec4<u32>>;
@compute @workgroup_size(1)
fn testLayout(@builtin(global_invocation_id) id: vec3<u32>) {
    let cell = testCells[id.x];
    let origin = 0.5*(camera.terrainSize-vec2<f32>(1));
    let p = vec3<f32>(f32(cell.x)+0.01-origin.x,0,f32(cell.y)+0.8-origin.y);
    let material = sampleLegoStudy(p,p,p,vec3<f32>(0,1,0),0);
    testResults[id.x] = vec4<u32>(legoMaterialLayout(cell),bitcast<vec3<u32>>(material.albedo));
}
'''
    shader = device.create_shader_module(code=source)
    pipeline = device.create_compute_pipeline(layout='auto',compute={'module':shader,'entry_point':'testLayout'})
    n=129
    raw=np.full((n,n),32768,np.uint32)
    # Families: sand, meadow, forest, steep warm stone.
    def encode(y):return int(np.floor((y/600+1)*32767.5+0.5))
    raw[:,:32]=encode(-201)
    raw[:,32:64]=encode(-195)
    raw[:,64:96]=encode(-182)
    raw[:,96:]=encode(-190)
    raw[:,99:]=encode(-180)
    terrain=device.create_texture(size=(n,n,1),format='r32uint',usage=wgpu.TextureUsage.TEXTURE_BINDING|wgpu.TextureUsage.COPY_DST)
    device.queue.write_texture({'texture':terrain},raw,{'bytes_per_row':n*4},(n,n,1))
    atlas=device.create_texture(size=(64,64,1),format='r32uint',usage=wgpu.TextureUsage.TEXTURE_BINDING|wgpu.TextureUsage.COPY_DST)
    metadata=np.zeros((64,64),np.uint32)
    # These two cells wrap to the same atlas location. Only world chunk 2 owns it.
    packed=0x1000|1|(1<<4)|(3<<6)|(4<<8)
    metadata[8,1]=packed|(2<<16)
    device.queue.write_texture({'texture':atlas},metadata,{'bytes_per_row':64*4},(64,64,1))
    cells=np.array([[1,8],[65,8],[10,10],[40,10],[80,10],[98,10],[128,128]],np.int32)
    uniform_data=np.zeros(136,np.float32)
    uniform_data[48:52]=[n,n,1/n,1/n]
    uniform_data[52:56]=[600,1,1,0]
    uniform_data[62]=4
    # waterParams follows the light vectors and six frustum planes.
    uniform_data[96]=-200
    uniform=device.create_buffer_with_data(data=uniform_data,usage=wgpu.BufferUsage.UNIFORM|wgpu.BufferUsage.COPY_DST)
    inputs=device.create_buffer_with_data(data=cells,usage=wgpu.BufferUsage.STORAGE)
    output=device.create_buffer(size=len(cells)*16,usage=wgpu.BufferUsage.STORAGE|wgpu.BufferUsage.COPY_SRC)
    group=device.create_bind_group(layout=pipeline.get_bind_group_layout(0),entries=[
        {'binding':0,'resource':{'buffer':uniform}}, {'binding':13,'resource':terrain.create_view()},
        {'binding':20,'resource':atlas.create_view()}, {'binding':21,'resource':{'buffer':inputs}},
        {'binding':22,'resource':{'buffer':output}}])
    def dispatch(mode):
        uniform_data[62]=mode;device.queue.write_buffer(uniform,0,uniform_data)
        encoder=device.create_command_encoder();p=encoder.begin_compute_pass()
        p.set_pipeline(pipeline);p.set_bind_group(0,group);p.dispatch_workgroups(len(cells));p.end()
        device.queue.submit([encoder.finish()])
        return np.frombuffer(device.queue.read_buffer(output),np.uint32).reshape(-1,4).copy()
    grouped=dispatch(4);single=dispatch(5)
    assert grouped[1,0]==metadata[8,1], 'valid nearby brick was not reused'
    assert grouped[0,0] != metadata[8,1], 'stale wrapped chunk leaked into world'
    def top(x,z):
        x=int(np.clip(x,0,n-2));z=int(np.clip(z,0,n-2))
        return -600+((int(raw[z,x])*3750+32767)//65535)*.32
    for i,(x,z) in enumerate(cells):
        if i==1:continue
        x=int(np.clip(x,0,n-2));z=int(np.clip(z,0,n-2));y=top(x,z)+200
        relief=max(abs(top(x-2,z)-top(x+2,z)),abs(top(x,z-2)-top(x,z+2)))
        family=0 if y<2.4 else 3 if y>6 and relief>1.8 else 2 if y>12 else 1
        h=(((x+173)*374761393)^((z+419)*668265263))&0xffffffff
        h=((h^(h>>13))*1274126177)&0xffffffff; shadehash=(h^(h>>16))%10
        shade=0 if shadehash<2 else 2 if shadehash>=8 else 1
        assert grouped[i,0]==0x1000|((family*3+shade)<<8),(i,grouped[i,0],family)
    assert np.isfinite(grouped[:,1:].view(np.float32)).all()
    # K removes only the group seam: all cached palette ownership stays fixed.
    np.testing.assert_array_equal(grouped[:,0],single[:,0])
    assert np.max(grouped[1,1:].view(np.float32))>np.max(single[1,1:].view(np.float32))
    print('PASS: production world lookup, stale-tag rejection, map edges, four palette families and grouped/single seams')

if __name__=='__main__':run()
