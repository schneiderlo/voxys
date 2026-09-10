// Actual Save button, page reload and paid rebuilding. No state injection or images.
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';
import {validateCoveArchive} from './validate_cove_archive.mjs';
export async function validateCoveSaves(call,directory,resume=false){
    await mkdir(directory,{recursive:true});
    const evaluate=async expression=>{
        const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));return r.result.value;
    };
    const read=()=>evaluate('JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json()))');
    const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
    const wait=async(predicate,label,{reload=false,timeoutMs=reload?60000:20000}={})=>{
        const deadline=Date.now()+timeoutMs;let state,error;
        do {
            try {state=await read();error=null;if(predicate(state))return state;}
            catch(value){if(!reload)throw value;error=String(value);}
            await delay(50);
        }while(Date.now()<deadline);
        throw Error(`${label}: ${error||JSON.stringify(state)}`);
    };
    const click=async id=>{
        await evaluate(`(()=>{
            if(globalThis.voxyCoveSaveInputTrace)return;
            globalThis.voxyCoveSaveInputTrace=[];
            for(const kind of ['pointerdown','pointerup','click'])document.addEventListener(kind,event=>{
                const trace=globalThis.voxyCoveSaveInputTrace;
                trace.push({kind,id:event.target.closest?.('button')?.id||event.target.id||event.target.tagName,
                    x:event.clientX,y:event.clientY,trusted:event.isTrusted});
                if(trace.length>32)trace.shift();
            },true);
        })()`);
        await evaluate(`document.getElementById(${JSON.stringify(id)})?.scrollIntoView({block:'nearest',behavior:'instant'})`);
        const deadline=Date.now()+5000;let point;
        const locate=()=>evaluate(`(()=>{const e=document.getElementById(${JSON.stringify(id)});
            if(!e||e.hidden||e.disabled||!e.getClientRects().length)return null;
            const r=e.getBoundingClientRect(),x=r.x+r.width/2,y=r.y+r.height/2;
            return e.contains(document.elementFromPoint(x,y))?{x,y}:null;})()`);
        do {
            const candidate=await locate();
            if(candidate){
                await call('Input.dispatchMouseEvent',{type:'mouseMoved',...candidate});
                await delay(160);
                const settled=await locate();
                if(settled&&Math.hypot(settled.x-candidate.x,settled.y-candidate.y)<.5){point=settled;break;}
            }else await delay(60);
        }while(Date.now()<deadline);
        assert(point,`${id} must be visible and enabled`);
        await call('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',clickCount:1,...point});
        await call('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',clickCount:1,...point});
        const trace=await evaluate('globalThis.voxyCoveSaveInputTrace.slice(-3)');
        report.inputs.push({id,point,trace});
        assert(trace.some(event=>event.kind==='click'&&event.id===id),`Click did not reach ${id}: ${JSON.stringify(trace)}`);
    };
    const key=(letter,down)=>call('Input.dispatchKeyEvent',{type:down?'keyDown':'keyUp',key:letter.toLowerCase(),code:`Key${letter}`,
        windowsVirtualKeyCode:letter.charCodeAt(0),nativeVirtualKeyCode:letter.charCodeAt(0)});
    const hold=async(keys,ticks,physics=false)=>{
        const clock=s=>BigInt(physics?s.boat.physicsTicks.completed:s.player.tick),first=clock(await read());
        try {for(const k of keys)await key(k,true);await wait(s=>clock(s)>=first+BigInt(ticks),
            physics?'completed machine ticks':'input ticks',{timeoutMs:physics?10000+ticks*2000:20000});}
        finally {for(const k of keys)await key(k,false);}
    };
    const walkTo=async(target)=>{
        for(let attempt=0;attempt<100;attempt++){
            const s=await read(),p=s.player.feet,t=typeof target==='function'?target(s):target;
            const dx=t[0]-p[0],dz=t[1]-p[2],distance=Math.hypot(dx,dz);
            // Match the delivered cove journey: a displayed frame may consume
            // several walking steps. The actual Board/Helm checks below still
            // decide whether interaction is possible at this waypoint.
            if(distance<.25){await hold([],20);return;}
            const yaw=s.camera.yaw,forward=(dx*Math.sin(yaw)+dz*Math.cos(yaw))/distance,right=(dx*Math.cos(yaw)-dz*Math.sin(yaw))/distance;
            const keys=[];if(Math.abs(forward)>.4)keys.push(forward>0?'W':'S');if(Math.abs(right)>.4)keys.push(right>0?'D':'A');
            await hold(keys,Math.max(1,Math.min(5,Math.floor(distance/.06)-1)));
        }
        throw Error('Could not reach restored boat: '+JSON.stringify({target:typeof target==='function'?target(await read()):target,state:await read()}));
    };
    const report={status:'running',kind:'real paid construction, Save, three page reloads, fresh-owner purchase, accepted job, restored winch and sailing',stages:[],inputs:[]};
    const record=async name=>{
        const state=await read();assert.equal(state.failed,false);assert.equal(state.terrainSurface,'lego');
        assert.deepEqual(await evaluate('globalThis.voxyUncapturedGpuErrors||[]'),[]);
        report.stages.push({name,state});await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));return state;
    };
    const addPontoon=async parts=>{
        await click('salvage-workshop-toggle');await wait(s=>s.workshop.open,'workshop');
        await click('workshop-add');await wait(s=>s.workshop.valid&&s.workshop.changed,'new pontoon fits');
        await click('workshop-keep');await wait(s=>s.workshop.canLaunch,'kept paid design');
        assert.equal((await read()).workshop.charge,'24');
        await click('workshop-launch');await wait(s=>s.ready&&!s.workshop.pending&&!s.workshop.open&&s.boat.parts===parts&&s.pause.canPause,'paid launch');
    };
    const saveAndReload=async name=>{
        await click('salvage-pause');await wait(s=>s.pause.phase==='paused','pause for save');
        const saved=await record(name+'-before-save');
        const captured=await validateCoveArchive(call,directory,name+'-archive');
        assert.equal(captured.archive.schema,4,'the live host captures every section in v4');
        await click('salvage-save');
        const deadline=Date.now()+20000;
        while(Date.now()<deadline&&!await evaluate('document.getElementById("salvage-save-status").textContent.startsWith("Saved.")'))await delay(60);
        assert(await evaluate('document.getElementById("salvage-save-status").textContent.startsWith("Saved.")'),'storage must acknowledge save');
        assert.equal(await evaluate('new URLSearchParams(location.search).get("world")'),saved.world);
        await record(name+'-stored');
        await call('Page.reload',{ignoreCache:true});await delay(200);
        await wait(s=>s.ready&&s.restore?.phase==='ready'&&s.pause.phase==='paused','restore completed',{reload:true});
        const restored=await record(name+'-reloaded');
        assert.equal(restored.world,saved.world);assert.equal(restored.restore.baseTick,saved.pause.tick);
        assert.equal(BigInt(restored.pause.tick),BigInt(saved.pause.tick)+1n,'one bounded initial restoration tick');
        assert.equal(restored.boat.parts,saved.boat.parts);assert.equal(restored.boat.massKg,saved.boat.massKg);
        assert.deepEqual(restored.boat.paidPartIds,saved.boat.paidPartIds);assert.deepEqual(restored.session.inventory,saved.session.inventory);
        assert.equal(restored.session.cargo,saved.session.cargo);assert.equal(restored.job.phase,saved.job.phase);
        assert.equal(restored.session.revision,saved.session.revision);
        assert.equal(BigInt(restored.observation.epoch),BigInt(saved.observation.epoch)+1n);
        assert.notEqual(restored.observation.incarnation,saved.observation.incarnation);
        assert.equal(restored.pause.waterTime,saved.pause.waterTime);
        assert.equal(restored.tow.attached,saved.tow.attached);assert.equal(restored.tow.broken,saved.tow.broken);
        assert.equal(restored.tow.ropeLength,saved.tow.ropeLength);assert.equal(restored.tow.motor,0);
        if(saved.tow.attached)assert.equal(restored.tow.ropeObservedTick,restored.pause.tick);
        assert(Math.hypot(...restored.boat.position.map((v,i)=>v-saved.boat.position[i]))<.2,'controlled one-tick boat settling');
        assert(Math.hypot(...restored.tow.position.map((v,i)=>v-saved.tow.position[i]))<.2,'separate cargo restored in place');
        assert.equal(restored.player.onBoat,saved.player.onBoat);assert.equal(restored.player.mode,saved.player.mode);
        assert.equal(restored.player.rootKey,saved.player.rootKey);assert.equal(restored.tow.rootKey,saved.tow.rootKey);
        assert.equal(restored.boat.controlPart,saved.boat.controlPart);
        assert.deepEqual(restored.boat.roots.map(root=>root.key),saved.boat.roots.map(root=>root.key));
        assert.equal(restored.boat.joinedTick,restored.pause.tick);
        return restored;
    };
    try {
        if(resume){
            report.kind='resume the saved two-pontoon expedition, then actual towing save/reload and sailing';
            await wait(s=>s.ready&&s.restore?.phase==='ready'&&s.pause.phase==='paused','saved paid expedition',{reload:true});
            const saved=await record('paid-expedition-resumed');assert.equal(saved.boat.parts,13);
            assert.equal(saved.boat.paidPartIds.length,2);assert.equal(saved.session.inventory.salvageMaterial,'0');
            await click('salvage-pause');await wait(s=>s.pause.phase==='running','resume saved paid expedition');
        }else{
        await wait(s=>s.ready&&s.boat?.active&&s.workshop?.canOpen&&s.pause.canPause,'fresh cove');
        await record('fresh-world');await addPontoon(12);
        const purchased=await record('first-paid-pontoon');assert.equal(purchased.boat.massKg,1155);
        assert.equal(purchased.session.inventory.salvageMaterial,'24');const firstId=purchased.boat.paidPartIds[0];
        await saveAndReload('first');await click('salvage-pause');await wait(s=>s.pause.phase==='running','resume first load');
        await addPontoon(13);const second=await record('purchase-after-reload');
        assert.equal(second.boat.massKg,1275);assert.equal(second.session.inventory.salvageMaterial,'0');
        assert(second.boat.paidPartIds.includes(firstId));assert.equal(new Set(second.boat.paidPartIds).size,2);
        await saveAndReload('second');await click('salvage-pause');await wait(s=>s.pause.phase==='running','resume second load');
        }
        await walkTo([4.5,-49]);await walkTo([4.5,-53]);await wait(s=>s.player.interaction==='board','reachable boat boarding');
        await hold(['E'],2);await wait(s=>s.player.onBoat,'board restored craft');
        await walkTo(s=>[s.boat.helmPosition[0],s.boat.helmPosition[2]]);await wait(s=>s.player.interaction==='helm','restored helm');
        await click('salvage-interact');await wait(s=>s.player.mode==='helm','use restored helm');
        await record('restored-helm');
        await click('salvage-job-accept');await wait(s=>s.job.phase==='accepted'&&!s.job.pending,'job accepted after reload');
        await hold(['F'],2);await wait(s=>s.tow.attached&&s.tow.confirmed,'hook salvage on restored boat');
        const hooked=await record('restored-boat-hooks-cargo');await hold(['Q'],30,true);
        await wait(s=>s.tow.motor===0&&s.tow.confirmed&&s.pause.canPause,'winch stops before save');
        assert((await read()).tow.ropeLength<hooked.tow.ropeLength-.25);
        const loadedTow=await saveAndReload('towing');await click('salvage-pause');
        await wait(s=>s.pause.phase==='running','resume with saved tow');
        await hold(['Q'],12,true);await wait(s=>s.tow.motor===0&&s.tow.confirmed,'saved winch still reels');
        const reeled=await record('saved-winch-reels-after-reload');
        assert.equal(reeled.tow.broken,false);assert(reeled.tow.ropeLength<loadedTow.tow.ropeLength-.1);
        await hold(['F'],2);await wait(s=>!s.tow.attached&&s.tow.confirmed,'release restored cable');
        const helm=await record('restored-cargo-released');await hold(['W'],120,true);const sailed=await record('restored-boat-sails');
        assert(sailed.boat.speed>.2);assert(Math.hypot(sailed.boat.position[0]-helm.boat.position[0],sailed.boat.position[2]-helm.boat.position[2])>.4);
        await click('salvage-leave');const deadline=Date.now()+20000;
        while(Date.now()<deadline&&!await evaluate('location.search.includes("experience=lego-world")'))await delay(50);
        assert(await evaluate('location.search.includes("experience=lego-world")'),'drained Leave');report.status='passed';
    }catch(error){report.status='failed';report.error=String(error);throw error;}
    finally {await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));}
    return report;
}
