// Exercise shipped input and UI. No screenshots, camera setters or state injection.
import assert from 'node:assert/strict';
import {mkdir, writeFile} from 'node:fs/promises';

export async function validateCoveLaunch(call, directory) {
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
    const report={status:'running',kind:'Real launch, sailing, undo/redo and rejected launch; no images',stages:[]};
    const record=async name=>{
        const s=await read();assert.equal(s.failed,false);assert.equal(s.terrainSurface,'lego');
        assert.deepEqual(await evaluate('globalThis.voxyUncapturedGpuErrors||[]'),[]);
        assert.equal(s.boat.buildId,'6');assert.equal(s.session.buildParts,s.boat.parts);
        assert.deepEqual(s.session.inventory,{salvageMaterial:'48',specialMachinery:'0'});
        if(s.workshop.launches) {
            assert(s.boat.observedTick>=s.workshop.lastLaunchTick);
            assert(BigInt(s.boat.eventsThrough)>=BigInt(s.workshop.lastLaunchTick));
            assert(BigInt(s.boat.physicsTicks.completed)>=BigInt(s.workshop.lastLaunchTick));
        }
        report.stages.push({name,state:s});await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));return s;
    };
    const launch=async(button,count,parts)=>{
        await click(button);
        await wait(s=>!s.workshop.open&&!s.workshop.pending&&s.workshop.launches===count,'confirmed launch '+count);
        const s=await record('launch-'+count);assert.equal(s.boat.parts,parts);return s;
    };
    const open=async()=>{await click('salvage-workshop-toggle');await wait(s=>s.workshop.open,'open workshop');};
    const select=async name=>{
        for(let i=0;i<12;i++) {if((await read()).workshop.name===name)return;await click('workshop-next');}
        throw Error('Missing selectable '+name);
    };
    try {
        await wait(s=>s.ready&&s.workshop?.canOpen&&s.boat.observedTick>0,'cove ready');
        const initial=await record('original');assert.equal(initial.boat.parts,11);
        await open();await click('workshop-snap');
        await wait(s=>s.workshop.valid&&s.workshop.changed,'move winch to socket');
        await click('workshop-keep');await wait(s=>s.workshop.canLaunch,'kept edit can launch');
        await click('workshop-launch');await wait(s=>!s.workshop.pending&&s.workshop.launchMessage.includes('standing room'),'reject blocked boarding');
        const blocked=await record('blocked-winch-refused');assert.equal(blocked.workshop.launches,0);assert.equal(blocked.boat.parts,11);
        assert.equal(blocked.boat.topologyRevision,'0');
        await click('workshop-undo');await select('Cargo cradle');await click('workshop-remove');
        await wait(s=>s.workshop.parts===10&&s.workshop.valid,'remove cradle');await click('workshop-keep');
        const reduced=await launch('workshop-launch',1,10);assert(reduced.boat.massKg<initial.boat.massKg);
        assert(reduced.player.collisionBoxes<initial.player.collisionBoxes,'removed cradle loses its walking collision');
        await open();const undone=await launch('workshop-undo-launch',2,11);assert.equal(undone.boat.massKg,1035);
        await open();const redone=await launch('workshop-redo-launch',3,10);assert.equal(redone.boat.massKg,reduced.boat.massKg);
        // Put the winch on the deck socket freed by removing the cradle.
        // This changes its physical tow anchor as well as its visible placement.
        await open();await select('Winch');
        await click('workshop-right');await click('workshop-right');
        await wait(s=>s.workshop.valid&&s.workshop.changed,'winch fits freed deck socket');
        const winchPlacement=(await read()).workshop.placement;
        await click('workshop-keep');const movedWinch=await launch('workshop-launch',4,10);
        assert.equal(movedWinch.boat.massKg,reduced.boat.massKg);
        await open();assert.deepEqual((await read()).workshop.placement,winchPlacement);
        await click('salvage-workshop-toggle');await wait(s=>!s.workshop.open,'dock after winch launch');
        // Missing propulsion can still be a connected workshop design. Launch
        // must reject it without touching accepted identities, hull or mass.
        await open();await select('Propeller');await click('workshop-remove');
        await wait(s=>s.workshop.valid&&s.workshop.parts===9,'connected design without propeller');await click('workshop-keep');
        await click('workshop-launch');await wait(s=>!s.workshop.pending&&s.workshop.launchMessage.includes('one propeller'),'reject missing propulsion');
        const refused=await record('invalid-launch-refused');assert.equal(refused.boat.topologyRevision,movedWinch.boat.topologyRevision);
        assert.equal(refused.workshop.launches,4);assert.equal(refused.boat.massKg,redone.boat.massKg);
        await click('workshop-undo');await wait(s=>!s.workshop.canLaunch&&s.workshop.parts===10,'restore retained design');
        await click('salvage-workshop-toggle');await wait(s=>!s.workshop.open,'dock');
        await walkTo(4.5,-49);await walkTo(4.5,-53);await hold(['E'],2);await wait(s=>s.player.onBoat,'board edited boat');
        await walkTo(s=>[s.boat.helmPosition[0],s.boat.helmPosition[2]]);await wait(s=>s.player.interaction==='helm','edited helm');
        await click('salvage-interact');await wait(s=>s.player.mode==='helm','use helm');
        const helm=await record('edited-boat-helm');
        assert(helm.tow.operable,'moved winch must remain operable');
        if(!helm.tow.inRange)await hold(['W','D'],45);
        await wait(s=>s.tow.distance<7.5,'sail within moved winch hook range');
        await record('moved-winch-approach');
        await click('salvage-hook');await wait(s=>s.tow.attached&&s.tow.confirmed,'hook using moved winch');
        const hooked=await record('moved-winch-hooked');await hold(['Q'],30);
        await wait(s=>s.tow.motor===0&&s.tow.confirmed,'stop moved winch');
        const reeled=await record('moved-winch-reeled');assert(!reeled.tow.broken);
        assert(reeled.tow.distance<hooked.tow.distance-.15,'new anchor must pull real cargo');
        await click('salvage-hook');await wait(s=>!s.tow.attached&&s.tow.confirmed,'release moved winch');
        await hold(['W'],180);
        const sailing=await record('edited-boat-sailing');assert(sailing.boat.speed>.2);
        assert(Math.hypot(sailing.boat.position[0]-helm.boat.position[0],sailing.boat.position[2]-helm.boat.position[2])>.5);
        await hold(['W','D'],120);const turned=await record('edited-boat-turning');
        assert(Math.abs(sailing.boat.orientation.reduce((sum,v,i)=>sum+v*turned.boat.orientation[i],0))<.999);
        const resets=turned.resets;await key('R',true);await key('R',false);
        await wait(s=>s.ready&&s.resets>resets&&!s.player.onBoat,'reset edited boat');
        await wait(s=>s.boat.observedTick>turned.boat.observedTick
            &&Math.hypot(s.boat.position[0]+.5,s.boat.position[2]+54)<.5,'physical edited boat returns to berth');
        const recovered=await record('edited-boat-reset');assert.equal(recovered.boat.parts,10);
        assert.equal(recovered.boat.topologyRevision,movedWinch.boat.topologyRevision);assert.equal(recovered.boat.massKg,reduced.boat.massKg);
        await click('salvage-leave');
        const deadline=Date.now()+20000;
        while(Date.now()<deadline&&!await evaluate('location.search.includes("experience=lego-world")'))await delay(50);
        assert(await evaluate('location.search.includes("experience=lego-world")'),'Leave drains all replaced resources');
        report.status='passed';
    }catch(error){report.status='failed';report.error=String(error);throw error;}
    finally {await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));}
    return report;
}
