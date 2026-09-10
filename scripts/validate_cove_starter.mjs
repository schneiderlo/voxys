// Real workshop controls, paid-stock reuse and IndexedDB reload. No images or game setters.
import assert from 'node:assert/strict';
import {validateCoveDelivery} from './validate_cove_delivery.mjs';
export async function validateCoveStarter(call,directory,resumeOnly=false){
    return validateCoveDelivery(call,directory,async({read,wait,click,record,delivered})=>{
        const paid=delivered.boat.paidPartIds;
        assert.equal(paid.length,1,'source has one actual purchased beam');
        const unchanged=s=>{
            assert.deepEqual(s.session.inventory,delivered.session.inventory);
            assert.equal(s.job.phase,delivered.job.phase);assert.equal(s.job.secured,delivered.job.secured);
            assert.equal(s.harbor.installed,delivered.harbor.installed);
            assert(Math.hypot(...s.tow.position.map((n,i)=>n-delivered.tow.position[i]))<.001);
        };
        const check=(s,active,stored)=>{
            unchanged(s);assert.deepEqual(s.boat.paidPartIds,active);assert.deepEqual(s.workshop.storedPartIds,stored);
        };
        const resume=async()=>{if((await read()).pause.phase==='paused')await click('salvage-pause');await wait(s=>s.pause.phase==='running','resumed');};
        const reload=async name=>{
            const saved=await read();await call('Page.reload',{ignoreCache:true});await new Promise(r=>setTimeout(r,200));
            await wait(s=>s.ready&&s.restore?.phase==='ready'&&s.pause.phase==='paused','real saved boat reload',true);
            const loaded=await record(name);assert.equal(BigInt(loaded.pause.tick),BigInt(saved.pause.tick)+1n);return loaded;
        };
        // Return through the already shipped durable rescue so an old saved rig
        // reaches the dock upright without changing any paid ownership.
        await resume();await click('salvage-reset');
        await wait(s=>s.rescue.phase==='idle'&&BigInt(s.rescue.completed)>0n&&s.pause.phase==='paused','return to workshop');
        await resume();await click('salvage-workshop-toggle');await wait(s=>s.workshop.open&&s.workshop.canRebuild,'rebuild available');
        await click('workshop-rebuild');await wait(s=>s.pause.phase==='paused'&&!s.workshop.savePending&&!s.workshop.pending,'starter rebuild saved');
        const rebuilt=await record('starter-rebuild-saved');check(rebuilt,[],paid);assert.equal(rebuilt.boat.parts,11);
        check(await reload('starter-rebuild-page-reload'),[],paid);
        await resume();await click('salvage-workshop-toggle');await wait(s=>s.workshop.open,'workshop');
        await click('workshop-catalog-next');await wait(s=>s.workshop.catalogName==='Beam'&&s.workshop.partCost==='0','owned beam offered for reuse');
        await click('workshop-add');await wait(s=>s.workshop.changed&&s.workshop.valid,'stored beam connected');
        await click('workshop-keep');await wait(s=>s.workshop.canLaunch&&s.workshop.charge==='0','free owned-part reuse');
        await click('workshop-launch');await wait(s=>s.pause.phase==='paused'&&!s.workshop.savePending&&!s.workshop.pending,'stock withdrawal saved');
        check(await record('stored-beam-reused-and-saved'),paid,[]);check(await reload('stock-withdrawal-page-reload'),paid,[]);
        await resume();await click('salvage-workshop-toggle');await wait(s=>s.workshop.open&&s.workshop.canRebuild,'repeat rebuild');
        await click('workshop-rebuild');await wait(s=>s.pause.phase==='paused'&&!s.workshop.savePending&&!s.workshop.pending,'second rebuild saved');
        check(await record('second-rebuild-saved'),[],paid);check(await reload('second-rebuild-page-reload'),[],paid);
        await click('salvage-leave');await wait(s=>!s.active,'starter world drained');
    },resumeOnly);
}
