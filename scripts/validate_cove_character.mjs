// Actual browser controls and reload. The observer only reads live game state.
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';

export async function validateCoveCharacter(call,directory){
    await mkdir(directory,{recursive:true});
    const began=Date.now(),deadline=began+180000;
    const report={status:'running',kind:'Browser robot, movement, camera modal and durable reload',stages:[]};
    const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
    const evaluate=async expression=>{
        const result=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        if(result.exceptionDetails)throw Error(JSON.stringify(result.exceptionDetails));return result.result.value;
    };
    const read=()=>evaluate('JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json()))');
    const wait=async(predicate,label,{reload=false,seconds=20}={})=>{
        const end=Math.min(deadline,Date.now()+seconds*1000);let state,error;
        while(Date.now()<end){
            try{state=await read();assert(!state.failed,'game failed');if(predicate(state))return state;}
            catch(value){if(!reload)throw value;error=String(value);}
            await delay(25);
        }
        throw Error(`${label}: ${JSON.stringify({state,error})}`);
    };
    const record=async(name,state=undefined)=>{
        state??=await read();assert(state.ready&&!state.failed&&state.terrainSurface==='lego');
        assert.deepEqual(await evaluate('globalThis.voxyUncapturedGpuErrors||[]'),[]);
        report.stages.push({name,state});await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));return state;
    };
    const input=async(name,down)=>{
        const special={Space:[' ','Space',32],F2:['F2','F2',113],Escape:['Escape','Escape',27]};
        const [key,code,number]=special[name]||[name.toLowerCase(),`Key${name}`,name.charCodeAt(0)];
        await call('Input.dispatchKeyEvent',{type:down?'keyDown':'keyUp',key,code,windowsVirtualKeyCode:number,nativeVirtualKeyCode:number});
    };
    const press=async name=>{try{await input(name,true);await delay(100);}finally{await input(name,false);}await delay(100);};
    const hold=async(name,ticks)=>{
        const before=BigInt((await read()).player.tick);
        try{await input(name,true);return await wait(s=>BigInt(s.player.tick)>=before+BigInt(ticks),'accepted movement ticks');}
        finally{await input(name,false);}
    };
    const click=async selector=>{
        await evaluate(`document.querySelector(${JSON.stringify(selector)})?.scrollIntoView({block:'nearest',behavior:'instant'})`);
        await delay(120);
        const p=await evaluate(`(()=>{const e=document.querySelector(${JSON.stringify(selector)});if(!e||e.disabled||!e.getClientRects().length)return null;
            const r=e.getBoundingClientRect(),x=r.x+r.width/2,y=r.y+r.height/2;return e.contains(document.elementFromPoint(x,y))?{x,y}:null;})()`);
        assert(p,`${selector} is visible, enabled and unobstructed`);
        await call('Input.dispatchMouseEvent',{type:'mouseMoved',...p});
        await call('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',buttons:1,clickCount:1,...p});
        await delay(70);await call('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',buttons:0,clickCount:1,...p});
    };
    const cameraChoice=s=>({mode:s.characterCamera.mode,distance:s.characterCamera.distance,
        reducedMotion:s.characterCamera.reducedMotion,frameLoad:s.characterCamera.frameLoad});
    try{
        await wait(s=>s.characterCamera?.valid&&s.character?.draws===34,'actual animated robot frame',{seconds:60});
        const initial=await record('robot-visible-on-lego-dock');
        if(process.env.VOXY_SMOKE_CHARACTER_SCREENSHOT){
            const image=await call('Page.captureScreenshot',{format:'png'});
            await writeFile(process.env.VOXY_SMOKE_CHARACTER_SCREENSHOT,Buffer.from(image.data,'base64'));
            report.visualCapture=process.env.VOXY_SMOKE_CHARACTER_SCREENSHOT;
        }
        const walked=await hold('W',12);assert.equal(walked.character.clip,'walk');
        assert(Math.hypot(...walked.player.feet.map((v,i)=>v-initial.player.feet[i]))>.15);
        await record('walk-uses-real-exported-clip',walked);
        await press('Space');
        const airborne=await wait(s=>s.player.mode==='airborne'&&['jump','fall'].includes(s.character.clip),'jump from dock');
        assert(!airborne.player.onBoat);assert.equal(airborne.player.rootKey,'0');
        await record('independent-airborne-character',airborne);
        await wait(s=>s.player.mode==='walking','land back on dock');
        await record('landed');
        await press('F2');await wait(s=>s.gamepad.menuOwner,'camera modal owns input');
        const modal=await read(),serial=BigInt(modal.assetFixture.submittedSerial);
        try{
            await input('W',true);
            await wait(s=>BigInt(s.assetFixture.submittedSerial)>=serial+5n,'frames while movement is held in menu');
        }finally{await input('W',false);}
        assert.deepEqual((await read()).player.feet,modal.player.feet,'camera menu cannot walk the robot');
        for(const action of [320,322,323])await click(`[data-camera-action="${action}"]`);
        await click('[data-camera-action="324"]');
        await wait(s=>s.characterCamera.distance<modal.characterCamera.distance,'menu zoom changes actual camera preference');
        await click('[data-camera-action="325"]');
        await click('[data-camera-action="324"]');
        await click('[data-camera-action="321"]');
        const changed=await wait(s=>s.characterCamera.valid&&s.characterCamera.mode!==modal.characterCamera.mode
            &&s.characterCamera.reducedMotion&&!modal.characterCamera.reducedMotion
            &&s.characterCamera.frameLoad&&!modal.characterCamera.frameLoad,'camera options applied');
        assert(Math.abs(Math.atan2(Math.sin(changed.characterCamera.yaw-changed.character.facingYaw),
            Math.cos(changed.characterCamera.yaw-changed.character.facingYaw)))<.002,'explicit menu recenter');
        await record('camera-options-through-visible-controls',changed);
        await press('F2');await wait(s=>!s.gamepad.menuOwner,'camera modal releases input');
        await press('P');await wait(s=>s.pause.phase==='paused','joined pause');
        await press('F2');await wait(s=>s.gamepad.menuOwner,'camera menu while paused');
        const paused=await read();await click('[data-camera-action="324"]');
        await wait(s=>s.characterCamera.distance<paused.characterCamera.distance,'paused menu zoom');
        assert.equal((await read()).pause.tick,paused.pause.tick);
        await press('F2');await wait(s=>!s.gamepad.menuOwner,'close paused camera');
        const saved=await record('paused-camera-settings-ready-to-save');
        await click('#salvage-save');
        const end=Date.now()+20000;
        while(Date.now()<end&&!await evaluate('document.getElementById("salvage-save-status").textContent.startsWith("Saved.")'))await delay(50);
        assert(await evaluate('document.getElementById("salvage-save-status").textContent.startsWith("Saved.")'),'actual storage acknowledgment');
        await record('durable-character-and-camera-save');
        await call('Page.reload',{ignoreCache:true});await delay(150);
        const restored=await wait(s=>s.ready&&s.restore?.phase==='ready'&&s.pause.phase==='paused'&&s.characterCamera?.valid,
            'restored robot and camera',{reload:true,seconds:60});
        assert.equal(restored.world,saved.world);assert.deepEqual(cameraChoice(restored),cameraChoice(saved));
        assert.deepEqual(restored.character.worldVelocity,saved.character.worldVelocity);
        assert(Math.abs(restored.character.facingYaw-saved.character.facingYaw)<1e-8);
        assert.deepEqual(restored.session.inventory,saved.session.inventory);assert.equal(restored.boat.parts,saved.boat.parts);
        assert.deepEqual(restored.boat.paidPartIds,saved.boat.paidPartIds);assert.equal(restored.player.mode,saved.player.mode);
        assert.equal(restored.character.draws,34);await record('reloaded-same-world-settings-and-ownership',restored);
        report.status='passed';
    }catch(error){report.status='failed';report.error=String(error);try{report.failureState=await read();}catch{}}
    finally{report.seconds=(Date.now()-began)/1000;await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));}
    assert.equal(report.status,'passed',report.error);return report;
}
