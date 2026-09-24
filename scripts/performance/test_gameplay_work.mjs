import {test} from 'node:test';
import assert from 'node:assert/strict';
import {checkGameplayWork} from './gameplay_work.mjs';
function fixture() {
    return {name:'idle',before:{physics:{tick:100}},after:{physics:{tick:400}},workBefore:{},workAfter:{
        'renderPass:scene_terrain_current_sun':300,
        'mappedBytes:physics_debug_readback':300*16,
        'resolve:physics_stage_timestamps':10,
    }};
}
test('accepts active scene with bounded readback and low-rate diagnostics',()=>{
    assert.equal(checkGameplayWork(fixture()).mappedBytesPerTick,16);
});
test('rejects every previous recurring-work regression independently',()=>{
    for(const [key,value] of [
        ['renderPass:opaque_scene_seed',1],
        ['mappedBytes:physics_debug_readback',300*2555920],
        ['resolve:physics_stage_timestamps',300],
    ]) {
        const sample=fixture();sample.workAfter[key]=value;
        assert.throws(()=>checkGameplayWork(sample));
    }
});
test('missing instrumentation cannot pass as zero work',()=>{
    const sample=fixture();sample.workAfter={};assert.throws(()=>checkGameplayWork(sample));
});
test('unlabeled query sets use the labeled resolve buffer',()=>{
    const sample=fixture();delete sample.workAfter['resolve:physics_stage_timestamps'];
    sample.workAfter['resolve:']=300;
    assert.throws(()=>checkGameplayWork(sample),/unavailable/);
    sample.workAfter['resolveInto:physics_stage_timestamp_resolve']=10;
    assert.equal(checkGameplayWork(sample).physicsTimingSamplesPerTick,10/300);
    sample.workAfter['resolveInto:physics_stage_timestamp_resolve']=300;
    assert.throws(()=>checkGameplayWork(sample),/cadence/);
});
test('actual event bursts retain capacity while throwing',()=>{
    const sample=fixture();sample.name='throw';sample.workAfter['mappedBytes:physics_debug_readback']=300*2555920;
    assert.doesNotThrow(()=>checkGameplayWork(sample));
    sample.workAfter['renderPass:opaque_scene_seed']=1;assert.throws(()=>checkGameplayWork(sample));
});
