// Exercise web/data_packs.js: early downloads, content-addressed caching,
// stale-release eviction and fallback to Emscripten's own fetch.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const source=readFileSync(new URL('../web/data_packs.js',import.meta.url),'utf8');
const base='https://example.test/voxys/index.html';
const hash=c=>c.repeat(64);

function load(){
    const context={console:{warn(){},error(){}},URL,URLSearchParams,Response};
    context.globalThis=context;
    vm.runInNewContext(source,context);
    return context.VoxyDataPacks;
}
function bytes(size,fill){return new Uint8Array(size).fill(fill);}
function environment(files,cache=new Map()){
    const fetched=[];
    const absolute=url=>new URL(url,base).href;
    const store={
        async match(url){const hit=cache.get(absolute(url));return hit&&new Response(hit);},
        async put(url,response){cache.set(absolute(url),new Uint8Array(await response.arrayBuffer()));},
        async keys(){return [...cache.keys()].map(url=>({url}));},
        async delete(request){return cache.delete(request.url);},
    };
    return {fetched,cache,env:{
        location:{href:base},
        caches:{open:async()=>store},
        async fetch(url){
            fetched.push(url);
            const body=files[url.split('?')[0]];
            return body?new Response(body):new Response(null,{status:404});
        },
    }};
}
const settle=()=>new Promise(resolve=>setTimeout(resolve,10));

test('manifest parsing accepts only a written release manifest',()=>{
    const api=load();
    assert.equal(api.parseManifest('__VOXY_RELEASE_FILES__'),null);
    assert.equal(api.parseManifest('{"voxy_wasm.data":{"sha256":"abc","size":4}}'),null);
    assert.equal(api.parseManifest('{"../x.data":{"sha256":"'+hash('a')+'","size":4}}'),null);
    assert.deepEqual(Object.keys(api.parseManifest('{"voxy_wasm.data":{"sha256":"'+hash('a')+'","size":4}}')),['voxy_wasm.data']);
});

test('LEGO-terrain experiences need no optional pack; others request theirs',()=>{
    const api=load();
    for(const experience of ['build','adventure','lego','lego-world'])assert.deepEqual([...api.packsFor(experience)],[]);
    assert.deepEqual([...api.packsFor('salvage-cove')],['salvage']);
    assert.deepEqual([...api.packsFor('ridgebreak')],['moto','materials']);
    assert.deepEqual([...api.packsFor('terrain')],['materials']);
});

test('first visit downloads, a later visit with the same hashes reads the cache',async()=>{
    const api=load();
    const manifest={'voxy_wasm.data':{sha256:hash('a'),size:1000},'voxy_moto.data':{sha256:hash('b'),size:10},'voxy_salvage.data':{sha256:hash('c'),size:20}};
    const files={'voxy_wasm.data':bytes(1000,1),'voxy_moto.data':bytes(10,2),'voxy_salvage.data':bytes(20,3)};
    const first=environment(files);
    const progress=[];
    const run=api.start({manifest,packs:['moto'],environment:first.env});
    run.onProgress=s=>progress.push(s.loaded);
    await run.ready;await settle();
    assert.deepEqual(first.fetched.sort(),[`voxy_moto.data?h=${hash('b')}`,`voxy_wasm.data?h=${hash('a')}`]);
    assert.equal(run.total,1010);assert.equal(progress.at(-1),1010);assert.equal(run.cachedFiles,0);
    assert.equal(run.take(`voxy_wasm.data?h=${hash('a')}`,999),null,'size mismatch is refused');
    assert.equal(run.take(`voxy_wasm.data?h=${hash('a')}`,1000).byteLength,1000);
    assert.equal(run.take('voxy_wasm.data',1000),null,'a pack is handed over once');

    const second=environment(files,first.cache);
    const again=api.start({manifest,packs:['moto'],environment:second.env});
    await again.ready;
    assert.deepEqual(second.fetched,[]);assert.equal(again.cachedFiles,2);
    assert.deepEqual(new Uint8Array(again.take('voxy_moto.data',10)),bytes(10,2));
});

test('a new release evicts only files whose hashes changed',async()=>{
    const api=load();
    const files={'voxy_wasm.data':bytes(8,1),'voxy_moto.data':bytes(4,2)};
    const old=environment(files);
    await api.start({manifest:{'voxy_wasm.data':{sha256:hash('a'),size:8},'voxy_moto.data':{sha256:hash('b'),size:4}},packs:['moto'],environment:old.env}).ready;
    await settle();
    const next=environment({...files,'voxy_wasm.data':bytes(8,9)},old.cache);
    const manifest={'voxy_wasm.data':{sha256:hash('d'),size:8},'voxy_moto.data':{sha256:hash('b'),size:4}};
    await api.start({manifest,packs:[],environment:next.env}).ready;await settle();
    assert.deepEqual(next.fetched,[`voxy_wasm.data?h=${hash('d')}`]);
    assert.deepEqual([...next.cache.keys()].sort(),[
        `https://example.test/voxys/voxy_moto.data?h=${hash('b')}`,
        `https://example.test/voxys/voxy_wasm.data?h=${hash('d')}`]);
});

test('a damaged cached copy is downloaded again',async()=>{
    const api=load();
    const manifest={'voxy_wasm.data':{sha256:hash('a'),size:8}};
    const cache=new Map([[`https://example.test/voxys/voxy_wasm.data?h=${hash('a')}`,bytes(5,1)]]);
    const run=environment({'voxy_wasm.data':bytes(8,1)},cache);
    const state=api.start({manifest,packs:[],environment:run.env});await state.ready;
    assert.equal(run.fetched.length,1);assert.equal(state.loaded,8);assert.equal(state.take('voxy_wasm.data',8).byteLength,8);
});

test('failed downloads leave the pack to Emscripten',async()=>{
    const api=load();
    const manifest={'voxy_wasm.data':{sha256:hash('a'),size:8}};
    const run=environment({});
    const state=api.start({manifest,packs:[],environment:run.env});await state.ready;
    assert.equal(state.take('voxy_wasm.data',8),null);assert.equal(state.loaded,0);
});

test('Emscripten file URLs are content-addressed when the manifest knows them',()=>{
    const api=load();
    const manifest={'voxy_wasm.wasm':{sha256:hash('e'),size:8}};
    assert.equal(api.locate('voxy_wasm.wasm','',manifest,'b1'),`voxy_wasm.wasm?h=${hash('e')}`);
    assert.equal(api.locate('voxy_salvage.data','/p/',manifest,'b1'),'/p/voxy_salvage.data?v=b1');
    assert.equal(api.locate('voxy_wasm.data','',null,'b 1'),'voxy_wasm.data?v=b%201');
});
