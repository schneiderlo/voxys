import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {webcrypto} from 'node:crypto';
const require=createRequire(import.meta.url),api=require('../web/adventure_saves.js');
const world='0102030405060708090a0b0c0d0e0f10';
// Transport fixture only; it deliberately does not assert gameplay semantics.
async function payload(id=world){
    const bytes=new Uint8Array(288);bytes.set(new TextEncoder().encode('VXADHOME'));
    new DataView(bytes.buffer).setUint32(8,1,true);bytes.set(Uint8Array.from(id.match(/../g),v=>parseInt(v,16)),12);
    bytes.set(new Uint8Array(await webcrypto.subtle.digest('SHA-256',bytes.subarray(0,-32))),bytes.length-32);return bytes;
}
function fixture(initial=null){
    const rows=new Map(),metadata=new Map(),calls=[],events=new Map();let rejectPublish=false;
    if(initial)rows.set(world,{generation:3n,payload:initial.slice()});
    const environment={crypto:webcrypto,localStorage:{getItem:key=>metadata.get(key)??null,setItem:(key,value)=>metadata.set(key,value)},
        addEventListener:(type,fn)=>events.set(type,fn),removeEventListener:type=>events.delete(type)};
    environment.VoxyExpeditionStore={async openStore(id,validate,options){
        calls.push({type:'open',id,options});assert.equal(options.databaseName,'voxys-adventure-v1');
        return {
            async load(){const value=rows.get(id)||{generation:0n,payload:new Uint8Array()};if(value.payload.length)assert(await validate(value.payload.slice()));return {...value,payload:value.payload.slice()};},
            async publish(generation,bytes){calls.push({type:'publish',generation});assert(await validate(bytes));if(rejectPublish)throw Error('Storage full.');assert.equal(generation,rows.get(id)?.generation||0n);const value={generation:generation+1n,payload:bytes.slice()};rows.set(id,value);return {generation:value.generation};},
            async close(){calls.push({type:'close'});}
        };
    }};
    return {rows,metadata,calls,events,environment,setFail(value){rejectPublish=value;}};
}
test('bootstrap verifies adventure magic, version, namespace, checksum and bound',async()=>{
    const bytes=await payload();assert(await api.checkArchive(bytes,world,{crypto:webcrypto}));
    for(const at of [0,8,12,100,287]){const bad=bytes.slice();bad[at]^=1;assert.equal(await api.checkArchive(bad,world,{crypto:webcrypto}),false);}
    assert.equal(await api.checkArchive(bytes.subarray(0,287),world,{crypto:webcrypto}),false);
    assert.equal(await api.checkArchive(new Uint8Array(api.maximumPayloadBytes+1),world,{crypto:webcrypto}),false);
});
test('selected missing save refuses and releases owner instead of granting starter supplies',async()=>{
    const f=fixture();await assert.rejects(api.open({world,environment:f.environment}),/missing/);assert(f.calls.some(c=>c.type==='close'));
});
test('restore bytes cannot be saved until full runtime validator accepts them',async()=>{
    const bytes=await payload(),f=fixture(bytes),owner=await api.open({world,environment:f.environment});
    assert.deepEqual(owner.loadedBytes,bytes);await assert.rejects(owner.publish(bytes),/finish loading/);
    await assert.rejects(owner.setValidator(()=>false),/installed world/);assert.equal(f.calls.filter(c=>c.type==='publish').length,0);
    await owner.setValidator(value=>value.length===288);await owner.publish(bytes);assert.equal(owner.generation,4n);
    assert.equal(f.metadata.get(api.metadataKey),world);await owner.close();
});
test('new adventure uses separate world and preserves prior confirmed selection until saved',async()=>{
    const bytes=await payload(),f=fixture(bytes);f.metadata.set(api.metadataKey,world);
    const owner=await api.open({newWorld:true,environment:f.environment});assert.notEqual(owner.world,world);assert.equal(owner.loadedBytes.length,0);
    assert.equal(f.metadata.get(api.metadataKey),world);assert.deepEqual(f.rows.get(world).payload,bytes);
    const next=await payload(owner.world);await owner.setValidator(()=>true);await owner.publish(next);assert.equal(f.metadata.get(api.metadataKey),owner.world);
    assert.deepEqual(f.rows.get(world).payload,bytes);await owner.close();
});
test('default entry reopens latest confirmed adventure and ignores unrelated Cove shortcut',async()=>{
    const bytes=await payload(),f=fixture(bytes);f.metadata.set(api.metadataKey,world);f.metadata.set('voxys.cove.last-confirmed-world.v1','bad');
    const owner=await api.open({environment:f.environment});assert.equal(owner.world,world);assert.equal(owner.generation,3n);await owner.close();
});
test('failed publish preserves confirmed state and retry reconciles generation',async()=>{
    const bytes=await payload(),f=fixture(bytes),owner=await api.open({world,environment:f.environment});await owner.setValidator(()=>true);
    f.setFail(true);await assert.rejects(owner.publish(bytes),/full/);assert.equal(owner.generation,3n);assert.equal(f.metadata.has(api.metadataKey),false);
    f.setFail(false);await owner.publish(bytes);assert.equal(owner.generation,4n);await owner.close();
});
test('denied shortcut metadata does not turn a confirmed save into a failure',async()=>{
    const bytes=await payload(),f=fixture(bytes);f.environment.localStorage={getItem(){throw Error('denied');},setItem(){throw Error('denied');}};
    const owner=await api.open({world,environment:f.environment});await owner.setValidator(()=>true);await owner.publish(bytes);assert.equal(owner.generation,4n);await owner.close();
});
test('copies returned to the caller cannot mutate pending or loaded archive ownership',async()=>{
    const bytes=await payload(),f=fixture(bytes),owner=await api.open({world,environment:f.environment});const out=owner.loadedBytes;out.fill(0);assert.deepEqual(owner.loadedBytes,bytes);
    await owner.setValidator(()=>true);const caller=bytes.slice(),pending=owner.publish(caller);caller.fill(9);await pending;assert.deepEqual(f.rows.get(world).payload,bytes);await owner.close();
});
test('invalid selected ID and conflicting new/resume inputs never open storage',async()=>{
    const f=fixture();for(const id of ['',world.toUpperCase(),'0'.repeat(32)])await assert.rejects(api.open({world:id,environment:f.environment}),/identity/);
    await assert.rejects(api.open({world,newWorld:true,environment:f.environment}),/either/);assert.equal(f.calls.length,0);
});
test('page closure releases the owned world and prevents further publication',async()=>{
    const bytes=await payload(),f=fixture(bytes),owner=await api.open({world,environment:f.environment});await owner.setValidator(()=>true);
    f.events.get('pagehide')();assert.equal(owner.closed,true);await assert.rejects(owner.publish(bytes),/closed/);assert(f.calls.some(c=>c.type==='close'));
});
