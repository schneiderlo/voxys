// Exercise shipped input and UI. No screenshots, camera setters or state injection.
import assert from 'node:assert/strict';
import {mkdir, writeFile} from 'node:fs/promises';

export async function validateCoveParts(call, directory) {
    await mkdir(directory, {recursive:true});
    const expression='JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json()))';
    const evaluate=async expression=>{
        const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));
        return r.result.value;
    };
    const read=()=>evaluate(expression);
    const delay=ms=>new Promise(r=>setTimeout(r,ms));
    const wait=async(predicate,label)=>{
        const until=Date.now()+20000;
        let state;
        do{state=await read(); if(predicate(state))return state; await delay(30);}while(Date.now()<until);
        throw Error(`${label}: ${JSON.stringify(state)}`);
    };
    const key=(letter,down)=>call('Input.dispatchKeyEvent',{type:down?'keyDown':'keyUp',
        key:letter==='Space'?' ':letter.toLowerCase(),code:letter==='Space'?'Space':`Key${letter}`,
        windowsVirtualKeyCode:letter==='Space'?32:letter.charCodeAt(0),nativeVirtualKeyCode:letter==='Space'?32:letter.charCodeAt(0)});
    const hold=async(keys,ticks)=>{
        const first=BigInt((await read()).player.tick);
        try{
            for(const k of keys)await key(k,true);
            await wait(s=>BigInt(s.player.tick)>=first+BigInt(ticks),'movement ticks');
        }finally{for(const k of keys)await key(k,false);}
    };
    const mouse=async p=>{
        await call('Input.dispatchMouseEvent',{type:'mouseMoved',...p});
        await call('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',clickCount:1,...p});
        await call('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',clickCount:1,...p});
    };
    const click=async id=>{
        // Let the 100 ms HUD refresh finish before locating its next control.
        await delay(160);
        const p=await evaluate(`(()=>{const e=document.getElementById(${JSON.stringify(id)});
            if(!e||e.hidden||e.disabled||!e.getClientRects().length)return null;
            e.scrollIntoView({block:'nearest'});const r=e.getBoundingClientRect();
            const x=r.x+r.width/2,y=r.y+r.height/2;
            if(!e.contains(document.elementFromPoint(x,y)))return null;return {x,y};})()`);
        assert(p,`${id} must be visible, enabled and unobstructed`);await mouse(p);
    };
    const walkTo=async(x,z)=>{
        for(let n=0;n<100;++n){
            const s=await read(), p=s.player.feet, target=typeof x==='function'?x(s):[x,z];
            const dx=target[0]-p[0],dz=target[1]-p[2],length=Math.hypot(dx,dz);
            if(length<.14){await hold([],20);return;}
            const yaw=s.camera.yaw;
            const forward=(dx*Math.sin(yaw)+dz*Math.cos(yaw))/length;
            const right=(dx*Math.cos(yaw)-dz*Math.sin(yaw))/length;
            const keys=[];
            if(Math.abs(forward)>.4)keys.push(forward>0?'W':'S');
            if(Math.abs(right)>.4)keys.push(right>0?'D':'A');
            await hold(keys,Math.max(1,Math.min(5,Math.floor(length/.06)-1)));
        }
        throw Error(`Walk failed to reach ${x},${z}: ${JSON.stringify((await read()).player)}`);
    };
    const report={status:'running',kind:'Real paid-part construction, economy, undo/redo and sailing; no images',stages:[]};
    const record=async(name,parts,material)=>{
        const s=await read();assert.equal(s.failed,false);assert.equal(s.terrainSurface,'lego');
        assert.deepEqual(await evaluate('globalThis.voxyUncapturedGpuErrors||[]'),[]);
        assert.equal(s.boat.parts,parts);assert.equal(s.session.buildParts,parts);
        assert.deepEqual(s.session.inventory,{salvageMaterial:String(material),specialMachinery:'0'});
        assert.equal(s.boat.paidPartIds.length,parts-11,'starter loans must not become paid copies');
        if(s.workshop.launches) {
            assert(s.boat.observedTick>=s.workshop.lastLaunchTick);
            assert(BigInt(s.boat.eventsThrough)>=BigInt(s.workshop.lastLaunchTick));
        }
        report.stages.push({name,state:s});await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));return s;
    };
    const open=async()=>{await click('salvage-workshop-toggle');await wait(s=>s.workshop.open,'open workshop');};
    const launch=async(button,number,parts,material)=>{
        await click(button);await wait(s=>!s.workshop.open&&!s.workshop.pending&&s.workshop.launches===number,'confirm paid launch '+number);
        return record('launch-'+number,parts,material);
    };
    const add=async()=>{
        await click('workshop-add');await wait(s=>s.workshop.valid&&s.workshop.changed,'new part fits sockets');
        await click('workshop-keep');await wait(s=>!s.workshop.changed,'keep new part');
    };
    try {
        await wait(s=>s.ready&&s.workshop?.canOpen&&s.boat.observedTick>0,'cove ready');
        const initial=await record('starting-stock',11,48);assert.equal(initial.boat.massKg,1035);
        await open();assert.equal((await read()).workshop.catalogName,'Pontoon');
        await click('workshop-add');await wait(s=>s.workshop.valid&&s.workshop.parts===12,'preview added pontoon');
        await record('unpaid-ghost',11,48);await click('workshop-revert');
        await wait(s=>s.workshop.parts===11&&!s.workshop.changed,'cancel ghost');await record('canceled-ghost',11,48);
        await add();const kept=await record('kept-paid-design',11,48);
        assert.equal(kept.workshop.charge,'24');assert.equal(kept.workshop.refund,'0');assert(kept.workshop.canLaunch);
        const first=await launch('workshop-launch',1,12,24);assert.equal(first.boat.massKg,1155);
        const firstId=first.boat.paidPartIds[0];assert(first.player.collisionBoxes>initial.player.collisionBoxes);
        await open();await launch('workshop-undo-launch',2,11,48);
        await open();const restored=await launch('workshop-redo-launch',3,12,24);assert.equal(restored.boat.paidPartIds[0],firstId);
        await open();await add();const second=await launch('workshop-launch',4,13,0);assert.equal(second.boat.massKg,1275);
        assert(second.boat.paidPartIds.includes(firstId));assert.notEqual(second.boat.paidPartIds[0],second.boat.paidPartIds[1]);
        // A design can be planned beyond today's stock, but cannot be launched.
        await open();await click('workshop-catalog-next');await wait(s=>s.workshop.catalogName==='Beam','choose beam');
        await add();const unfunded=await record('unfunded-design',13,0);
        assert.equal(unfunded.workshop.charge,'12');assert.equal(unfunded.workshop.affordable,false);assert.equal(unfunded.workshop.canLaunch,false);
        await delay(160);assert.equal(await evaluate('document.getElementById("workshop-launch").disabled'),true);
        await click('workshop-undo');await wait(s=>!s.workshop.canLaunch&&s.workshop.parts===13,'undo unfunded draft');
        await click('salvage-workshop-toggle');await wait(s=>!s.workshop.open,'dock');
        await walkTo(4.5,-49);await walkTo(4.5,-53);await hold(['E'],2);await wait(s=>s.player.onBoat,'board expanded craft');
        await walkTo(s=>[s.boat.helmPosition[0],s.boat.helmPosition[2]]);await wait(s=>s.player.interaction==='helm','expanded helm');
        await click('salvage-interact');await wait(s=>s.player.mode==='helm','use expanded helm');
        const helm=await record('expanded-boat-helm',13,0);await hold(['W'],180);
        const sailing=await record('expanded-boat-sailing',13,0);
        assert(sailing.boat.speed>.2);assert(Math.hypot(sailing.boat.position[0]-helm.boat.position[0],sailing.boat.position[2]-helm.boat.position[2])>.5);
        await hold(['W','D'],120);const turned=await record('expanded-boat-turning',13,0);
        assert(Math.abs(sailing.boat.orientation.reduce((sum,v,i)=>sum+v*turned.boat.orientation[i],0))<.999);
        const resets=turned.resets;await key('R',true);await key('R',false);
        await wait(s=>s.ready&&s.resets>resets&&!s.player.onBoat,'reset expanded boat');
        await wait(s=>s.boat.observedTick>turned.boat.observedTick&&Math.hypot(s.boat.position[0]+.5,s.boat.position[2]+54)<.5,'expanded boat returns to berth');
        const reset=await record('reset-does-not-refill-stock',13,0);assert.deepEqual(reset.boat.paidPartIds,second.boat.paidPartIds);
        // Remove an actually purchased pontoon. The catalog's salvage value is
        // smaller than purchase cost; Undo must restore that same paid identity.
        await open();for(let i=0;i<14&&(await read()).workshop.selected!==25;i++)await click('workshop-next');
        assert.equal((await read()).workshop.selected,25);await click('workshop-remove');
        await wait(s=>s.workshop.valid&&s.workshop.parts===12,'remove paid pontoon');await click('workshop-keep');
        const quote=(await read()).workshop;const refund=BigInt(quote.refund);assert(refund>0n&&refund<24n);assert.equal(quote.charge,'0');
        const removed=await launch('workshop-launch',5,12,refund);assert(!removed.boat.paidPartIds.includes(firstId));
        await open();const undone=await launch('workshop-undo-launch',6,13,0);assert.deepEqual(undone.boat.paidPartIds,second.boat.paidPartIds);

        if(process.env.VOXY_SMOKE_COVE_ARCHIVE==='1') {
            await wait(s=>s.pause?.canPause,'expanded boat ready to pause');
            await click('salvage-pause');await wait(s=>s.pause.phase==='paused','expanded boat paused');
            const {validateCoveArchive}=await import('./validate_cove_archive.mjs');
            report.archive=await validateCoveArchive(call,directory,'paid-boat-archive');
        }
        await click('salvage-leave');const deadline=Date.now()+20000;
        while(Date.now()<deadline&&!await evaluate('location.search.includes("experience=lego-world")'))await delay(50);
        assert(await evaluate('location.search.includes("experience=lego-world")'),'Leave drains added shapes');report.status='passed';
    }catch(error){report.status='failed';report.error=String(error);throw error;}
    finally {await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));}
    return report;
}
