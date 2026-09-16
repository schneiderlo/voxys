import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {webcrypto} from 'node:crypto';
const require=createRequire(import.meta.url),api=require('../web/adventure_saves.js');
const world='0102030405060708090a0b0c0d0e0f10';
// Transport fixture only; it deliberately does not assert gameplay semantics.
async function payload(id=world,schema=2){
    const bytes=new Uint8Array(288);bytes.set(new TextEncoder().encode('VXADHOME'));
    new DataView(bytes.buffer).setUint32(8,schema,true);bytes.set(Uint8Array.from(id.match(/../g),v=>parseInt(v,16)),12);
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

test('legacy bootstrap and schema2 publication retain one world and wait for confirmed storage',async()=>{
    const legacy=await payload(world,1),current=await payload(world,2),f=fixture(legacy);
    const owner=await api.open({world,environment:f.environment});
    assert.deepEqual(owner.loadedBytes,legacy);assert.equal(owner.generation,3n);
    await assert.rejects(owner.publish(current),/finish loading/);
    await owner.setValidator(bytes=>[1,2].includes(new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength).getUint32(8,true)));
    assert.deepEqual(f.rows.get(world).payload,legacy); // Validating the migration is read-only.
    f.setFail(true);await assert.rejects(owner.publish(current),/full/);assert.deepEqual(f.rows.get(world).payload,legacy);
    f.setFail(false);await owner.publish(current);assert.equal(owner.generation,4n);assert.deepEqual(f.rows.get(world).payload,current);
    assert.equal(f.metadata.get(api.metadataKey),world);await owner.close();
    const reopened=await api.open({world,environment:f.environment});assert.deepEqual(reopened.loadedBytes,current);await reopened.close();
});
test('bootstrap accepts only explicit old and new schemas even with a valid checksum',async()=>{
    for(const schema of [1,2,3,4,5,6])assert(await api.checkArchive(await payload(world,schema),world,{crypto:webcrypto}));
    for(const schema of [0,7,255,0xffffffff])assert.equal(await api.checkArchive(await payload(world,schema),world,{crypto:webcrypto}),false);
});

test('schema2 to schema3 keeps confirmed bytes until validated publication succeeds',async()=>{
    const previous=await payload(world,2),current=await payload(world,3),f=fixture(previous);
    const owner=await api.open({world,environment:f.environment});
    await assert.rejects(owner.publish(current),/finish loading/);
    await owner.setValidator(bytes=>[2,3].includes(new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength).getUint32(8,true)));
    assert.deepEqual(f.rows.get(world).payload,previous);
    f.setFail(true);await assert.rejects(owner.publish(current),/full/);assert.deepEqual(f.rows.get(world).payload,previous);
    f.setFail(false);await owner.publish(current);assert.deepEqual(f.rows.get(world).payload,current);
    assert.equal(owner.world,world);assert.equal(owner.generation,4n);await owner.close();
});
test('schema3 to schema4 waits for runtime acceptance and preserves the old world across failed publication and reopen',async()=>{
    const previous=await payload(world,3),current=await payload(world,4),f=fixture(previous);
    f.metadata.set(api.metadataKey,world);
    const owner=await api.open({environment:f.environment});
    assert.equal(owner.world,world);assert.equal(owner.generation,3n);assert.deepEqual(owner.loadedBytes,previous);
    await assert.rejects(owner.publish(current),/finish loading/);
    await assert.rejects(owner.setValidator(()=>false),/installed world/);
    await assert.rejects(owner.publish(current),/finish loading/);
    assert.equal(f.calls.filter(c=>c.type==='publish').length,0,'preparing or refusing migration cannot publish');
    const accepted=bytes=>[3,4].includes(new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength).getUint32(8,true));
    await owner.setValidator(accepted);assert.deepEqual(f.rows.get(world).payload,previous);
    f.setFail(true);await assert.rejects(owner.publish(current),/full/);
    assert.equal(owner.generation,3n);assert.deepEqual(owner.loadedBytes,previous);
    assert.deepEqual(f.rows.get(world).payload,previous);assert.equal(f.metadata.get(api.metadataKey),world);
    await owner.close();
    const reopened=await api.open({environment:f.environment});
    assert.equal(reopened.world,world);assert.deepEqual(reopened.loadedBytes,previous);
    await assert.rejects(reopened.publish(current),/finish loading/);
    await reopened.setValidator(accepted);f.setFail(false);await reopened.publish(current);
    assert.equal(reopened.generation,4n);assert.deepEqual(reopened.loadedBytes,current);
    assert.equal(f.rows.size,1,'migration must retain the same world namespace');await reopened.close();
    const confirmed=await api.open({environment:f.environment});
    assert.equal(confirmed.world,world);assert.equal(confirmed.generation,4n);assert.deepEqual(confirmed.loadedBytes,current);
    await confirmed.close();
});
for(const [oldSchema,newSchema] of [[4,5],[5,6]])test(`schema${oldSchema} to schema${newSchema} retains the old checkpoint until validated migration is confirmed`,async()=>{
    const previous=await payload(world,oldSchema),current=await payload(world,newSchema),f=fixture(previous);
    const owner=await api.open({world,environment:f.environment});
    assert.deepEqual(owner.loadedBytes,previous);await assert.rejects(owner.publish(current),/finish loading/);
    await assert.rejects(owner.setValidator(()=>false),/installed world/);
    assert.equal(f.calls.filter(c=>c.type==='publish').length,0);
    await owner.setValidator(bytes=>[oldSchema,newSchema].includes(new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength).getUint32(8,true)));
    assert.deepEqual(f.rows.get(world).payload,previous,'loading and validating the new recipe rules must not write a checkpoint');
    f.setFail(true);await assert.rejects(owner.publish(current),/full/);
    assert.equal(owner.generation,3n);assert.deepEqual(owner.loadedBytes,previous);assert.deepEqual(f.rows.get(world).payload,previous);
    f.setFail(false);await owner.publish(current);assert.equal(owner.generation,4n);
    assert.equal(owner.world,world);assert.deepEqual(f.rows.get(world).payload,current);await owner.close();
    const reopened=await api.open({environment:f.environment});assert.equal(reopened.world,world);
    assert.deepEqual(reopened.loadedBytes,current);assert.equal(reopened.generation,4n);await reopened.close();
});

// Separate real profile keys, with one shared storage backend.
test('creative saves and shortcuts stay isolated from adventure worlds',async()=>{
    const rows=new Map(),metadata=new Map([[api.metadataKey,world]]),opened=[];
    const bytes=await payload();rows.set('voxys-adventure-v1:'+world,{generation:3n,payload:bytes});
    const environment={crypto:webcrypto,localStorage:{getItem:k=>metadata.get(k),setItem:(k,v)=>metadata.set(k,v)}};
    environment.VoxyExpeditionStore={async openStore(id,validate,{databaseName}){
        opened.push(databaseName);const key=databaseName+':'+id;
        return {async load(){return rows.get(key)||{generation:0n,payload:new Uint8Array()};},
            async publish(generation,payload){assert(await validate(payload));rows.set(key,{generation:generation+1n,payload});return {generation:generation+1n};},async close(){}};
    }};
    const creative=await api.open({profile:'build',environment});
    assert.notEqual(creative.world,world);assert.equal(creative.loadedBytes.length,0);
    await creative.setValidator(()=>true);const buildBytes=await payload(creative.world,6);await creative.publish(buildBytes);await creative.close();
    assert.equal(metadata.get(api.metadataKey),world);assert.equal(api.confirmedWorld(environment,'build'),creative.world);
    const reopened=await api.open({profile:'build',environment});assert.deepEqual(reopened.loadedBytes,buildBytes);await reopened.close();
    const legacy=await api.open({environment});assert.equal(legacy.world,world);assert.deepEqual(legacy.loadedBytes,bytes);await legacy.close();
    assert.deepEqual(opened,['voxys-free-build-v1','voxys-free-build-v1','voxys-adventure-v1']);
    await assert.rejects(api.open({profile:'unknown',environment}),/profile/);
    await assert.rejects(api.open({profile:'build',world,environment}),/missing/);
});
