import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url),api=require('../web/adventure_preferences.js');
const defaults=()=>({version:1,textScale:1,highContrast:false,attack:0,dodge:0});

// Transport fixture only. Production C++ owns the actual preference schema,
// conflict validation, atomic application, input rearming and canonical codec.
function fixture(saved=null){
    const f={calls:[],reads:[],writes:[],value:defaults(),revision:1n,reply:null,readDenied:false,writeDenied:false};
    f.stored=new Map(saved===null?[]:[[api.storageKey,saved]]);
    f.environment={localStorage:{
        getItem(key){f.reads.push(key);if(f.readDenied)throw Error('denied');return f.stored.get(key)??null;},
        setItem(key,value){f.writes.push([key,value]);if(f.writeDenied)throw Error('quota');f.stored.set(key,value);},
    }};
    f.engine={ccall(name,result,types,args){
        assert.equal(name,'adventure_preferences_action');assert.equal(result,'string');assert.deepEqual(types,['number','string']);
        const [kind,text]=args;f.calls.push([kind,text]);
        if(kind===1)return f.reply??JSON.stringify(f.value);
        if(kind===4||kind===5)return 'ok';
        assert.equal(kind,2,'the transport never invents Reset or a menu command');
        let next;try{next=JSON.parse(text);}catch{return 'Invalid saved settings.';}
        if(!next||next.version!==1||typeof next.highContrast!=='boolean'||!Number.isFinite(next.textScale))return 'Invalid saved settings.';
        if(JSON.stringify(next)!==JSON.stringify(f.value)){f.value=next;++f.revision;}
        return 'ok';
    }};
    f.bridge=api.install(f.engine,f.environment);
    // AdventureRuntime::json omits top-level ready; quest.ready is unrelated.
    f.snapshot=()=>({preferencesRevision:String(f.revision),preferencesStatus:'Current status'});
    f.tick=state=>f.bridge.tick(state??f.snapshot());
    f.change=patch=>{Object.assign(f.value,patch);++f.revision;};
    return f;
}

test('startup accepts the runtime revision without a ready field and never stores untouched defaults',()=>{
    const f=fixture();
    for(const state of [{ready:false,preferencesRevision:'1'},{ready:true,failed:true,preferencesRevision:'1'},
        {ready:true,preferencesRevision:1},{ready:true,preferencesRevision:'01'},
        {ready:true,preferencesRevision:'18446744073709551616'}])f.tick(state);
    assert.deepEqual(f.calls,[]);assert.deepEqual(f.reads,[]);
    const state=f.snapshot(),before=structuredClone(state);f.tick(state);assert.deepEqual(state,before);
    assert.deepEqual(f.reads,[api.storageKey]);assert.deepEqual(f.calls,[[1,'']]);assert.deepEqual(f.writes,[]);
    for(let i=0;i<20;++i)f.tick();assert.deepEqual(f.calls,[[1,'']]);f.bridge.cleanup();
});

test('accepted stored settings establish their post-apply baseline before any menu change is saved',()=>{
    const saved=JSON.stringify({...defaults(),textScale:1.5});const f=fixture(saved);
    f.tick();assert.equal(f.value.textScale,1.5);assert.equal(f.revision,2n);assert.deepEqual(f.writes,[]);
    const afterApply=f.calls.length;
    f.tick({ready:true,preferencesRevision:'1'});assert.equal(f.calls.length,afterApply,'pre-apply snapshot cannot establish the baseline');
    f.tick();assert.deepEqual(f.writes,[]);assert.equal(f.stored.get(api.storageKey),saved);
    const steady=f.calls.length;for(let i=0;i<20;++i)f.tick();assert.equal(f.calls.length,steady);
    f.change({highContrast:true});f.tick();
    assert.deepEqual(f.writes,[[api.storageKey,JSON.stringify(f.value)]]);assert.equal(f.calls.at(-1)[0],4);
    const stored=f.calls.length;f.tick();assert.equal(f.calls.length,stored);f.bridge.cleanup();
});

test('identical valid saved values retain original bytes without a startup rewrite',()=>{
    const saved=JSON.stringify(defaults(),null,2),f=fixture(saved);
    f.tick();assert.equal(f.revision,1n);f.tick();assert.equal(f.stored.get(api.storageKey),saved);
    assert.deepEqual(f.writes,[]);assert.equal(f.calls.filter(([kind])=>kind===2).length,1);
    assert.equal(f.calls.filter(([kind])=>kind===4).length,1);f.bridge.cleanup();
});

test('malformed and oversized saved blobs remain intact and are not retried by polling',()=>{
    for(const saved of ['not JSON',JSON.stringify({version:42}), 'x'.repeat(4097),'é'.repeat(3000)]){
        const f=fixture(saved);f.tick();const calls=f.calls.length;
        for(let i=0;i<10;++i)f.tick();
        assert.deepEqual(f.value,defaults());assert.equal(f.stored.get(api.storageKey),saved);
        assert.deepEqual(f.writes,[]);assert.equal(f.calls.length,calls);assert.equal(f.calls.at(-1)[0],5);
        assert.equal(f.calls.filter(([kind])=>kind===2).length,new TextEncoder().encode(saved).length<=4096?1:0);
        f.bridge.cleanup();
    }
});

test('storage denial keeps visit settings usable and later explicit edits retry once',()=>{
    const saved='keep this invalid original',f=fixture(saved);f.readDenied=true;f.writeDenied=true;
    f.tick();assert.deepEqual(f.value,defaults());assert.equal(f.stored.get(api.storageKey),saved);
    f.change({textScale:1.5});f.tick();assert.equal(f.value.textScale,1.5);assert.equal(f.writes.length,1);
    assert.equal(f.calls.at(-1)[0],5);const count=f.calls.length;
    for(let i=0;i<30;++i)f.tick();assert.equal(f.calls.length,count);assert.equal(f.writes.length,1);
    f.writeDenied=false;f.change({highContrast:true});f.tick();assert.equal(f.writes.length,2);
    assert.equal(f.stored.get(api.storageKey),JSON.stringify(f.value));assert.equal(f.calls.at(-1)[0],4);
    assert.deepEqual(f.reads,[api.storageKey],'startup storage is never reloaded over current choices');f.bridge.cleanup();
});

test('a denied storage getter or absent storage cannot stop the game or trigger frame retries',()=>{
    for(const denied of [false,true]){
        const f=fixture();delete f.environment.localStorage;
        if(denied)Object.defineProperty(f.environment,'localStorage',{get(){throw Error('blocked origin');}});
        assert.doesNotThrow(()=>f.tick());f.change({highContrast:true});assert.doesNotThrow(()=>f.tick());
        const count=f.calls.length;f.tick();f.tick();assert.equal(f.calls.length,count);
        assert.equal(f.value.highContrast,true);assert.equal(f.calls.at(-1)[0],5);f.bridge.cleanup();
    }
});

test('explicit Save again or Reset revision can replace bad storage even at default values',()=>{
    const f=fixture('invalid old blob');f.tick();assert.deepEqual(f.writes,[]);
    ++f.revision;f.tick();assert.deepEqual(f.writes,[[api.storageKey,JSON.stringify(defaults())]]);
    assert.equal(f.calls.at(-1)[0],4);f.bridge.cleanup();
});

test('a menu edit before the post-load observation persists the latest canonical settings',()=>{
    const f=fixture(JSON.stringify({...defaults(),textScale:1.5}));f.tick();
    f.change({dodge:2});f.tick();assert.equal(f.writes.length,1);
    assert.equal(f.stored.get(api.storageKey),JSON.stringify(f.value));assert.equal(f.value.textScale,1.5);
    f.tick();assert.equal(f.writes.length,1);f.bridge.cleanup();
});

test('bad canonical transport output never overwrites storage and each revision gets one attempt',()=>{
    for(const reply of ['bad JSON','null','[]','x'.repeat(4097),JSON.stringify({value:'é'.repeat(3000)})]){
        const f=fixture();f.tick();f.reply=reply;f.change({textScale:1.25});f.tick();
        assert.deepEqual(f.writes,[]);assert.equal(f.calls.at(-1)[0],5);const count=f.calls.length;
        f.tick();f.tick();assert.equal(f.calls.length,count);
        f.reply=null;f.change({highContrast:true});f.tick();assert.equal(f.writes.length,1);f.bridge.cleanup();
    }
});

test('decimal revisions stay exact above Number precision and stale observations do not write',()=>{
    const f=fixture();f.revision=9007199254740992n;f.tick();f.change({attack:2});f.tick();
    assert.equal(f.writes.length,1);const calls=f.calls.length;
    f.tick({ready:true,preferencesRevision:'9007199254740992'});assert.equal(f.calls.length,calls);
    f.change({dodge:1});f.tick();assert.equal(f.writes.length,2);f.bridge.cleanup();
});

test('cleanup stops every startup, read, write and status notification without a timer',()=>{
    for(const started of [false,true]){
        const f=fixture();if(started)f.tick();f.bridge.cleanup();f.bridge.cleanup();
        const calls=f.calls.length,reads=f.reads.length;f.change({textScale:1.5});f.tick();
        assert.equal(f.calls.length,calls);assert.equal(f.reads.length,reads);assert.deepEqual(f.writes,[]);
    }
    assert.equal(api.install({},{}),undefined);
});
