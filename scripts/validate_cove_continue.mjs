// One real Leave -> Continue return to an existing confirmed Cove save.
// Reads only game observations/export and local navigation metadata; no save setters.
import assert from 'node:assert/strict';
import {mkdir,readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';

export async function validateCoveContinue(call,directory,{baselineReportPath,expectedPresentationParts=9}={}) {
    assert(typeof baselineReportPath==='string');
    assert(Number.isInteger(expectedPresentationParts)&&expectedPresentationParts>0&&expectedPresentationParts<=16);
    const baselineBytes=await readFile(baselineReportPath),baseline=JSON.parse(baselineBytes);
    assert.equal(baseline.status,'passed');assert.equal(baseline.cove_mechanisms?.status,'passed');
    const old=baseline.cove_mechanisms,saved=old.stages.find(s=>s.name==='saved-mechanisms')?.state;
    assert(saved&&saved.pause.phase==='paused'&&!saved.workshop.open);
    const blueprint=old.blueprint;assert(typeof blueprint==='string'&&/^53564250[0-9a-f]+$/.test(blueprint));
    const identity=s=>({world:s.world,build:s.boat.buildId,topology:s.boat.topologyRevision,roots:s.boat.roots.map(r=>r.key)});
    const ownership=s=>({parts:s.boat.parts,mass:s.boat.massKg,paid:s.boat.paidPartIds,inventory:s.session.inventory,
        revision:s.session.revision,builds:s.session.builds,buildParts:s.session.buildParts,connections:s.session.buildConnections,
        cargo:s.session.cargo,jobs:s.session.jobs,stock:s.workshop.storedPartIds,recovery:s.workshop.recoveryDigests});
    const expectedIdentity=identity(saved),expectedOwnership=ownership(saved);
    const runtime=s=>({...identity(s),incarnation:s.boat.physicsTicks.incarnation,body:s.boat.mechanisms.bodyIndex,generation:s.boat.mechanisms.bodyGeneration});
    const began=Date.now(),deadline=began+120000;
    await mkdir(directory,{recursive:true});
    const report={status:'running',maximumSeconds:120,kind:'Actual Leave then same-origin Continue saved Cove; no refit or additional manual save action',
        baseline:{path:baselineReportPath,sha256:createHash('sha256').update(baselineBytes).digest('hex'),
            url:baseline.url,profile:baseline.retained_profile,identity:expectedIdentity,ownership:expectedOwnership,
            blueprintSha256:createHash('sha256').update(Buffer.from(blueprint,'hex')).digest('hex')},stages:[]};
    const persist=()=>writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));
    const evaluate=async expression=>{
        const result=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        if(result.exceptionDetails)throw Error(JSON.stringify(result.exceptionDetails));return result.result.value;
    };
    const read=()=>evaluate("typeof voxyModule==='object'&&voxyModule?._voxy_is_initialized?.()===1?JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json())):null");
    const until=async(reader,predicate,label)=>{
        const end=Math.min(deadline,Date.now()+40000);let value,lastError;
        while(Date.now()<end){
            try{value=await reader();if(predicate(value))return value;lastError=null;}catch(error){lastError=String(error);}
            await new Promise(resolve=>setTimeout(resolve,30));
        }
        throw Error(`${label}: ${JSON.stringify(value)}${lastError?' / '+lastError:''}`);
    };
    const key=async(key,code,number)=>{
        await evaluate('voxyModule.canvas.focus({preventScroll:true})');
        try{await call('Input.dispatchKeyEvent',{type:'keyDown',key,code,windowsVirtualKeyCode:number,nativeVirtualKeyCode:number});}
        finally{await call('Input.dispatchKeyEvent',{type:'keyUp',key,code,windowsVirtualKeyCode:number,nativeVirtualKeyCode:number});}
    };
    const uncap=async()=>{
        if(!await evaluate('Boolean(voxyModule._voxy_get_uncapped_fps())'))await key('F9','F9',120);
        await until(()=>evaluate('Boolean(voxyModule._voxy_get_uncapped_fps())'),x=>x,'physical F9 uncapped');
    };
    const element=async selector=>evaluate(`(()=>{const e=document.querySelector(${JSON.stringify(selector)});if(!e)return null;
        const r=e.getBoundingClientRect(),c=getComputedStyle(e),x=r.x+r.width/2,y=r.y+r.height/2;
        return {text:e.textContent.trim(),href:e.href||null,disabled:Boolean(e.disabled),hidden:e.hidden,
            visible:r.width>0&&r.height>0&&c.visibility!=='hidden'&&c.display!=='none'&&x>=0&&x<innerWidth&&y>=0&&y<innerHeight,
            hit:e.contains(document.elementFromPoint(x,y)),x,y,width:r.width,height:r.height,tag:e.tagName,tabIndex:e.tabIndex};})()`);
    const click=async selector=>{
        await evaluate(`document.querySelector(${JSON.stringify(selector)})?.scrollIntoView({block:'center'})`);
        const target=await until(()=>element(selector),e=>e&&e.visible&&e.hit&&!e.hidden&&!e.disabled,`actual ${selector} hit target`);
        try{await call('Input.dispatchMouseEvent',{type:'mousePressed',x:target.x,y:target.y,button:'left',clickCount:1});}
        finally{await call('Input.dispatchMouseEvent',{type:'mouseReleased',x:target.x,y:target.y,button:'left',clickCount:1});}
        return target;
    };
    let pageOwner;
    const invariant=s=>{
        assert(s?.active&&s.ready&&!s.failed&&s.boat.active&&s.session.admissionOpen);
        assert.equal(s.restore.phase,'ready');assert.equal(s.terrainSurface,'lego');assert.equal(s.boat.physicsTicks.failed,false);
        assert.deepEqual(identity(s),expectedIdentity);assert.deepEqual(ownership(s),expectedOwnership);
        assert.equal(s.job.phase,saved.job.phase);assert.equal(s.job.secured,saved.job.secured);assert.equal(s.job.durable,saved.job.durable);
        assert.equal(s.harbor.installed,saved.harbor.installed);assert.equal(s.assetFixture.presentationParts,expectedPresentationParts);
        assert.equal(s.assetFixture.gpuReservationBytes,'9731960');assert(s.assetFixture.environmentReady&&s.assetFixture.draws>0);
        assert(s.boat.mechanisms.bodyIndex>0&&s.boat.mechanisms.bodyGeneration>0);
        assert(!s.workshop.pending&&!s.workshop.savePending&&!s.job.pending&&!s.job.savePending&&!s.rescue.pending&&!s.harbor.pending);
        if(pageOwner)assert.deepEqual(runtime(s),pageOwner);
    };
    const capture=async(name,{paused=true,workshop=false}={})=>{
        const state=await until(read,s=>s?.restore?.phase==='ready'&&s.ready&&s.pause.phase===(paused?'paused':'running')
            &&s.workshop.open===workshop&&s.assetFixture.environmentReady,name);
        invariant(state);const owner=runtime(state);
        const next=await until(read,s=>s&&s.workshop.open===workshop&&s.pause.phase===(paused?'paused':'running')
            &&BigInt(s.assetFixture.submittedSerial)>BigInt(state.assetFixture.submittedSerial),name+' later submission');
        invariant(next);assert.deepEqual(runtime(next),owner);
        const done=await until(read,s=>s&&BigInt(s.assetFixture.completedSerial)>=BigInt(next.assetFixture.submittedSerial)
            &&BigInt(s.boat.physicsTicks.completed)>=BigInt(next.boat.mechanisms.tick),name+' actual GPU and physics completion');
        invariant(done);assert.deepEqual(runtime(done),owner);
        assert.equal(done.workshop.open,workshop);assert.equal(done.pause.phase,paused?'paused':'running');
        assert.deepEqual(await evaluate('globalThis.voxyUncapturedGpuErrors||[]'),[]);
        report.stages.push({name,state,submission:{owner,fixture:next.assetFixture.submittedSerial,physics:next.boat.mechanisms.tick},
            completion:{owner:runtime(done),fixture:done.assetFixture.completedSerial,physics:done.boat.physicsTicks.completed}});
        await persist();return done;
    };
    try{
        const originalUrl=await evaluate('location.href'),original=new URL(originalUrl);
        assert.equal(original.origin,new URL(baseline.url).origin);assert.equal(original.searchParams.get('world'),saved.world);
        await until(read,s=>s?.restore?.phase==='ready'&&s.ready&&s.pause.phase==='paused','initial existing save ready');
        await uncap();pageOwner=runtime(await read());
        const first=await capture('confirmed-old-save-before-leave');
        assert.equal(await evaluate("localStorage.getItem('voxys.cove.last-confirmed-world.v1')"),saved.world,'confirmed navigation pointer matches the resumed world');
        report.leaveControl=await click('#salvage-leave');
        // Existing Leave navigates only after its frame-boundary active=false acknowledgment.
        // The subsequent document also releases the previous page's exclusive storage owner.
        await until(()=>evaluate('location.href'),href=>new URL(href).searchParams.get('experience')==='lego-world'
            &&!new URL(href).searchParams.has('world'),'real drained Leave reaches LEGO page');
        const link=await until(()=>element('#cove-continue'),e=>e?.visible&&!e.hidden&&e.hit,'Continue is visibly reachable');
        assert.equal(link.tag,'A');assert.equal(link.text,'Continue saved Cove');assert.equal(link.tabIndex,0);assert(link.height>=44);
        const destination=new URL(link.href);assert.equal(destination.origin,original.origin);assert.equal(destination.pathname,original.pathname);
        assert.equal(destination.search,`?experience=salvage-cove&world=${saved.world}`);assert.equal(destination.hash,'');
        report.stages.push({name:'left-cove-and-found-confirmed-continue',url:await evaluate('location.href'),link});await persist();
        report.continueControl=await click('#cove-continue');pageOwner=null;
        await until(()=>evaluate('location.href'),href=>href===destination.href,'actual Continue follows the canonical same-origin link');
        await until(read,s=>s?.restore?.phase==='ready'&&s.ready&&s.pause.phase==='paused'&&s.world===saved.world,'same saved Cove resumes paused');
        await uncap();pageOwner=runtime(await read());const returned=await capture('continue-restores-the-same-paused-owned-world');
        assert.notEqual(returned.observation.incarnation,first.observation.incarnation,'new page restored a fresh observation owner');
        await key('p','KeyP',80);await until(read,s=>s?.pause.phase==='running'&&s.workshop.canOpen,'resume for read-only blueprint comparison');
        await key('b','KeyB',66);await until(read,s=>s?.workshop.open&&!s.workshop.changed&&!s.workshop.camera.framePending,'open unchanged owned design');
        const actual=await evaluate("voxyModule.ccall('voxy_salvage_blueprint_action','string',['number','string'],[1,''])");
        assert.equal(actual,blueprint,'exact saved part identities, placements, paint, connections and module settings');
        report.blueprintSha256=createHash('sha256').update(Buffer.from(actual,'hex')).digest('hex');
        await capture('continued-design-settings-and-stock-exact',{paused:false,workshop:true});
        await key('b','KeyB',66);await until(read,s=>s&&!s.workshop.open,'close without edits');
        await key('p','KeyP',80);await capture('continued-cove-paused-without-manual-save');report.status='passed';
    }catch(error){report.status='failed';report.error=String(error);try{report.failureState=await read();}catch{}throw error;}
    finally{report.elapsedSeconds=(Date.now()-began)/1000;await persist();}
    return report;
}
