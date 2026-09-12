import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {webcrypto} from 'node:crypto';
import {readFile} from 'node:fs/promises';
const require=createRequire(import.meta.url),api=require('../web/cove_saves.js');
const world='0102030405060708090a0b0c0d0e0f10';
const shortcutKey='voxys.cove.last-confirmed-world.v1';
function fixture(){
    const calls=[],events=new Map(),metadata=new Map();let finish,fail;
    const publication=new Promise((resolve,reject)=>{finish=resolve;fail=reject;});
    const engine={
        ccall(name,result,types,args){calls.push({name,args});if(name==='voxy_stage_cove_resume')return 1;
            if(args[0]===3)return 'aabb';if([2,4,5,6,7].includes(args[0]))return 'ok';return '';},
        callMain(args){calls.push({name:'main',args});},_voxy_is_initialized:()=>1,
        _voxy_get_salvage_preview_json:()=>JSON.stringify({world,restore:{phase:'awaiting-storage'}}),UTF8ToString:value=>value
    };
    const store={closed:false,validate:null,
        async load(){assert.equal(await this.validate(new Uint8Array([1,2])),true);return {generation:8n,payload:new Uint8Array([1,2])};},
        async publish(generation,payload){calls.push({name:'publish',generation,payload:[...payload]});return publication;},
        async close(){this.closed=true;calls.push({name:'close'});}
    };
    const environment={crypto:webcrypto,setTimeout,
        localStorage:{getItem:key=>metadata.get(key)??null,setItem(key,value){calls.push({name:'remember',key,value});metadata.set(key,value);}},
        addEventListener(name,fn){events.set(name,fn);},removeEventListener(name){events.delete(name);},
        VoxyExpeditionStore:{async openStore(id,validate){assert.equal(id,world);store.validate=validate;return store;}}
    };
    const until=async predicate=>{for(let i=0;i<100&&!predicate();i++)await new Promise(r=>setTimeout(r,1));assert(predicate());};
    return {engine,store,environment,calls,events,finish,fail,until,metadata};
}
test('restoration cannot activate before the paired lineage is committed',async()=>{
    const f=fixture(),pending=api.resume(f.engine,world,['--config','salvage_cove.cfg'],f.environment);
    await f.until(()=>f.calls.some(c=>c.name==='publish'));
    assert(!f.calls.some(c=>c.args?.[0]===4));assert.equal(f.calls.filter(c=>c.name==='main').length,1);
    f.finish({generation:9n});await pending;
    const release=f.calls.find(c=>c.args?.[0]===4);assert(release);assert.match(release.args[1],/^[0-9a-f]{64}$/);
    assert.equal(f.store.closed,false,'successful owner stays locked for gameplay');
});
test('failed publication closes admission and never activates the saved world',async()=>{
    const f=fixture(),pending=api.resume(f.engine,world,[],f.environment);
    await f.until(()=>f.calls.some(c=>c.name==='publish'));f.fail(Error('disk full'));
    await assert.rejects(pending,/disk full/);
    assert(!f.calls.some(c=>c.args?.[0]===4));assert(f.calls.some(c=>c.args?.[0]===5));assert(f.store.closed);
});
test('missing selected world cannot launch a fresh world or grant materials',async()=>{
    const f=fixture();f.store.load=async()=>({generation:0n,payload:new Uint8Array()});
    await assert.rejects(api.resume(f.engine,world,[],f.environment),/missing/);
    assert(!f.calls.some(c=>c.name==='main'||c.name==='publish'));assert(f.store.closed);
});
test('page closure during publication cannot release a retired owner',async()=>{
    const f=fixture(),pending=api.resume(f.engine,world,[],f.environment);
    await f.until(()=>f.calls.some(c=>c.name==='publish'));f.events.get('pagehide')();f.finish({generation:9n});
    await assert.rejects(pending,/finish loading/);
    assert(!f.calls.some(c=>c.args?.[0]===4));assert(f.calls.some(c=>c.args?.[0]===5));assert(f.store.closed);
});
test('invalid selected world refuses without opening storage or starting the game',async()=>{
    const f=fixture();for(const id of ['',world.toUpperCase(),'0'])await assert.rejects(api.resume(f.engine,id,[],f.environment),/invalid/);
    assert.deepEqual(f.calls,[]);
});
function saveFixture(){
    const f=fixture();
    const elements=Object.fromEntries(['salvage-save','salvage-save-status','salvage-save-help'].map(id=>[id,{
        hidden:true,disabled:true,textContent:'',listeners:new Map(),
        addEventListener(name,fn){this.listeners.set(name,fn);},removeEventListener(name){this.listeners.delete(name);}
    }]));
    f.environment.document={getElementById:id=>elements[id]};
    f.environment.location={href:'https://example.test/?experience=salvage-cove'};
    f.environment.history={replaceState(_state,_title,url){f.calls.push({name:'address',url});}};
    f.store.load=async()=>({generation:0n,payload:new Uint8Array()});
    const ccall=f.engine.ccall.bind(f.engine);
    f.engine.ccall=(name,result,types,args)=>args?.[0]===1?'aabb':ccall(name,result,types,args);
    const ui=api.install(f.engine,f.environment),button=elements['salvage-save'];
    return {...f,elements,ui,click:()=>button.listeners.get('click')(),
        ready:()=>ui.tick({world,ready:true,failed:false,pause:{phase:'paused'}})};
}
test('manual save is pause-only, hides outside the cove, and acknowledges actual publication',async()=>{
    const f=saveFixture(),button=f.elements['salvage-save'],status=f.elements['salvage-save-status'];
    f.ui.tick({ready:true,pause:{phase:'running'}});assert(button.hidden);assert(f.elements['salvage-save-help'].hidden);
    f.ui.tick({world,ready:true,pause:{phase:'running'}});assert(button.disabled);
    f.ready();assert(!button.disabled);assert(!f.elements['salvage-save-help'].hidden);
    const saving=f.click();await f.until(()=>f.calls.some(c=>c.name==='publish'));
    assert(button.disabled);assert.equal(status.textContent,'Saving expedition…');assert(!f.calls.some(c=>c.name==='address'));
    await f.click();assert.equal(f.calls.filter(c=>c.name==='publish').length,1,'one bounded pending save');
    f.finish({generation:1n});await saving;assert.match(status.textContent,/^Saved\./);
    assert.equal(new URL(f.calls.find(c=>c.name==='address').url).searchParams.get('world'),world);
    f.ui.cleanup();assert(f.store.closed);assert(f.calls.some(c=>c.args?.[0]===5));
});
test('failed manual save remains visibly unsaved and never changes the return address',async()=>{
    const f=saveFixture();f.ready();const saving=f.click();await f.until(()=>f.calls.some(c=>c.name==='publish'));
    f.fail(Error('Browser storage is full'));await saving;
    assert.match(f.elements['salvage-save-status'].textContent,/Save failed.*storage is full/);
    assert(!f.calls.some(c=>c.name==='address'));f.ui.cleanup();
});
test('closing the save UI while publication is pending revokes the owner without reporting success',async()=>{
    const f=saveFixture();f.ready();const saving=f.click();await f.until(()=>f.calls.some(c=>c.name==='publish'));
    f.ui.cleanup();f.finish({generation:1n});await saving;
    assert(!f.calls.some(c=>c.name==='address'));assert(f.calls.some(c=>c.args?.[0]===5));assert(f.store.closed);
});
test('delivery freezes until the exact archive publication completes, then acknowledges once',async()=>{
    const f=saveFixture(),state={world,ready:true,failed:false,pause:{phase:'paused'},job:{savePending:true,secured:true}};
    f.ui.tick(state);await f.until(()=>f.calls.some(c=>c.name==='publish'));
    assert(f.calls.some(c=>c.args?.[0]===6));assert(!f.calls.some(c=>c.args?.[0]===7));
    f.ui.tick(state);await f.click();assert.equal(f.calls.filter(c=>c.name==='publish').length,1);
    f.finish({generation:1n});await f.until(()=>f.calls.some(c=>c.args?.[0]===7));
    const digest=Buffer.from(await webcrypto.subtle.digest('SHA-256',new Uint8Array([0xaa,0xbb]))).toString('hex');
    assert.equal(f.calls.find(c=>c.args?.[0]===7).args[1],digest);
    assert.match(f.elements['salvage-save-status'].textContent,/Delivery saved/);
    f.ui.tick(state);assert.equal(f.calls.filter(c=>c.name==='publish').length,1);f.ui.cleanup();
});
test('failed delivery is not acknowledged or retried every frame; explicit retry can commit',async()=>{
    const f=saveFixture(),state={world,ready:true,pause:{phase:'paused'},job:{savePending:true,secured:true}};
    f.ui.tick(state);await f.until(()=>f.calls.some(c=>c.name==='publish'));f.fail(Error('storage full'));
    await f.until(()=>f.elements['salvage-save-status'].textContent.startsWith('Save failed.'));
    for(let i=0;i<8;++i)f.ui.tick(state);
    assert.equal(f.calls.filter(c=>c.name==='publish').length,1);assert(!f.calls.some(c=>c.args?.[0]===7));
    f.store.publish=async generation=>({generation:generation+1n});await f.click();
    assert.equal(f.calls.filter(c=>c.args?.[0]===7).length,1);assert.match(f.elements['salvage-save-status'].textContent,/Delivery saved/);
    f.ui.cleanup();
});
test('closure after delivery publication starts never releases the live reward',async()=>{
    const f=saveFixture();f.ui.tick({world,ready:true,pause:{phase:'paused'},job:{savePending:true,secured:true}});
    await f.until(()=>f.calls.some(c=>c.name==='publish'));f.ui.cleanup();f.finish({generation:1n});
    await new Promise(r=>setTimeout(r,10));assert(!f.calls.some(c=>c.args?.[0]===7));
    assert(f.calls.some(c=>c.args?.[0]===5));assert(!f.calls.some(c=>c.name==='address'));
});
test('rejected delivery digest revokes admission instead of showing a reward',async()=>{
    const f=saveFixture(),original=f.engine.ccall.bind(f.engine);
    f.engine.ccall=(...args)=>{if(args[3]?.[0]===7)return '';return original(...args);};
    f.ui.tick({world,ready:true,pause:{phase:'paused'},job:{savePending:true,secured:true}});
    await f.until(()=>f.calls.some(c=>c.name==='publish'));f.finish({generation:1n});
    await f.until(()=>f.elements['salvage-save-status'].textContent.startsWith('Save failed.'));
    assert(f.calls.some(c=>c.args?.[0]===5));assert(!f.calls.some(c=>c.name==='address'));f.ui.cleanup();
});

test('harbor installation acknowledges its frozen archive without announcing a second payout',async()=>{
    const f=saveFixture(),state={world,ready:true,pause:{phase:'paused'},job:{savePending:true,secured:true},harbor:{pending:true}};
    f.ui.tick(state);await f.until(()=>f.calls.some(c=>c.name==='publish'));
    assert.match(f.elements['salvage-save-status'].textContent,/powered harbor lift/);
    assert(!f.calls.some(c=>c.args?.[0]===7));f.finish({generation:1n});
    await f.until(()=>f.calls.some(c=>c.args?.[0]===7));
    assert.match(f.elements['salvage-save-status'].textContent,/Harbor lift powered and saved/);
    assert(!f.elements['salvage-save-status'].textContent.includes('60'));f.ui.cleanup();
});

test('rescue saves unbanked cargo only after physical return joins, then acknowledges exact bytes',async()=>{
    const f=saveFixture(),state={world,ready:true,pause:{phase:'paused'},job:{secured:false,savePending:false},
        rescue:{pending:true,phase:'releasing',savePending:false}};
    f.ui.tick(state);assert(f.elements['salvage-save'].disabled);await f.click();
    assert(!f.calls.some(c=>c.name==='publish'));
    state.rescue={pending:true,phase:'saving',savePending:true};state.job.savePending=true;
    f.ui.tick(state);await f.until(()=>f.calls.some(c=>c.name==='publish'));
    assert.match(f.elements['salvage-save-status'].textContent,/recovered boat/);
    assert(!f.calls.some(c=>c.args?.[0]===7));f.finish({generation:1n});
    await f.until(()=>f.calls.some(c=>c.args?.[0]===7));
    assert.equal(f.calls.find(c=>c.args?.[0]===7).args[1],Buffer.from(await webcrypto.subtle.digest('SHA-256',new Uint8Array([0xaa,0xbb]))).toString('hex'));
    assert.match(f.elements['salvage-save-status'].textContent,/Boat recovered and saved/);
    assert(!f.elements['salvage-save-status'].textContent.includes('60'));f.ui.cleanup();
});

test('failed rescue stays pending without automatic retry and explicit retry publishes once',async()=>{
    const f=saveFixture(),state={world,ready:true,pause:{phase:'paused'},job:{secured:false,savePending:true},
        rescue:{pending:true,phase:'saving',savePending:true}};
    f.ui.tick(state);await f.until(()=>f.calls.some(c=>c.name==='publish'));f.fail(Error('storage full'));
    await f.until(()=>f.elements['salvage-save-status'].textContent.startsWith('Save failed.'));
    for(let i=0;i<10;i++)f.ui.tick(state);
    assert.equal(f.calls.filter(c=>c.name==='publish').length,1);assert(!f.calls.some(c=>c.args?.[0]===7));
    f.store.publish=async generation=>({generation:generation+1n});await f.click();
    assert.equal(f.calls.filter(c=>c.args?.[0]===7).length,1);f.ui.cleanup();
});

test('closing during rescue publication cannot release the retired game owner',async()=>{
    const f=saveFixture();f.ui.tick({world,ready:true,pause:{phase:'paused'},job:{secured:false,savePending:true},
        rescue:{pending:true,savePending:true}});
    await f.until(()=>f.calls.some(c=>c.name==='publish'));f.ui.cleanup();f.finish({generation:1n});
    await new Promise(r=>setTimeout(r,10));assert(!f.calls.some(c=>c.args?.[0]===7));
    assert(f.calls.some(c=>c.args?.[0]===5));assert(!f.calls.some(c=>c.name==='address'));
});

test('starter rebuild and stock withdrawal save without granting a delivery reward',async()=>{
    const f=saveFixture(),state={world,ready:true,pause:{phase:'running'},job:{secured:false},workshop:{savePending:true}};
    f.ui.tick(state);assert(!f.calls.some(c=>c.name==='publish'));
    state.pause.phase='paused';f.ui.tick(state);await f.until(()=>f.calls.some(c=>c.name==='publish'));
    assert.match(f.elements['salvage-save-status'].textContent,/boat and owned parts/);
    assert(!f.calls.some(c=>c.args?.[0]===7));f.finish({generation:1n});
    await f.until(()=>f.calls.some(c=>c.args?.[0]===7));
    assert.equal(f.calls.find(c=>c.args?.[0]===7).args[1],Buffer.from(await webcrypto.subtle.digest('SHA-256',new Uint8Array([0xaa,0xbb]))).toString('hex'));
    assert.match(f.elements['salvage-save-status'].textContent,/Boat and owned parts saved/);
    assert(!f.elements['salvage-save-status'].textContent.includes('60'));f.ui.cleanup();
});

test('Continue remembers only a completed resume with accepted lineage acknowledgment',async()=>{
    const f=fixture(),previous='11111111111111111111111111111111';f.metadata.set(shortcutKey,previous);
    const pending=api.resume(f.engine,world,[],f.environment);
    await f.until(()=>f.calls.some(c=>c.name==='publish'));
    assert.equal(f.metadata.get(shortcutKey),previous);f.finish({generation:9n});await pending;
    assert.equal(f.metadata.get(shortcutKey),world);
    assert(f.calls.findIndex(c=>c.name==='remember')>f.calls.findIndex(c=>c.args?.[0]===4));
});
test('failed or closed resume leaves the previous Continue pointer unchanged',async()=>{
    for(const refusal of ['publish','ack','close']){
        const f=fixture(),previous='11111111111111111111111111111111';f.metadata.set(shortcutKey,previous);
        if(refusal==='ack'){
            const original=f.engine.ccall.bind(f.engine);
            f.engine.ccall=(...args)=>args[3]?.[0]===4?'':original(...args);
        }
        const pending=api.resume(f.engine,world,[],f.environment);
        await f.until(()=>f.calls.some(c=>c.name==='publish'));
        if(refusal==='publish')f.fail(Error('disk full'));
        else {if(refusal==='close')f.events.get('pagehide')();f.finish({generation:9n});}
        await assert.rejects(pending);
        assert.equal(f.metadata.get(shortcutKey),previous);assert(!f.calls.some(c=>c.name==='remember'));
    }
});
test('manual and delivery pointers wait for successful publication and required acknowledgment',async()=>{
    for(const delivery of [false,true]){
        const f=saveFixture(),previous='11111111111111111111111111111111';f.metadata.set(shortcutKey,previous);
        f.ui.tick({world,ready:true,pause:{phase:'paused'},...(delivery?{job:{secured:true,savePending:true}}:{})});
        const pending=delivery?null:f.click();await f.until(()=>f.calls.some(c=>c.name==='publish'));
        assert.equal(f.metadata.get(shortcutKey),previous);f.finish({generation:1n});
        if(pending)await pending;else await f.until(()=>f.calls.some(c=>c.name==='remember'));
        assert.equal(f.metadata.get(shortcutKey),world);
        if(delivery)assert(f.calls.findIndex(c=>c.name==='remember')>f.calls.findIndex(c=>c.args?.[0]===7));
        f.ui.cleanup();
    }
});
test('failed, unacknowledged and closed save attempts never replace a confirmed shortcut',async()=>{
    for(const refusal of ['publish','ack','close']){
        const f=saveFixture(),previous='11111111111111111111111111111111';f.metadata.set(shortcutKey,previous);
        if(refusal==='ack'){
            const original=f.engine.ccall.bind(f.engine);
            f.engine.ccall=(...args)=>args[3]?.[0]===7?'':original(...args);
        }
        f.ui.tick({world,ready:true,pause:{phase:'paused'},job:{secured:true,savePending:true}});
        await f.until(()=>f.calls.some(c=>c.name==='publish'));
        if(refusal==='publish')f.fail(Error('quota'));
        else {if(refusal==='close')f.ui.cleanup();f.finish({generation:1n});}
        if(refusal!=='close')await f.until(()=>f.elements['salvage-save-status'].textContent.startsWith('Save failed.'));
        else await new Promise(r=>setTimeout(r,10));
        assert.equal(f.metadata.get(shortcutKey),previous);assert(!f.calls.some(c=>c.name==='remember'));f.ui.cleanup();
    }
});
function shortcutFixture(value,href='https://example.test/game/index.html?experience=lego&world=bad&redirect=https://other.test/#fragment'){
    const link={hidden:false,href:'https://untrusted.test/',removeAttribute(name){assert.equal(name,'href');delete this.href;}};
    const environment={document:{getElementById:id=>id==='cove-continue'?link:null},location:{href},
        localStorage:{getItem(key){assert.equal(key,shortcutKey);return value;}}};
    return {environment,link};
}
test('Continue is a same-origin current-path link containing only validated world and Cove route',()=>{
    const f=shortcutFixture(world);api.installContinue(f.environment);assert.equal(f.link.hidden,false);
    const url=new URL(f.link.href);assert.equal(url.origin,'https://example.test');assert.equal(url.pathname,'/game/index.html');
    assert.equal(url.search,`?experience=salvage-cove&world=${world}`);assert.equal(url.hash,'');
});
test('missing, corrupt and oversized Continue metadata hides the shortcut without touching saves',()=>{
    for(const value of [null,undefined,'',world.toUpperCase(),'0'.repeat(32),'a'.repeat(33),'x'.repeat(100000),
        '../other',`https://other.test/?world=${world}`,{world},` ${world}`]){
        const f=shortcutFixture(value);api.installContinue(f.environment);assert.equal(f.link.hidden,true);assert.equal(f.link.href,undefined);
    }
    for(const href of ['file:///tmp/index.html','javascript:alert(1)','not a URL']){
        const f=shortcutFixture(world,href);api.installContinue(f.environment);assert.equal(f.link.hidden,true);assert.equal(f.link.href,undefined);
    }
});
test('unavailable localStorage cannot fail save/resume or expose a broken Continue link',async()=>{
    for(const failure of ['getter','getItem','setItem']){
        const blocked=environment=>{
            if(failure==='getter')Object.defineProperty(environment,'localStorage',{get(){throw Error('permission');}});
            else environment.localStorage={getItem(){if(failure==='getItem')throw Error('permission');return null;},setItem(){throw Error('quota');}};
        };
        const link=shortcutFixture(world);blocked(link.environment);assert.doesNotThrow(()=>api.installContinue(link.environment));assert(link.link.hidden);
        const restored=fixture();blocked(restored.environment);const resume=api.resume(restored.engine,world,[],restored.environment);
        await restored.until(()=>restored.calls.some(c=>c.name==='publish'));restored.finish({generation:9n});await resume;assert(!restored.store.closed);
        const saved=saveFixture();blocked(saved.environment);saved.ready();const save=saved.click();
        await saved.until(()=>saved.calls.some(c=>c.name==='publish'));saved.finish({generation:1n});await save;
        assert.match(saved.elements['salvage-save-status'].textContent,/^Saved\./);assert(!saved.store.closed);saved.ui.cleanup();
    }
});
test('a valid but missing shortcut still refuses restoration and cannot create a fresh world',async()=>{
    const f=fixture();f.metadata.set(shortcutKey,world);f.store.load=async()=>({generation:0n,payload:new Uint8Array()});
    await assert.rejects(api.resume(f.engine,world,[],f.environment),/missing/);
    assert.equal(f.metadata.get(shortcutKey),world);assert(!f.calls.some(c=>c.name==='main'||c.name==='publish'||c.name==='remember'));
});
test('Continue uses a semantic hidden anchor, appears on LEGO routes only and survives narrow navigation CSS',async()=>{
    const html=await readFile(new URL('../web/index.html',import.meta.url),'utf8');
    assert.match(html,/<a id="cove-continue" hidden>Continue saved Cove<\/a>/);
    assert.match(html,/if \(legoShore \|\| legoWorld\) \{\s*VoxyCoveSaves\.installContinue\(\);/);
    assert.match(html,/#lego-shore-controls a:not\(#cove-continue\) \{ display:none; \}/);
    assert.match(html,/#lego-shore-controls #cove-continue:not\(\[hidden\]\).*min-height:44px/);
});
