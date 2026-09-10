// Real first-job delivery supplies the upgrade; the harbor sequence uses only
// shipped keyboard/mouse controls and read-only state. No images or setters.
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {validateCoveDelivery} from './validate_cove_delivery.mjs';
export async function validateCoveHarbor(call,directory,resumeOnly=false){
    return validateCoveDelivery(call,directory,async({evaluate,read,wait,click,hold,walkTo,record,delivered})=>{
        let installed;
        if(!resumeOnly){
        const firstRescue=BigInt((await read()).rescue.completed);await click('salvage-reset');
        await wait(s=>s.ready&&BigInt(s.rescue.completed)>firstRescue&&!s.rescue.pending&&s.pause.phase==='paused','rescue saved at dock');
        await click('salvage-pause');
        await walkTo([4.5,-52.5]);await wait(s=>s.harbor.canInstall,'powered lift available');
        await click('salvage-harbor-install');
        await wait(s=>s.harbor.installed&&s.harbor.durable&&!s.harbor.pending&&s.pause.phase==='paused','harbor installation saved');
        installed=await record('harbor-installed-and-saved');assert.deepEqual(installed.session.inventory,delivered.session.inventory);
        assert(installed.tow.position[0]>6&&installed.tow.position[0]<8&&installed.tow.position[2]>-49&&installed.tow.position[1]>=0&&installed.tow.speed===0);
        assert(Math.hypot(...installed.tow.position.map((v,i)=>v-delivered.tow.position[i]))>5);
        await click('salvage-pause');
        // Delivery may have pushed the boat out of alignment before the
        // generator was moved onto the pier. Use the shipped Return action to
        // reposition it now that the berth is clear; preserve the paid build,
        // installed frame and banked cargo. This is an explicit player action.
        const resetCount=BigInt((await read()).rescue.completed);await click('salvage-reset');
        await wait(s=>s.ready&&BigInt(s.rescue.completed)>resetCount&&!s.rescue.pending&&s.pause.phase==='paused','cleared berth rescue saved');
        await click('salvage-pause');await walkTo([5.5,-51]);
        await walkTo([4.5,-51]);const positioned=await record('harbor-return-to-cleared-berth');
        assert.deepEqual(positioned.session.inventory,installed.session.inventory);
        assert(Math.hypot(...positioned.tow.position.map((v,i)=>v-installed.tow.position[i]))<.001);
        await click('salvage-harbor-attach');
        await record('harbor-attachment-requested');
        await wait(s=>s.harbor.attached&&s.harbor.canOperate,'safe four-line attachment');
        // Preserve a real, durably published pre-lift checkpoint. Retaining the
        // isolated browser profile allows focused fault reproduction without
        // replaying construction/delivery or injecting game/storage state.
        await click('salvage-pause');await wait(s=>s.pause.phase==='paused','pre-lift pause');
        await click('salvage-save');
        const saveEnd=Date.now()+10000;let checkpointReceipt='';
        do{checkpointReceipt=await evaluate('document.getElementById("salvage-save-status").textContent');
            if(checkpointReceipt.startsWith('Saved.'))break;await new Promise(r=>setTimeout(r,50));}while(Date.now()<saveEnd);
        assert.match(checkpointReceipt,/^Saved\./);await record('harbor-prelift-checkpoint');
        }else{
            installed=delivered;assert(delivered.harbor.durable&&delivered.harbor.attached);
            assert.equal(delivered.harbor.brokenMask,0);
        }
        // Export exact already-published current/mirror envelopes through a
        // read-only transaction. These bytes are portable to the native host;
        // no checkpoint is synthesized and no browser storage is written here.
        const envelopes=await evaluate(`new Promise((resolve,reject)=>{
            const request=indexedDB.open('voxys-expeditions-v1');
            request.onupgradeneeded=()=>{request.transaction.abort();reject(Error('Missing saved database'));};
            request.onerror=()=>reject(request.error);
            request.onsuccess=()=>{const db=request.result,tx=db.transaction(['current','mirror'],'readonly'),rows={};
                tx.oncomplete=()=>{db.close();resolve(rows);};tx.onabort=()=>{db.close();reject(tx.error);};
                for(const name of ['current','mirror']){const read=tx.objectStore(name).get(${JSON.stringify(installed.world)});
                    read.onsuccess=()=>{const row=read.result;if(!row||!(row.bytes instanceof Uint8Array)||row.bytes.length>9*1024*1024){tx.abort();return;}
                        rows[name]=Array.from(row.bytes,b=>b.toString(16).padStart(2,'0')).join('');};}
            };
        })`);
        assert.equal(envelopes.current,envelopes.mirror);
        await mkdir(directory+'/published-slot',{recursive:true});
        for(const name of ['current','mirror']){
            const bytes=Buffer.from(envelopes[name],'hex');assert.equal(bytes.subarray(0,4).toString(),'SVSG');
            assert.equal(createHash('sha256').update(bytes.subarray(0,-32)).digest('hex'),bytes.subarray(-32).toString('hex'));
            await writeFile(directory+'/published-slot/'+name,bytes,{flag:'wx',mode:0o600});
        }
        await click('salvage-pause');await wait(s=>s.pause.phase==='running','resume actual lift setup');
        const rest=await record('harbor-attached');
        const holdButton=async(id,ticks)=>{
            let point;const end=Date.now()+5000;
            do{
                point=await evaluate(`(()=>{const e=document.getElementById(${JSON.stringify(id)});
                    if(!e||e.hidden||e.disabled||!e.getClientRects().length)return null;e.scrollIntoView({block:'nearest'});
                    const r=e.getBoundingClientRect(),x=r.x+r.width/2,y=r.y+r.height/2;
                    return e.contains(document.elementFromPoint(x,y))?{x,y}:null;})()`);
                if(!point)await new Promise(r=>setTimeout(r,50));
            }while(!point&&Date.now()<end);
            assert(point,id+' is visible and enabled');
            const first=BigInt((await read()).player.tick);
            try{
                await call('Input.dispatchMouseEvent',{type:'mouseMoved',...point});
                await call('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',clickCount:1,...point});
                await wait(s=>{if(s.harbor.brokenMask){
                        if(s.harbor.lineLoads?.some(line=>line&&line.breakTick!=='0'))throw Error('Cable overload: '+JSON.stringify(s));
                        return false;
                    }
                    return BigInt(s.player.tick)>=first+BigInt(ticks);},'held harbor control steps');
            }finally{
                // Release outside the button, as a player can do while dragging.
                await call('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',clickCount:1,x:950,y:500});
            }
            await wait(s=>s.harbor.motor===0&&s.harbor.canOperate,'harbor motor stopped');
        };
        await holdButton('salvage-harbor-raise',600);
        // The force limiter can stall take-up while a wave loads the rig.
        // Continue the real held control up to a bounded 25-second total;
        // elapsed button time alone does not prove the clearance stop was reached.
        for(let step=0;step<3&&(await read()).harbor.lengths.some(n=>n>4.182);++step){
            await record('harbor-limited-raise-'+step);await holdButton('salvage-harbor-raise',300);
        }
        const raised=await record('harbor-raised');
        assert.equal(raised.harbor.brokenMask,0);
        // Fixed berth height and actual take-up matter in this wave-driven
        // scene: a boat already near a wave crest has less travel available.
        assert(raised.harbor.lengths.every(n=>Math.abs(n-4.18)<.002));
        assert(rest.harbor.lengths.some((n,i)=>n-raised.harbor.lengths[i]>.5));
        assert(raised.boat.position[1]>3&&raised.boat.position[1]>rest.boat.position[1]+.5);
        await click('salvage-pause');await wait(s=>s.pause.phase==='paused','suspended pause joined');
        await click('salvage-save');
        const end=Date.now()+10000;let receipt='';
        do{receipt=await evaluate('document.getElementById("salvage-save-status").textContent');
            if(receipt.startsWith('Saved.'))break;await new Promise(r=>setTimeout(r,50));}while(Date.now()<end);
        assert.match(receipt,/^Saved\./);const saved=await record('harbor-suspended-save');
        await call('Page.reload',{ignoreCache:true});await new Promise(r=>setTimeout(r,200));
        await wait(s=>s.ready&&s.restore?.phase==='ready'&&s.pause.phase==='paused','reload suspended harbor',true);
        const loaded=await record('harbor-suspended-reloaded');
        assert.equal(BigInt(loaded.pause.tick),BigInt(saved.pause.tick)+1n);
        assert(loaded.harbor.attached&&loaded.harbor.durable);assert.equal(loaded.harbor.brokenMask,0);
        assert.equal(loaded.harbor.motor,0);assert.deepEqual(loaded.harbor.lengths,saved.harbor.lengths);
        assert.deepEqual(loaded.session.inventory,delivered.session.inventory);
        assert.deepEqual(loaded.boat.paidPartIds,delivered.boat.paidPartIds);
        assert(Math.hypot(...loaded.tow.position.map((v,i)=>v-installed.tow.position[i]))<.001);
        assert(Math.hypot(...loaded.boat.position.map((v,i)=>v-saved.boat.position[i]))<.1);
        await click('salvage-pause');await holdButton('salvage-harbor-lower',660);await holdButton('salvage-harbor-lower',660);
        const lowered=await record('harbor-lowered-after-reload');assert.equal(lowered.harbor.brokenMask,0);
        assert(lowered.harbor.lengths.every((n,i)=>n-loaded.harbor.lengths[i]>4));
        assert(lowered.boat.position[1]<raised.boat.position[1]-.5);
        await click('salvage-harbor-release');await wait(s=>!s.harbor.attached&&s.harbor.stage===4,'four ropes released');
        await record('harbor-released');await walkTo([4.5,-53]);await wait(s=>s.player.interaction==='board','lowered boat alongside');
        await click('salvage-interact');await wait(s=>s.player.onBoat,'board lowered boat');
        await walkTo(s=>[s.boat.helmPosition[0],s.boat.helmPosition[2]]);await wait(s=>s.player.interaction==='helm','helm');
        await click('salvage-interact');await wait(s=>s.player.mode==='helm','use helm');await hold(['W'],90);
        const sailed=await record('harbor-sail-after-reload');assert(sailed.boat.speed>1);
        assert.deepEqual(sailed.session.inventory,delivered.session.inventory);
        await click('salvage-leave');await wait(s=>!s.active,'harbor drained Leave');
    },resumeOnly);
}
