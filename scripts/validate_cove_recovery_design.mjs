// Actual buttons, IndexedDB saves and reloads. No game setters or images.
import assert from 'node:assert/strict';
import {validateCoveDelivery} from './validate_cove_delivery.mjs';
export async function validateCoveRecoveryDesign(call,directory,resumeOnly=false){
    return validateCoveDelivery(call,directory,async({read,wait,click,record,delivered})=>{
        const paid=delivered.boat.paidPartIds;
        assert.equal(paid.length,1,'source has one actual purchased beam');
        const check=(s,active,stored)=>{
            assert.deepEqual(s.session.inventory,delivered.session.inventory);
            assert.equal(s.job.phase,delivered.job.phase);assert.equal(s.job.secured,delivered.job.secured);
            assert.equal(s.harbor.installed,delivered.harbor.installed);
            assert(Math.hypot(...s.tow.position.map((n,i)=>n-delivered.tow.position[i]))<.001);
            assert.deepEqual(s.boat.paidPartIds,active);assert.deepEqual(s.workshop.storedPartIds,stored);
        };
        const resume=async()=>{if((await read()).pause.phase==='paused')await click('salvage-pause');await wait(s=>s.pause.phase==='running','resumed');};
        const workshop=async()=>{await resume();await click('salvage-workshop-toggle');await wait(s=>s.workshop.open,'workshop open');};
        const durable=async name=>{await wait(s=>s.pause.phase==='paused'&&!s.workshop.savePending&&!s.workshop.pending,name);return record(name);};
        const reload=async name=>{
            const saved=await read();await call('Page.reload',{ignoreCache:true});await new Promise(r=>setTimeout(r,200));
            await wait(s=>s.ready&&s.restore?.phase==='ready'&&s.pause.phase==='paused','saved recovery reload',true);
            const loaded=await record(name);assert.equal(BigInt(loaded.pause.tick),BigInt(saved.pause.tick)+1n);return loaded;
        };
        await resume();await click('salvage-reset');
        await wait(s=>s.rescue.phase==='idle'&&BigInt(s.rescue.completed)>0n&&s.pause.phase==='paused','return to workshop');
        await workshop();await wait(s=>s.workshop.canRebuild,'starter service available');
        await click('workshop-rebuild');const rebuilt=await durable('custom-design-protected-before-rebuild');
        check(rebuilt,[],paid);assert.equal(rebuilt.boat.parts,11);assert.equal(rebuilt.workshop.recoveryDesigns,1);
        const digests=rebuilt.workshop.recoveryDigests;assert.equal(digests.length,1);
        let loaded=await reload('protected-design-page-reload');check(loaded,[],paid);assert.deepEqual(loaded.workshop.recoveryDigests,digests);
        await workshop();await wait(s=>s.workshop.canRebuild,'repeat starter service available');await click('workshop-rebuild');
        const repeated=await durable('original-starter-rebuild-keeps-custom-backup');check(repeated,[],paid);
        assert.deepEqual(repeated.workshop.recoveryDigests,digests);
        await workshop();await wait(s=>s.workshop.canLoadRecovery,'recovery design available');await click('workshop-recovery-load');
        loaded=await wait(s=>s.workshop.canLaunch,'custom layout loaded');
        assert.equal(loaded.workshop.parts,delivered.boat.parts);assert.equal(loaded.workshop.massKg,delivered.boat.massKg);
        assert.equal(loaded.workshop.charge,'0');assert.equal(loaded.workshop.refund,'0');assert.equal(loaded.workshop.canRemoveRecovery,false);
        await record('recovered-layout-ready-with-owned-stock');await click('workshop-launch');
        const restored=await durable('recovered-custom-boat-launched-and-saved');check(restored,paid,[]);
        assert.equal(restored.boat.parts,delivered.boat.parts);assert.equal(restored.boat.massKg,delivered.boat.massKg);
        loaded=await reload('recovered-boat-page-reload');check(loaded,paid,[]);assert.deepEqual(loaded.workshop.recoveryDigests,digests);
        await workshop();await wait(s=>s.workshop.canRebuild,'recovered custom boat service');await click('workshop-rebuild');
        const known=await durable('restored-custom-design-deduplicated');check(known,[],paid);
        assert.deepEqual(known.workshop.recoveryDigests,digests,'restored layout, welds, paint and settings remain identical');
        await workshop();await wait(s=>s.workshop.canRemoveRecovery,'selected recovery removal available');await click('workshop-recovery-remove');
        const removed=await durable('selected-recovery-design-removal-saved');check(removed,[],paid);assert.equal(removed.workshop.recoveryDesigns,0);
        loaded=await reload('recovery-removal-page-reload');check(loaded,[],paid);assert.equal(loaded.workshop.recoveryDesigns,0);assert.equal(loaded.workshop.canLoadRecovery,false);
        await click('salvage-leave');await wait(s=>!s.active,'recovery world drained');
    },resumeOnly);
}
