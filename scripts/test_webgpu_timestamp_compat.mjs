import assert from 'node:assert/strict';
import {test} from 'node:test';
import vm from 'node:vm';
import {readFile} from 'node:fs/promises';
const source=await readFile(new URL('../web/loader.js',import.meta.url),'utf8');
const context=vm.createContext({window:{},console});
vm.runInContext(source,context);
const sentinel=0xffffffff;
function mockDevice(){
    const encoders=[];
    const device={features:new Set(['timestamp-query']),limits:{maxStorageBuffersPerShaderStage:8},
        createCommandEncoder(descriptor){
            assert.equal(this,device);
            const encoder={descriptor,beginRenderPass(d){assert.equal(this,encoder);return d;},
                beginComputePass(d){assert.equal(this,encoder);return d;}};
            encoders.push(encoder);return encoder;
        },queue:{submit(){throw new Error('adapter must never submit');}}};
    return {device,encoders};
}
test('normal and absent descriptors use the allocation-free passthrough',()=>{
    for(const d of [undefined,{}, {timestampWrites:{querySet:{},beginningOfPassWriteIndex:0,endOfPassWriteIndex:1}}])
        assert.equal(context.normalizeTimestampDescriptor(d),d);
});
test('render end sentinel is omitted without mutating frozen input',()=>{
    const {device}=mockDevice();context.installTimestampCompatibility(device);
    const q={},writes=Object.freeze({querySet:q,beginningOfPassWriteIndex:0,endOfPassWriteIndex:sentinel});
    const descriptor=Object.freeze({label:'background',timestampWrites:writes,colorAttachments:[]});
    const out=device.createCommandEncoder().beginRenderPass(descriptor);
    assert.equal(out.timestampWrites.querySet,q);assert.equal(out.timestampWrites.beginningOfPassWriteIndex,0);
    assert.equal('endOfPassWriteIndex' in out.timestampWrites,false);
    assert.equal(out.colorAttachments,descriptor.colorAttachments);assert.equal(out.label,'background');
    assert.equal(writes.endOfPassWriteIndex,sentinel);
});
test('compute beginning sentinel is omitted, and a zero end remains valid',()=>{
    const {device}=mockDevice();context.installTimestampCompatibility(device);
    const out=device.createCommandEncoder().beginComputePass({timestampWrites:{
        querySet:{},beginningOfPassWriteIndex:sentinel,endOfPassWriteIndex:0}});
    assert.equal('beginningOfPassWriteIndex' in out.timestampWrites,false);
    assert.equal(out.timestampWrites.endOfPassWriteIndex,0);
});
test('two missing endpoints remain invalid input for native WebGPU validation',()=>{
    const q={};const out=context.normalizeTimestampDescriptor({timestampWrites:{
        querySet:q,beginningOfPassWriteIndex:sentinel,endOfPassWriteIndex:sentinel}});
    assert.equal(out.timestampWrites.querySet,q);
    assert.equal(Object.keys(out.timestampWrites).join(','),'querySet');
});
test('other invalid indices are not hidden or clamped',()=>{
    for(const value of [-1,1000000,2**32,NaN,'4294967295']){
        const d={timestampWrites:{querySet:{},beginningOfPassWriteIndex:0,endOfPassWriteIndex:value}};
        assert.equal(context.normalizeTimestampDescriptor(d),d);
    }
});
test('installation is idempotent and scoped to one device',()=>{
    const first=mockDevice(),second=mockDevice();const untouched=second.device.createCommandEncoder;
    context.installTimestampCompatibility(first.device);const wrapped=first.device.createCommandEncoder;
    context.installTimestampCompatibility(first.device);assert.equal(first.device.createCommandEncoder,wrapped);
    assert.equal(second.device.createCommandEncoder,untouched);
    const d={timestampWrites:{querySet:{},endOfPassWriteIndex:sentinel}};
    assert.equal(second.device.createCommandEncoder().beginRenderPass(d),d);
});
test('encoder descriptors, return values and multiple encoders are preserved',()=>{
    const {device,encoders}=mockDevice();context.installTimestampCompatibility(device);
    const d={label:'frame'};const first=device.createCommandEncoder(d),second=device.createCommandEncoder();
    assert.equal(first.descriptor,d);assert.notEqual(first,second);assert.equal(encoders.length,2);
    const pass={timestampWrites:{beginningOfPassWriteIndex:0,endOfPassWriteIndex:1}};
    assert.equal(first.beginComputePass(pass),pass);assert.equal(second.beginRenderPass(pass),pass);
});
test('production device request installs the bridge',async()=>{
    const {device}=mockDevice();const adapter={limits:{maxStorageBuffersPerShaderStage:8},
        features:new Set(['timestamp-query']),info:{vendor:'test'},async requestDevice(d){
            assert.equal(d.requiredFeatures[0],'timestamp-query');return device;}};
    const result=await context.window.VoxyLoader.requestVoxyDevice(adapter,{enableTimestamps:true});
    assert.equal(result.device,device);assert.equal(result.profile.timestampQuery,true);
    const out=device.createCommandEncoder().beginRenderPass({timestampWrites:{
        querySet:{},beginningOfPassWriteIndex:0,endOfPassWriteIndex:sentinel}});
    assert.equal('endOfPassWriteIndex' in out.timestampWrites,false);
});
