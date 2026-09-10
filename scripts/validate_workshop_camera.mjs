// Actual camera/input controls in the playable Cove. No images or state setters.
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';
export async function validateWorkshopCamera(call,directory) {
    await mkdir(directory,{recursive:true});
    const evaluate=async expression=>{const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));return r.result.value;};
    const read=()=>evaluate('JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json()))');
    const delay=ms=>new Promise(r=>setTimeout(r,ms));
    const wait=async(predicate,label)=>{let s;const end=Date.now()+25000;
        do{s=await read();assert(!s.failed);if(predicate(s))return s;await delay(40);}while(Date.now()<end);throw Error(label+': '+JSON.stringify(s));};
    const move=p=>call('Input.dispatchMouseEvent',{type:'mouseMoved',...p});
    const mouse=(type,p,button='left',buttons=0)=>call('Input.dispatchMouseEvent',{type,...p,button,buttons,clickCount:1});
    const key=async(code,down)=>call('Input.dispatchKeyEvent',{type:down?'keyDown':'keyUp',code:code==='Shift'?'ShiftLeft':'Key'+code,
        key:code==='Shift'?'Shift':code.toLowerCase(),windowsVirtualKeyCode:code==='Shift'?16:code.charCodeAt(0)});
    const click=async id=>{
        await evaluate(`document.getElementById(${JSON.stringify(id)}).scrollIntoView({block:'nearest',behavior:'instant'})`);
        const locate=()=>evaluate(`(()=>{const e=document.getElementById(${JSON.stringify(id)}),r=e.getBoundingClientRect();
            if(e.hidden||e.disabled)return null;const x=r.x+r.width/2,y=r.y+r.height/2;
            return e.contains(document.elementFromPoint(x,y))?{x,y}:null;})()`);
        let point;const end=Date.now()+10000;
        do{const candidate=await locate();if(candidate){await move(candidate);await delay(180);const settled=await locate();
            if(settled&&Math.hypot(settled.x-candidate.x,settled.y-candidate.y)<.5)point=settled;
        }else await delay(60);}while(!point&&Date.now()<end);
        assert(point,id);await mouse('mousePressed',point,'left',1);await mouse('mouseReleased',point);
    };
    const blueprint=()=>evaluate("voxyModule.ccall('voxy_salvage_blueprint_action','string',['number','string'],[1,''])");
    const point=()=>evaluate('(()=>{const r=voxyModule.canvas.getBoundingClientRect();return {x:r.x+r.width*.25,y:r.y+r.height*.25}})()');
    const report={status:'running',kind:'Native game camera through browser DOM, mouse and keyboard; no images',stages:[]};
    const record=async name=>{report.stages.push({name,state:await read()});await writeFile(directory+'/summary.json',JSON.stringify(report,null,2));};
    try {
        await evaluate(`(()=>{globalThis.workshopCameraClicks=[];document.addEventListener('click',e=>{
            const button=e.target.closest?.('button');if(button){const list=globalThis.workshopCameraClicks;
            if(list.length===32)list.shift();list.push({id:button.id,action:button.dataset.workshopAction,disabled:button.disabled,trusted:e.isTrusted});}
        },{capture:true});})()`);
        let s=await wait(s=>s.ready,'ready');
        if(s.pause.phase==='paused'){await click('salvage-pause');await wait(s=>s.pause.phase==='running','resume');}
        await click('salvage-workshop-toggle');await wait(s=>s.workshop.open&&s.workshop.camera.rectangle[2]<.8,'whole boat beside panel');
        await delay(300);const initial=await read(),design=await blueprint();await record('whole-boat-framed');
        await click('workshop-focus');await wait(s=>Math.abs(s.workshop.camera.distance-initial.workshop.camera.distance)>.1,'selected part focus');
        await record('selected-part-focused');await click('workshop-frame');await delay(250);
        assert.equal(await blueprint(),design);
        let p=await point();await move(p);await delay(150);s=await read();const yaw=s.workshop.camera.yaw,elevation=s.workshop.camera.elevation;
        await mouse('mousePressed',p,'right',2);await delay(150);
        p={x:p.x+65,y:p.y+35};await move({...p,button:'right',buttons:2});
        await wait(s=>Math.abs(s.workshop.camera.yaw-yaw)>.1&&Math.abs(s.workshop.camera.elevation-elevation)>.1,'orbit');
        await mouse('mouseReleased',p,'right');assert.equal(await blueprint(),design);await record('orbit-preserves-design');
        const target=(await read()).workshop.camera.target;
        await key('Shift',true);await mouse('mousePressed',p,'right',2);await delay(150);
        p={x:p.x+48,y:p.y-22};await move({...p,button:'right',buttons:2,modifiers:8});
        await wait(s=>Math.hypot(...s.workshop.camera.target.map((v,i)=>v-target[i]))>.2,'pan');
        // Releasing over UI must end the gesture, not leave a held camera input.
        const panel=await evaluate('(()=>{const r=document.getElementById("salvage-preview").getBoundingClientRect();return {x:r.x+20,y:r.y+20}})()');
        await move(panel);await mouse('mouseReleased',panel,'right');await key('Shift',false);await delay(200);
        const settled=(await read()).workshop.camera;await move(p);await delay(200);
        assert.deepEqual((await read()).workshop.camera,settled);await record('pan-and-outside-release');
        const distance=settled.distance;
        await call('Input.dispatchMouseEvent',{type:'mouseWheel',...p,deltaX:0,deltaY:-180});
        await wait(s=>s.workshop.camera.distance<distance-.1,'wheel zoom');
        await move(panel);await delay(100);const beforeUi=(await read()).workshop.camera;
        await call('Input.dispatchMouseEvent',{type:'mouseWheel',...panel,deltaX:0,deltaY:220});await delay(300);
        assert.deepEqual((await read()).workshop.camera,beforeUi);assert.equal(await blueprint(),design);await record('wheel-respects-panel');
        await click('workshop-brick-2x4');await wait(s=>s.workshop.changed,'brick ghost');
        const ghost=(await read()).workshop; p=await point();await move(p);await delay(250);
        const placed=(await read()).workshop.placement;
        await mouse('mousePressed',p,'right',2);await delay(150);p={x:p.x+40,y:p.y+15};await move({...p,button:'right',buttons:2});
        await mouse('mousePressed',p,'left',3);await delay(200);
        assert((await read()).workshop.changed);assert.deepEqual((await read()).workshop.placement,placed);
        await mouse('mouseReleased',p,'left',2);await move(panel);await mouse('mouseReleased',panel,'right');
        assert.equal((await read()).workshop.parts,ghost.parts);await click('workshop-revert');await wait(s=>!s.workshop.changed,'discard ghost');
        assert.equal(await blueprint(),design);await record('camera-gesture-cannot-place-ghost');
        await call('Emulation.setDeviceMetricsOverride',{width:600,height:960,deviceScaleFactor:1,mobile:false});
        await wait(s=>s.workshop.camera.rectangle[1]>-.1,'portrait viewport above controls');await delay(300);
        assert(await evaluate('(()=>{const p=document.getElementById("salvage-preview");return p.scrollWidth<=p.clientWidth+1})()'),'workshop controls must fit the panel');
        assert.equal(await blueprint(),design);await record('portrait-framing-and-wrapped-controls');
        await call('Emulation.clearDeviceMetricsOverride');await delay(350);
        await click('salvage-workshop-toggle');await wait(s=>!s.workshop.open,'close workshop');
        const final=await read();assert.deepEqual(final.session.inventory,initial.session.inventory);
        assert.deepEqual(final.boat.paidPartIds,initial.boat.paidPartIds);assert.equal(final.boat.parts,initial.boat.parts);
        await record('returned-to-dock-with-unchanged-owned-boat');report.status='passed';
    }catch(error){report.status='failed';report.error=String(error);try{report.failureState=await read();}catch{}throw error;}
    finally{try{report.clicks=await evaluate('globalThis.workshopCameraClicks');}catch{}await writeFile(directory+'/summary.json',JSON.stringify(report,null,2));}
    return report;
}
