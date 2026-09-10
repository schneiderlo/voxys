// One actual-control workshop journey. No screenshots or simulation setters.
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';
export async function validateCoveWorkshop(call,directory) {
    await mkdir(directory,{recursive:true});
    const evaluate=async expression=>{
        const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));return r.result.value;
    };
    const read=()=>evaluate('JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json()))');
    const delay=ms=>new Promise(r=>setTimeout(r,ms));
    const wait=async(predicate,label)=>{
        const deadline=Date.now()+20000;let state;
        do{state=await read();if(predicate(state))return state;await delay(40);}while(Date.now()<deadline);
        throw Error(`${label}: ${JSON.stringify(state)}`);
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
    const key=async(letter,down)=>call('Input.dispatchKeyEvent',{type:down?'keyDown':'keyUp',
        key:letter.toLowerCase(),code:`Key${letter}`,windowsVirtualKeyCode:letter.charCodeAt(0),nativeVirtualKeyCode:letter.charCodeAt(0)});
    const tap=async letter=>{await key(letter,true);await delay(150);await key(letter,false);await delay(150);};
    const report={status:'running',kind:'Real starter workshop controls; design editing, not live launch; no images',stages:[]};
    const record=async name=>{
        const s=await read();assert.equal(s.failed,false);assert.equal(s.terrainSurface,'lego');
        assert.deepEqual(await evaluate('globalThis.voxyUncapturedGpuErrors||[]'),[]);
        assert.equal(s.boat.parts,11);assert.equal(s.boat.massKg,1035);
        assert.equal(s.session.builds,1);assert.equal(s.session.buildParts,11);assert.equal(s.session.buildConnections,17);
        assert.equal(s.boat.buildId,'6');assert.equal(s.boat.topologyRevision,'0');assert.equal(s.session.revision,'0');
        assert.deepEqual(s.session.inventory,{salvageMaterial:'48',specialMachinery:'0'});
        report.stages.push({name,state:s});return s;
    };
    try {
        await wait(s=>s.ready&&s.workshop?.canOpen&&s.boat.observedTick>0,'cove ready');
        const start=await record('dock');
        await click('salvage-workshop-toggle');await wait(s=>s.workshop.open,'open workshop');await delay(150);
        const opened=await record('workshop-open');assert.equal(opened.workshop.name,'Winch');
        const original=opened.workshop.placement;
        await click('workshop-raise');await wait(s=>!s.workshop.valid,'disconnected preview');await delay(150);
        await record('invalid-placement');
        assert.equal(await evaluate('document.getElementById("workshop-keep").disabled'),true);
        // A key on a focused UI button must neither edit the design nor move the player.
        const rotation=(await read()).workshop.rotation;await tap('R');
        assert.equal((await read()).workshop.rotation,rotation);
        assert.deepEqual((await read()).player.feet,start.player.feet);
        await click('workshop-revert');await wait(s=>s.workshop.valid&&!s.workshop.changed,'cancel invalid preview');
        await click('workshop-snap');await wait(s=>s.workshop.valid&&s.workshop.changed,'snap to real socket');await delay(150);
        const snapped=await record('socket-snap');assert.notDeepEqual(snapped.workshop.placement,original);
        await click('workshop-keep');await wait(s=>s.workshop.revision==='1','keep change');await delay(150);
        await record('design-kept');
        await click('workshop-undo');await wait(s=>s.workshop.revision==='2','undo');
        assert.deepEqual((await read()).workshop.placement,original);
        await record('undo-restores');
        await click('workshop-next');await wait(s=>s.workshop.name==='Cargo cradle','select cradle');
        await click('workshop-remove');await wait(s=>s.workshop.parts===10&&s.workshop.valid,'remove cradle');await delay(150);
        assert((await read()).workshop.massKg<1035);await click('workshop-keep');
        await wait(s=>s.workshop.revision==='3','keep removal');await record('cradle-removed');await delay(150);
        await click('workshop-undo');await wait(s=>s.workshop.parts===11&&s.workshop.revision==='4','restore cradle');
        // Canvas click enables keyboard commands while preserving a free pointer.
        const canvasPoint=await evaluate(`(()=>{const c=document.getElementById('voxy-canvas');const r=c.getBoundingClientRect();
            for(const fx of [.15,.3,.5])for(const fy of [.5,.7,.3]){const x=r.x+r.width*fx,y=r.y+r.height*fy;
                if(document.elementFromPoint(x,y)===c)return {x,y};}return null;})()`);
        assert(canvasPoint,'game view must remain visible beside the workshop');await mouse(canvasPoint);
        assert.equal(await evaluate('document.activeElement.id'),'voxy-canvas','view click restores keyboard focus');await tap('R');
        await wait(s=>s.workshop.rotation!==0,'keyboard rotation');await record('keyboard-rotation');
        assert.equal(await evaluate('Boolean(document.pointerLockElement)'),false);
        assert.deepEqual((await read()).player.feet,start.player.feet);
        await click('workshop-revert');const view=(await read()).camera.yaw;
        await click('workshop-orbit-right');await wait(s=>Math.abs(s.camera.yaw-view)>.1,'camera orbit');
        await record('orbit-view');
        await click('salvage-workshop-toggle');await wait(s=>!s.workshop.open,'return to dock');await delay(150);
        await record('return-to-dock');
        await click('salvage-workshop-toggle');await wait(s=>s.workshop.open,'reopen existing design');
        assert.equal((await read()).workshop.revision,'4');await delay(150);
        await click('salvage-workshop-toggle');await wait(s=>!s.workshop.open,'close before sailing');
        // Existing controls still move the actual player after leaving the workshop.
        await key('S',true);await wait(s=>Math.hypot(s.player.feet[0]-start.player.feet[0],s.player.feet[2]-start.player.feet[2])>.25,'walk after workshop');
        await key('S',false);await record('walking-resumes');await delay(150);
        await click('salvage-leave');
        const deadline=Date.now()+20000;
        while(Date.now()<deadline && !await evaluate('location.search.includes("experience=lego-world")'))await delay(50);
        assert(await evaluate('location.search.includes("experience=lego-world")'),'Leave drains and exits');
        report.status='passed';
    }catch(error){report.status='failed';report.error=String(error);throw error;}
    finally {await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));}
    return report;
}
