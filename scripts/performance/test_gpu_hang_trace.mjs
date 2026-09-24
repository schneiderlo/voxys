import test from 'node:test';
import assert from 'node:assert/strict';
import {installGpuHangTrace,splitGpuPasses} from './gpu_hang_trace.mjs';

function fakeGpu(){
    class Pass {
        constructor(encoder){this.encoder=encoder;}
        setPipeline(){}
        end(){this.encoder.events.push('end');}
    }
    class Compute extends Pass{}
    class Render extends Pass{}
    for(const name of ['dispatchWorkgroups','dispatchWorkgroupsIndirect'])Compute.prototype[name]=function(){this.encoder.events.push(name);};
    for(const name of ['draw','drawIndexed','drawIndirect','drawIndexedIndirect'])Render.prototype[name]=function(){this.encoder.events.push(name);};
    class Encoder {
        constructor(){this.events=[];}
        beginComputePass(){this.events.push('compute');return new Compute(this);}
        beginRenderPass(){this.events.push('render');return new Render(this);}
        finish(){return {events:[...this.events]};}
    }
    for(const name of ['copyBufferToBuffer','copyTextureToBuffer','copyBufferToTexture','copyTextureToTexture','resolveQuerySet','clearBuffer'])Encoder.prototype[name]=function(){this.events.push(name);};
    class Queue {
        batches=[];fences=[];
        submit(commands){this.batches.push(commands.flatMap(c=>c.events));}
        onSubmittedWorkDone(){return new Promise(resolve=>this.fences.push(resolve));}
    }
    class Device {
        features=new Set();queue=new Queue();
        lost=new Promise(resolve=>{this.lose=resolve;});
        addEventListener(){}
        createCommandEncoder(){return new Encoder();}
    }
    class Adapter {
        info={vendor:'test',architecture:'test'};
        async requestDevice(){return new Device();}
    }
    Object.assign(globalThis,{GPUAdapter:Adapter,GPUDevice:Device,GPUQueue:Queue,GPUCommandEncoder:Encoder,GPUComputePassEncoder:Compute,GPURenderPassEncoder:Render});
    return new Adapter();
}

test('fences settling after device loss are not successful GPU completions',async()=>{
    const adapter=fakeGpu();installGpuHangTrace();
    const device=await adapter.requestDevice();
    device.queue.submit([device.createCommandEncoder().finish()]);
    device.lose({reason:'unknown',message:'simulated hang'});await Promise.resolve();
    device.queue.fences[0]();await Promise.resolve();
    assert.equal(voxyGpuTrace.completed,0);
    assert.equal(voxyGpuTrace.lost[0].submitted,1);
    assert.equal(voxyGpuTrace.records[0].end,undefined);
    assert.equal(typeof voxyGpuTrace.records[0].settledAfterLoss,'number');
});

test('pass splitting preserves copy, compute and render ordering',async()=>{
    const adapter=fakeGpu();installGpuHangTrace();splitGpuPasses();
    const device=await adapter.requestDevice(),encoder=device.createCommandEncoder();
    // The page installs this kind of per-encoder timestamp compatibility wrap.
    for(const name of ['beginComputePass','beginRenderPass']){
        const begin=encoder[name];
        encoder[name]=function(descriptor){return Reflect.apply(begin,this,[descriptor]);};
    }
    encoder.copyBufferToBuffer();
    const compute=encoder.beginComputePass({label:'simulation'});
    compute.setPipeline({label:'step'});compute.dispatchWorkgroups(2,3,1);compute.end();
    encoder.clearBuffer();
    const render=encoder.beginRenderPass({label:'scene'});
    render.setPipeline({label:'draw'});render.draw(3);render.end();
    device.queue.submit([encoder.finish()]);
    assert.deepEqual(device.queue.batches,[['copyBufferToBuffer','compute','dispatchWorkgroups','end'],['clearBuffer','render','draw','end'],[]]);
    assert.equal(voxyGpuTrace.records[0].commands[0].operations['compute:simulation:step:dispatchWorkgroups'].maxGroups,6);
    assert.equal(voxyGpuTrace.records[1].commands[0].operations['render:scene:draw:draw'].count,1);
});

test('diagnostic omission is explicit and limited to matching dispatches',async()=>{
    const adapter=fakeGpu();installGpuHangTrace({skip:':suspect:'});
    const device=await adapter.requestDevice(),encoder=device.createCommandEncoder();
    const pass=encoder.beginComputePass();
    pass.setPipeline({label:'suspect'});pass.dispatchWorkgroups(1);
    pass.setPipeline({label:'control'});pass.dispatchWorkgroups(1);pass.end();
    device.queue.submit([encoder.finish()]);
    assert.deepEqual(device.queue.batches,[['compute','dispatchWorkgroups','end']]);
    assert.equal(voxyGpuTrace.records[0].commands[0].operations['SKIPPED:compute::suspect:dispatchWorkgroups'].count,1);
});
