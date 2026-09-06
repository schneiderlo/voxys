#!/usr/bin/env python3
"""Execute the shared production LEGO contacts and CCD on a WebGPU adapter.

Requires wgpu and numpy. SwiftShader is supported for correctness, not FPS.
"""
from pathlib import Path
import numpy as np
import wgpu

ROOT = Path(__file__).resolve().parents[1]


def run():
    device = wgpu.gpu.request_adapter_sync(power_preference='low-power').request_device_sync()
    source = (ROOT / 'shaders/physics_ccd.wgsl').read_text() + '''
struct TestInput { centerRadius: vec4<f32>, translation: vec4<f32> };
struct TestOutput { normalDistance: vec4<f32>, pointFeature: vec4<f32>, sweep: vec4<f32>, flags: vec4<f32> };
@group(0) @binding(11) var<storage,read> cases: array<TestInput>;
@group(0) @binding(12) var<storage,read_write> results: array<TestOutput>;
@compute @workgroup_size(1)
fn testSurface(@builtin(global_invocation_id) id: vec3<u32>) {
    let c = cases[id.x];
    let p = c.centerRadius.xyz;
    let r = c.centerRadius.w;
    let contact = legoSphereContact(maxHeightTexture,ccd.terrainOrigin_cell_height,ccd.terrain.xy,p,r);
    let pose = BodyPose(vec4<f32>(p,1.0),vec4<f32>(0,0,0,1));
    let shape = BodyShape(vec4<f32>(vec3<f32>(2.0*r),0.0),vec4<f32>(0),vec4<f32>(0));
    let sweep = sweep_terrain(pose,shape,c.translation.xyz);
    results[id.x] = TestOutput(vec4<f32>(contact.normal,contact.distance),
        vec4<f32>(contact.point,f32(contact.feature)), vec4<f32>(sweep.normal,sweep.fraction),
        vec4<f32>(select(0.0,1.0,sweep.hit),f32(sweep.iterations),0,0));
}
@compute @workgroup_size(64)
fn testLevels(@builtin(global_invocation_id) id: vec3<u32>) {
    results[id.x].normalDistance.x = legoPlateTop(id.x,600.0,1.0);
}
'''
    module = device.create_shader_module(code=source)
    surface = device.create_compute_pipeline(layout='auto',compute={'module':module,'entry_point':'testSurface'})
    levels = device.create_compute_pipeline(layout='auto',compute={'module':module,'entry_point':'testLevels'})
    output = device.create_buffer(size=65536*64,usage=wgpu.BufferUsage.STORAGE|wgpu.BufferUsage.COPY_SRC)
    def dispatch(pipeline, entries, count):
        group=device.create_bind_group(layout=pipeline.get_bind_group_layout(0),entries=entries)
        encoder=device.create_command_encoder()
        compute=encoder.begin_compute_pass()
        compute.set_pipeline(pipeline); compute.set_bind_group(0,group)
        compute.dispatch_workgroups(count); compute.end()
        device.queue.submit([encoder.finish()])
    dispatch(levels,[{'binding':12,'resource':{'buffer':output}}],1024)
    actual=np.frombuffer(device.queue.read_buffer(output),np.float32).reshape(-1,16)[:,0]
    raw=np.arange(65536,dtype=np.uint64)
    expected=-600+((raw*3750+32767)//65535).astype(np.float64)*.32
    np.testing.assert_allclose(actual,expected,atol=.00013,rtol=0)
    print('PASS: all 65,536 GPU plate heights agree with independent integer reference')

    texture=device.create_texture(size=(5,5,1),format='r32uint',usage=wgpu.TextureUsage.TEXTURE_BINDING|wgpu.TextureUsage.COPY_DST)
    field=np.full((5,5),32768,np.uint32)
    params=np.zeros(20,np.float32)
    params.view(np.uint32)[:8]=[1,0,32,0,5,5,12,2]
    params[8:12]=[2,2,1,8]
    params[12:16]=[1,1,.002,0]
    uniform=device.create_buffer_with_data(data=params,usage=wgpu.BufferUsage.UNIFORM)
    diagonal=.05/np.sqrt(2)
    cases=np.array([
        [-.5,.68,-.5,.5, 0,0,0,0],
        [-.16,.09,-.5,.04, 0,0,0,0],
        [-.2+diagonal,.18+diagonal,-.5,.05, 0,0,0,0],
        [-.5,5,-.5,.05, 0,-20,0,0],
        [-1.5,1,-.5,.1, 10,0,0,0],
        [-1.5,10,-.5,.05, 100,0,0,0],
        [-1.5,10,-.5,.05, 1000,0,0,0],
    ],np.float32)
    inputs=device.create_buffer_with_data(data=cases,usage=wgpu.BufferUsage.STORAGE)
    entries=[{'binding':9,'resource':{'buffer':uniform}}, {'binding':10,'resource':texture.create_view()},
             {'binding':11,'resource':{'buffer':inputs}}, {'binding':12,'resource':{'buffer':output}}]
    device.queue.write_texture({'texture':texture},field,{'bytes_per_row':20},(5,5,1))
    dispatch(surface,entries,len(cases))
    result=np.frombuffer(device.queue.read_buffer(output,size=len(cases)*64),np.float32).reshape(-1,16)
    np.testing.assert_allclose(result[:3,3],0,atol=1e-5)
    np.testing.assert_allclose(result[:3,:3],[[0,1,0],[1,0,0],[2**-.5,2**-.5,0]],atol=1e-5)
    assert result[3,12]==1
    np.testing.assert_allclose(result[3,11],(5-.18-.05-.002)/20,atol=2e-6)
    assert result[5,12]==0, 'Clear high-speed path must not report a collision'
    assert result[6,12]==1 and result[6,13]==256 and result[6,11]<1, 'Exhaustion must stop safely'
    field[:,2:]=round((1.6/8+1)*32767.5)
    device.queue.write_texture({'texture':texture},field,{'bytes_per_row':20},(5,5,1))
    dispatch(surface,entries,len(cases))
    result=np.frombuffer(device.queue.read_buffer(output,size=len(cases)*64),np.float32).reshape(-1,16)
    assert result[4,12]==1
    np.testing.assert_allclose(result[4,8:12],[-1,0,0,(1.5-.1-.002)/10],atol=2e-6)
    print('PASS: exact stud cap, side, rim; fast downward and wall impacts; clear sweep; bounded exhaustion')


if __name__ == '__main__':
    run()
