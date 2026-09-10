// Real cutter/workshop controls and IndexedDB reload; no injected game state.
import assert from 'node:assert/strict';
import {validateCoveDelivery} from './validate_cove_delivery.mjs';
export async function validateCoveCut(call,directory,resumeOnly=false){
    assert(resumeOnly,'Cut acceptance requires an isolated real paid-build save');
    return validateCoveDelivery(call,directory,async({evaluate,read,wait,click,hold,walkTo,record,delivered})=>{
        const paid=delivered.boat.paidPartIds;assert.equal(paid.length,2);
        const owned=(state,fitted=true)=>{
            assert.deepEqual(state.session.inventory,delivered.session.inventory);
            assert.deepEqual([...state.boat.paidPartIds].sort(),fitted?[...paid].sort():[]);
            assert.deepEqual([...state.workshop.storedPartIds].sort(),fitted?[]:[...paid].sort());
            assert.equal(state.job.phase,delivered.job.phase);assert.equal(state.job.secured,delivered.job.secured);
            if(fitted){assert.equal(state.boat.parts,delivered.boat.parts);assert.equal(state.boat.massKg,delivered.boat.massKg);}
        };
        const resume=async()=>{await click('salvage-pause');await wait(s=>s.pause.phase==='running','resume');};
        const durable=async(name,previousTick)=>{
            await wait(s=>s.pause.phase==='paused'&&!s.workshop.savePending&&!s.rescue.pending&&BigInt(s.pause.tick)>BigInt(previousTick),name);
            const state=await record(name);assert.equal(state.boat.joinedTick,state.pause.tick);
            for(const root of state.boat.roots)assert.equal(root.observedTick,state.pause.tick);
            return state;
        };
        const reload=async name=>{
            await call('Page.reload',{ignoreCache:true});await new Promise(r=>setTimeout(r,200));
            await wait(s=>s.ready&&s.restore?.phase==='ready'&&s.pause.phase==='paused',name,true);
            return record(name);
        };
        if((await read()).pause.phase==='paused')await resume();
        let tick=(await read()).boat.physicsTicks.completed;await click('salvage-reset');await durable('starting-rescue',tick);
        await resume();await walkTo([4.5,-49]);await walkTo([4.5,-53]);
        await wait(s=>s.player.interaction==='board','boat alongside');await click('salvage-interact');
        await wait(s=>s.player.onBoat,'aboard');await walkTo(s=>[s.boat.helmPosition[0],s.boat.helmPosition[2]]);
        await wait(s=>s.player.interaction==='helm','helm reachable');await click('salvage-interact');await wait(s=>s.player.mode==='helm','helm');
        const home=(await read()).boat.position;await hold(['W'],240);
        const sailing=await record('sailed-away-before-cut');assert(Math.hypot(...sailing.boat.position.map((n,i)=>n-home[i]))>5);
        let cut,count=0;
        for(let attempt=0;attempt<6;++attempt){
            await wait(s=>s.cutter.canCut,'nearby weld available');const selected=await record('selected-weld-'+attempt);assert.notEqual(selected.cutter.weld,'0');
            tick=selected.boat.physicsTicks.completed;await click('salvage-cut');cut=await durable('cut-saved-'+attempt,tick);owned(cut);
            assert.equal(cut.cutter.cutWelds,++count);assert.equal(cut.workshop.recoveryDesigns,1);assert(!cut.workshop.canRemoveRecovery);
            if(cut.boat.rootCount>1)break;await resume();
        }
        assert(cut.boat.rootCount>1);const keys=cut.boat.roots.map(r=>r.key),designs=cut.workshop.recoveryDigests,rider=cut.player.rootKey;
        let loaded=await reload('cut-page-reload');owned(loaded);assert.deepEqual(loaded.boat.roots.map(r=>r.key),keys);
        assert.equal(loaded.cutter.cutWelds,count);assert.equal(loaded.player.rootKey,rider);assert.deepEqual(loaded.workshop.recoveryDigests,designs);
        for(let i=0;i<cut.boat.roots.length;++i)assert(Math.hypot(...loaded.boat.roots[i].position.map((n,j)=>n-cut.boat.roots[i].position[j]))<.25);
        await resume();tick=(await read()).boat.physicsTicks.completed;await click('salvage-reset');const rescued=await durable('all-cut-sections-rescued',tick);owned(rescued);
        assert.deepEqual(rescued.boat.roots.map(r=>r.key),keys);assert.equal(rescued.cutter.cutWelds,count);assert(!rescued.player.onBoat&&!rescued.tow.attached);
        loaded=await reload('cut-rescue-page-reload');owned(loaded);assert.deepEqual(loaded.workshop.recoveryDigests,designs);
        await resume();await click('salvage-workshop-toggle');await wait(s=>s.workshop.open&&s.workshop.canRebuild,'broken workshop opens');
        tick=(await read()).boat.physicsTicks.completed;await click('workshop-rebuild');let rebuilt=await durable('broken-starter-rebuilt',tick);owned(rebuilt,false);
        assert.equal(rebuilt.boat.rootCount,1);assert.equal(rebuilt.cutter.cutWelds,0);assert.equal(rebuilt.boat.parts,11);assert.deepEqual(rebuilt.workshop.recoveryDigests,designs);
        await resume();await click('salvage-workshop-toggle');await wait(s=>s.workshop.open&&s.workshop.canLoadRecovery,'protected design offered');
        await click('workshop-recovery-load');await wait(s=>s.workshop.canLaunch,'protected design loaded');assert.equal((await read()).workshop.charge,'0');
        tick=(await read()).boat.physicsTicks.completed;await click('workshop-launch');const refit=await durable('paid-design-restored-from-stock',tick);owned(refit);
        assert.equal(refit.boat.rootCount,1);assert.equal(refit.cutter.cutWelds,0);
        await resume();await click('salvage-workshop-toggle');await wait(s=>s.workshop.open&&s.workshop.canRebuild,'repeat rebuild offered');
        tick=(await read()).boat.physicsTicks.completed;await click('workshop-rebuild');rebuilt=await durable('second-rebuild-no-paid-duplication',tick);owned(rebuilt,false);
        loaded=await reload('repeated-rebuild-page-reload');owned(loaded,false);assert.deepEqual(loaded.workshop.recoveryDigests,designs);
        await click('salvage-leave');const deadline=Date.now()+20000;let departed=false;
        do{try{departed=await evaluate('new URLSearchParams(location.search).get("experience")==="lego-world"');}catch{}
            if(!departed)await new Promise(r=>setTimeout(r,50));}while(!departed&&Date.now()<deadline);
        assert(departed,'Leave drains and returns to the LEGO world');
    },true);
}
