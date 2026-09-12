// Continue an actual older saved Cove through presentation-only cargo changes.
// Four real-control stages; read-only observations/export, no refit/save/reset.
import assert from 'node:assert/strict';
import {mkdir,readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';

export async function validateCoveCargoCompatibility(call,directory,{expectedPresentationParts=9,baselineReportPath}={}) {
    assert(Number.isInteger(expectedPresentationParts)&&expectedPresentationParts>=1&&expectedPresentationParts<=16);
    assert.equal(typeof baselineReportPath,'string','an actual earlier combined browser report is required');
    const baselineBytes=await readFile(baselineReportPath),outer=JSON.parse(baselineBytes);
    assert.equal(outer.status,'passed');
    const baseline=outer.cove_mechanisms;
    assert.equal(baseline?.status,'passed','baseline must include the completed mechanism journey');
    const saved=baseline.stages.find(s=>s.name==='saved-mechanisms')?.state;
    const verified=baseline.stages.find(s=>s.name==='restored-exact-design-stock-and-settings')?.state;
    assert(saved&&verified,'baseline must contain actual saved and verified restored design states');
    assert.equal(saved.pause.phase,'paused');assert.equal(saved.workshop.open,false);
    assert.equal(verified.workshop.name,'Propeller');
    assert.deepEqual(verified.workshop.settings,{enabled:false,limitPercent:100,reversed:true});
    const expectedBlueprint=baseline.blueprint;
    assert(typeof expectedBlueprint==='string'&&/^53564250[0-9a-f]+$/.test(expectedBlueprint)&&expectedBlueprint.length%2===0,
        'actual exact saved blueprint bytes required');
    const savedIdentity={world:saved.world,build:saved.boat.buildId,topology:saved.boat.topologyRevision,
        roots:saved.boat.roots.map(r=>r.key)};
    const ownership=s=>({parts:s.boat.parts,massKg:s.boat.massKg,paidPartIds:s.boat.paidPartIds,
        inventory:s.session.inventory,revision:s.session.revision,builds:s.session.builds,
        buildParts:s.session.buildParts,buildConnections:s.session.buildConnections,cargo:s.session.cargo,jobs:s.session.jobs,
        storedPartIds:s.workshop.storedPartIds,recoveryDigests:s.workshop.recoveryDigests});
    const expectedOwnership=ownership(saved);assert.deepEqual(ownership(verified),expectedOwnership);
    await mkdir(directory,{recursive:true});
    const started=Date.now(),deadline=started+60000;
    const report={status:'running',kind:'Older saved boat under nine compatible cargo presentations; no images, refit or additional save actions',
        maximumSeconds:60,expectedPresentationParts,baseline:{path:baselineReportPath,
            sha256:createHash('sha256').update(baselineBytes).digest('hex'),url:outer.url,profile:outer.retained_profile,
            savedIdentity,ownership:expectedOwnership,settings:verified.workshop.settings,
            blueprintSha256:createHash('sha256').update(Buffer.from(expectedBlueprint,'hex')).digest('hex')},stages:[]};
    const evaluate=async expression=>{
        const result=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        if(result.exceptionDetails)throw Error(JSON.stringify(result.exceptionDetails));return result.result.value;
    };
    const read=()=>evaluate('JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json()))');
    const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
    const wait=async(predicate,label)=>{
        const end=Math.min(deadline,Date.now()+15000);let state;
        while(Date.now()<end){state=await read();if(await predicate(state))return state;await delay(30);}
        throw Error(`${label}: ${JSON.stringify(state)}`);
    };
    const press=async(key,code,keyCode)=>{
        await evaluate('voxyModule.canvas.focus({preventScroll:true})');
        try{await call('Input.dispatchKeyEvent',{type:'keyDown',key,code,windowsVirtualKeyCode:keyCode,nativeVirtualKeyCode:keyCode});}
        finally{await call('Input.dispatchKeyEvent',{type:'keyUp',key,code,windowsVirtualKeyCode:keyCode,nativeVirtualKeyCode:keyCode});}
    };
    const uncap=async()=>{
        const before=Boolean(await evaluate('voxyModule._voxy_get_uncapped_fps()'));
        if(!before)await press('F9','F9',120);
        await wait(()=>evaluate('Boolean(voxyModule._voxy_get_uncapped_fps())'),'existing F9 uncapped control');
        report.presentationControl={initiallyUncapped:before,physicalF9:!before,uncapped:true};
    };
    const logicalIdentity=s=>({world:s.world,build:s.boat.buildId,topology:s.boat.topologyRevision,
        roots:s.boat.roots.map(r=>r.key)});
    const runtimeIdentity=s=>({...logicalIdentity(s),incarnation:s.boat.physicsTicks.incarnation,
        bodyIndex:s.boat.mechanisms.bodyIndex,bodyGeneration:s.boat.mechanisms.bodyGeneration});
    let currentOwner;
    const invariant=s=>{
        assert(s.active&&s.ready&&!s.failed&&s.boat.active&&s.session.admissionOpen,'restored active admitted Cove');
        assert.equal(s.restore?.phase,'ready','must restore the existing save, not start a replacement world');
        assert.equal(s.boat.physicsTicks.failed,false);assert.equal(s.terrainSurface,'lego');
        assert.deepEqual(logicalIdentity(s),savedIdentity,'saved world/build/topology/root identity');
        assert.deepEqual(ownership(s),expectedOwnership,'exact owned boat, connections, stock and revision');
        assert.equal(s.job.phase,saved.job.phase);assert.equal(s.job.secured,saved.job.secured);
        assert.equal(s.job.durable,saved.job.durable);assert.equal(s.harbor.installed,saved.harbor.installed);
        const m=s.boat.mechanisms;assert(m.bodyIndex>0&&m.bodyGeneration>0,'current live body');
        assert.equal(m.incarnation,s.boat.physicsTicks.incarnation);
        assert.equal(m.animatedParts,s.workshop.open?0:2);
        // Runtime body/physics identities may differ from the old process;
        // they must remain fixed within this actual continuation.
        if(currentOwner)assert.deepEqual(runtimeIdentity(s),currentOwner);
        const f=s.assetFixture;assert.equal(f.status,2);assert.equal(f.presentationParts,expectedPresentationParts);
        const ownerBytes=BigInt(f.gpuReservationBytes);
        assert(ownerBytes>0n&&ownerBytes<=16n*1024n*1024n,'owned fixture stays within the existing16MiB cap');
        assert(f.environmentReady&&BigInt(f.environmentGpuBytes)>0n&&f.draws>0,'lit fixture and draw work available');
        assert.equal(s.player.onBoat,false,'continuation stays at the dock');
        assert.equal(s.workshop.pending,false);assert.equal(s.workshop.savePending,false);
        assert.equal(s.job.pending,false);assert.equal(s.job.savePending,false);
        assert.equal(s.rescue.pending,false);assert.equal(s.harbor.pending,false);
    };
    const capture=async(name,{blueprint,workshopOpen=false,pausePhase='running'}={})=>{
        // The flag can reset across render/discard/transition updates. It
        // is not tagged to a submitted serial. Match its requested mode,
        // then require a later same-mode submission and its completion.
        const state=await wait(s=>{
            invariant(s);
            return s.workshop.open===workshopOpen&&s.pause.phase===pausePhase
                &&s.assetFixture.sceneSunShadows===!workshopOpen;
        },'requested logical mode and rendered shadow sample');
        assert.equal(state.assetFixture.sceneSunShadows,!workshopOpen);
        const owner=runtimeIdentity(state);
        const sameMode=s=>{
            invariant(s);assert.deepEqual(runtimeIdentity(s),owner);
            assert.equal(s.workshop.open,workshopOpen);assert.equal(s.pause.phase,pausePhase);
        };
        const submitted=await wait(s=>{
            sameMode(s);
            return BigInt(s.assetFixture.submittedSerial)>BigInt(state.assetFixture.submittedSerial);
        },'later same-mode submission follows the matched shadow sample');
        const submittedSerial=submitted.assetFixture.submittedSerial,submittedTick=submitted.boat.mechanisms.tick;
        const completed=await wait(s=>{
            sameMode(s);
            return BigInt(s.assetFixture.completedSerial)>=BigInt(submittedSerial)
                &&BigInt(s.boat.physicsTicks.completed)>=BigInt(submittedTick);
        },'later current-owner submission and physics work completes');
        assert.deepEqual(await evaluate('globalThis.voxyUncapturedGpuErrors||[]'),[],'no uncaptured GPU errors');
        report.stages.push({name,state,...(blueprint?{blueprint}:{}),presentation:{workshopOpen,pausePhase,
            sceneSunShadows:state.assetFixture.sceneSunShadows,submittedSerial:state.assetFixture.submittedSerial},
            submission:{owner:runtimeIdentity(submitted),fixture:submittedSerial,physics:submittedTick,
                workshopOpen:submitted.workshop.open,pausePhase:submitted.pause.phase,
                sceneSunShadows:submitted.assetFixture.sceneSunShadows},
            completion:{owner:runtimeIdentity(completed),
            fixture:completed.assetFixture.completedSerial,physics:completed.boat.physicsTicks.completed}});
        await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));return state;
    };
    const fresh=async(before,predicate,label)=>wait(s=>BigInt(s.assetFixture.submittedSerial)>=BigInt(before)+3n&&predicate(s),label);
    try{
        assert.equal(await evaluate('location.origin'),new URL(outer.url).origin,'same retained storage origin');
        assert.equal(await evaluate('new URLSearchParams(location.search).get("world")'),saved.world,'explicit older saved world');
        await wait(s=>s.ready&&s.restore?.phase==='ready'&&s.pause.phase==='paused'&&!s.workshop.open
            &&s.boat?.mechanisms&&s.assetFixture.environmentReady,'actual older paused save');
        await uncap();currentOwner=runtimeIdentity(await read());
        const paused=await capture('older-save-restored-with-nine-presentations',{pausePhase:'paused'});
        assert.equal(paused.pause.phase,'paused');
        await press('p','KeyP',80);
        await fresh(paused.assetFixture.submittedSerial,s=>s.pause.phase==='running'&&s.workshop.canOpen,'physical P resumes at the workshop station');
        const resumed=await capture('resumed-with-exact-owned-boat');
        await press('b','KeyB',66);
        await fresh(resumed.assetFixture.submittedSerial,s=>s.workshop.open&&!s.workshop.changed
            &&!s.assetFixture.sceneSunShadows,'physical B opens the workshop');
        const blueprint=await evaluate("voxyModule.ccall('voxy_salvage_blueprint_action','string',['number','string'],[1,''])");
        assert.equal(blueprint,expectedBlueprint,'exact part/connection/paint/placement/settings blueprint bytes survive new presentations');
        const workshop=await capture('workshop-preserves-exact-saved-blueprint',{blueprint,workshopOpen:true});
        assert.equal(workshop.workshop.changed,false);assert.equal(workshop.workshop.brickTool,false);
        for(const key of ['charge','refund','machineryCharge','machineryRefund'])assert.equal(workshop.workshop[key],'0');
        await press('b','KeyB',66);
        await fresh(workshop.assetFixture.submittedSerial,s=>!s.workshop.open&&s.pause.phase==='running'
            &&s.assetFixture.sceneSunShadows,'physical B closes the workshop');
        await capture('closed-workshop-same-owned-design');report.status='passed';
    }catch(error){
        report.status='failed';report.error=String(error);try{report.failureState=await read();}catch{}throw error;
    }finally{
        report.elapsedSeconds=(Date.now()-started)/1000;await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));
    }
    return report;
}
