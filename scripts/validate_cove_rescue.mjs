// Actual Rescue/Resume/boarding controls and real IndexedDB reload. The saved
// source is an isolated prior gameplay profile; no injected state or images.
import assert from 'node:assert/strict';
import {writeFile} from 'node:fs/promises';
import {validateCoveDelivery} from './validate_cove_delivery.mjs';
export async function validateCoveRescue(call,directory,resumeOnly=false,checkpointOnly=false){
    assert(!checkpointOnly||resumeOnly,'A checkpoint-only check needs a real saved source');
    const report=await validateCoveDelivery(call,directory,async({evaluate,read,wait,click,hold,walkTo,record,delivered})=>{
        const leave=async()=>{
            await click('salvage-leave');const deadline=Date.now()+20000;let departed=false;
            // The UI navigates only after observing active:false. Once it does,
            // the old module no longer exists; do not poll that destroyed owner.
            do{
                try{departed=await evaluate('new URLSearchParams(location.search).get("experience")==="lego-world"');}
                catch{ /* A context replacement during normal navigation. */ }
                if(!departed)await new Promise(r=>setTimeout(r,50));
            }while(!departed&&Date.now()<deadline);
            assert(departed,'Leave must drain and navigate back to the LEGO world');
        };
        if(checkpointOnly){
            assert(delivered.tow.attached&&delivered.tow.confirmed&&!delivered.job.secured,'This focused check needs live unbanked cargo');
            await record('attached-generator-rescue-checkpoint-scope');
        }
        const unchanged=s=>{
            assert.deepEqual(s.session.inventory,delivered.session.inventory);
            assert.deepEqual(s.boat.paidPartIds,delivered.boat.paidPartIds);
            assert.equal(s.boat.parts,delivered.boat.parts);assert.equal(s.boat.massKg,delivered.boat.massKg);
            assert.deepEqual(s.boat.roots.map(r=>r.key),delivered.boat.roots.map(r=>r.key));
            assert.equal(s.boat.controlPart,delivered.boat.controlPart);
            assert.equal(s.job.phase,delivered.job.phase);assert.equal(s.job.secured,delivered.job.secured);
            assert.equal(s.harbor.installed,delivered.harbor.installed);
            if(s.job.secured)assert(Math.hypot(...s.tow.position.map((n,i)=>n-delivered.tow.position[i]))<.001);
        };
        for(let cycle=0;cycle<2;cycle++){
            if((await read()).pause.phase==='paused')await click('salvage-pause');
            await wait(s=>s.pause.phase==='running','resume before rescue');
            const completed=BigInt((await read()).rescue.completed);
            await click('salvage-reset');
            await wait(s=>s.rescue.phase==='idle'&&BigInt(s.rescue.completed)>completed&&s.pause.phase==='paused','rescue saved');
            const saved=await record('rescue-saved-'+cycle);unchanged(saved);
            assert(!saved.player.onBoat&&!saved.tow.attached&&!saved.harbor.attached);
            await call('Page.reload',{ignoreCache:true});
            await new Promise(r=>setTimeout(r,200));
            await wait(s=>s.ready&&s.restore?.phase==='ready'&&s.pause.phase==='paused','reload rescued boat',true);
            const loaded=await record('rescue-page-reload-'+cycle);unchanged(loaded);
            assert.equal(BigInt(loaded.pause.tick),BigInt(saved.pause.tick)+1n);
            assert.equal(saved.boat.joinedTick,saved.pause.tick);
            assert.equal(loaded.boat.joinedTick,loaded.pause.tick);
            for(let i=0;i<saved.boat.roots.length;++i)
                assert(Math.hypot(...loaded.boat.roots[i].position.map((n,j)=>n-saved.boat.roots[i].position[j]))<.1);
            assert(Math.hypot(...loaded.boat.position.map((n,i)=>n-saved.boat.position[i]))<.1);
            assert(Math.hypot(...loaded.tow.position.map((n,i)=>n-saved.tow.position[i]))<.1);
            assert(!loaded.tow.attached&&!loaded.harbor.attached);
        }
        if(checkpointOnly){
            await leave();
            return;
        }
        await click('salvage-pause');await walkTo([5.5,-51]);await walkTo([4.5,-53]);
        await wait(s=>s.player.interaction==='board','rescued boat alongside');await click('salvage-interact');
        await wait(s=>s.player.onBoat,'board rescued boat');
        await walkTo(s=>[s.boat.helmPosition[0],s.boat.helmPosition[2]]);
        await wait(s=>s.player.interaction==='helm','helm position');await click('salvage-interact');
        await wait(s=>s.player.mode==='helm','helm');await hold(['W'],90);
        const sailing=await record('sailing-after-repeated-rescue');unchanged(sailing);assert(sailing.boat.speed>1);
        await leave();
    },resumeOnly);
    if(checkpointOnly){
        report.kind='attached generator Rescue, repeated durable checkpoint/reload and drained Leave; excludes boarding/sailing';
        await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));
    }
    return report;
}
