// Exercise shipped input and UI. No screenshots, camera setters or state injection.
import assert from 'node:assert/strict';
import {mkdir, writeFile} from 'node:fs/promises';

export async function validateCoveSettings(call, directory) {
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
    const report={status:'running',kind:'Real settings, physical controls, undo/redo and reset; no images',stages:[]};
    const record=async name=>{
        const s=await read();assert.equal(s.failed,false);assert.equal(s.terrainSurface,'lego');
        assert.deepEqual(await evaluate('globalThis.voxyUncapturedGpuErrors||[]'),[]);
        assert.equal(s.boat.parts,11);assert.equal(s.boat.massKg,1035);assert.equal(s.session.buildParts,11);
        assert.deepEqual(s.session.inventory,{salvageMaterial:'48',specialMachinery:'0'});assert.deepEqual(s.boat.paidPartIds,[]);
        if(s.workshop.launches){assert(s.boat.observedTick>=s.workshop.lastLaunchTick);assert(BigInt(s.boat.eventsThrough)>=BigInt(s.workshop.lastLaunchTick));}
        report.stages.push({name,state:s});await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));return s;
    };
    const select=async name=>{
        for(let i=0;i<12&&(await read()).workshop.name!==name;++i)await click('workshop-next');
        assert.equal((await read()).workshop.name,name);
    };
    const keep=async()=>{await click('workshop-keep');await wait(s=>!s.workshop.changed,'kept settings');};
    const open=async()=>{await click('salvage-workshop-toggle');await wait(s=>s.workshop.open,'open workshop');};
    const launch=async(button,count)=>{
        await click(button);await wait(s=>!s.workshop.open&&!s.workshop.pending&&s.workshop.launches===count,'confirmed setting launch '+count);
        return record('launch-'+count);
    };
    const board=async()=>{
        await walkTo(4.5,-49);await walkTo(4.5,-53);await hold(['E'],2);await wait(s=>s.player.onBoat,'board');
        await walkTo(s=>[s.boat.helmPosition[0],s.boat.helmPosition[2]]);await wait(s=>s.player.interaction==='helm','reach helm');
        await click('salvage-interact');await wait(s=>s.player.mode==='helm','use helm');
    };
    const reset=async()=>{
        const before=await read();await key('R',true);await key('R',false);
        await wait(s=>s.ready&&s.resets>before.resets&&!s.player.onBoat,'reset settings boat');
        await wait(s=>s.boat.observedTick>before.boat.observedTick&&Math.hypot(s.boat.position[0]+.5,s.boat.position[2]+54)<.5,'physical return to berth');
    };
    try {
        await wait(s=>s.ready&&s.workshop?.canOpen&&s.boat.observedTick>0,'cove ready');
        const initial=await record('default-outputs');assert(initial.boat.thrustLimitNewtons>0);assert(initial.boat.steeringLimitRadians>0);
        await open();assert.equal((await read()).workshop.name,'Winch');
        await click('workshop-setting-enabled');await wait(s=>s.workshop.changed&&!s.workshop.settings.enabled,'draft winch disabled');
        await click('workshop-revert');await wait(s=>!s.workshop.changed&&s.workshop.settings.enabled,'cancel winch setting');
        await click('workshop-setting-enabled');await keep();await click('workshop-undo');
        await wait(s=>s.workshop.settings.enabled&&!s.workshop.canLaunch,'undo local setting');
        await click('workshop-setting-enabled');await keep();
        for(const name of ['Propeller','Helm']) {
            await select(name);for(let i=0;i<4;i++)await click('workshop-setting-limit');
            await wait(s=>s.workshop.settings.limitPercent===0,'zero '+name);await keep();
        }
        const kept=await record('kept-unapplied-settings');assert.equal(kept.boat.thrustLimitNewtons,initial.boat.thrustLimitNewtons);
        assert.equal(kept.workshop.charge,'0');assert.equal(kept.workshop.refund,'0');
        const stopped=await launch('workshop-launch',1);assert.equal(stopped.boat.thrustLimitNewtons,0);assert.equal(stopped.boat.steeringLimitRadians,0);assert.equal(stopped.tow.hasWinch,false);
        await board();await delay(160);assert.equal(await evaluate('document.getElementById("salvage-hook").disabled'),true);
        const start=await read();await hold(['W','D'],180);const still=await record('zero-output-under-throttle-and-steer');
        assert(Math.hypot(still.boat.position[0]-start.boat.position[0],still.boat.position[2]-start.boat.position[2])<1,'disabled propulsion must not sail away');
        await reset();await open();
        await select('Propeller');assert.equal((await read()).workshop.settings.limitPercent,0);
        await click('workshop-setting-limit');await click('workshop-setting-limit');await click('workshop-setting-reverse');await keep();
        await select('Helm');for(let i=0;i<3;++i)await click('workshop-setting-limit');await keep();
        await select('Winch');assert.equal((await read()).workshop.settings.enabled,false);await click('workshop-setting-enabled');await keep();
        const tuned=await launch('workshop-launch',2);
        assert(Math.abs(tuned.boat.thrustLimitNewtons-initial.boat.thrustLimitNewtons*.75)<.01);
        assert(Math.abs(tuned.boat.steeringLimitRadians-initial.boat.steeringLimitRadians*.5)<.00001);
        assert.deepEqual(tuned.boat.thrustDirection,initial.boat.thrustDirection.map(v=>v===0?0:-v));assert.equal(tuned.tow.hasWinch,true);
        await open();await select('Propeller');const restored=(await read()).workshop;
        assert.deepEqual(restored.settings,{enabled:true,limitPercent:75,reversed:true});assert.equal(restored.canLaunch,false);
        await select('Helm');assert.equal((await read()).workshop.settings.limitPercent,50);
        await click('salvage-workshop-toggle');await board();
        const before=await read();await hold(['W'],150);const reversing=await record('reversed-thrust-sails-backward');
        assert(reversing.boat.position[2]-before.boat.position[2]>1,'W must drive backward with reversed propeller');
        await reset();const resetState=await record('reset-preserves-outputs');assert.equal(resetState.boat.thrustLimitNewtons,tuned.boat.thrustLimitNewtons);
        assert.deepEqual(resetState.boat.thrustDirection,tuned.boat.thrustDirection);assert.equal(resetState.tow.hasWinch,true);
        await open();const undo=await launch('workshop-undo-launch',3);assert.equal(undo.boat.thrustLimitNewtons,0);assert.equal(undo.boat.steeringLimitRadians,0);assert.equal(undo.tow.hasWinch,false);
        await open();const redo=await launch('workshop-redo-launch',4);assert.equal(redo.boat.thrustLimitNewtons,tuned.boat.thrustLimitNewtons);assert.deepEqual(redo.boat.thrustDirection,tuned.boat.thrustDirection);assert.equal(redo.tow.hasWinch,true);
        await click('salvage-leave');const deadline=Date.now()+20000;
        while(Date.now()<deadline&&!await evaluate('location.search.includes("experience=lego-world")'))await delay(50);
        assert(await evaluate('location.search.includes("experience=lego-world")'),'Leave drains configured craft');report.status='passed';
    }catch(error){report.status='failed';report.error=String(error);throw error;}
    finally{await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));}
    return report;
}
