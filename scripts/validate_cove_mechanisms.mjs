// One bounded real-control mechanism journey. Read-only observations/export;
// no engine setters, camera injection, screenshots, rescue or repeated building.
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';

export async function validateCoveMechanisms(call,directory,{expectedPresentationParts=7}={}) {
    assert(Number.isInteger(expectedPresentationParts)&&expectedPresentationParts>=1&&expectedPresentationParts<=16);
    await mkdir(directory,{recursive:true});
    const started=Date.now(),deadline=started+300000,tau=2*Math.PI,held=new Set();
    const report={status:'running',kind:'Real propeller/winch controls, free settings, frozen phases and physical save/reload; no images',
        expectedPresentationParts,maximumSeconds:300,stages:[]};
    const evaluate=async expression=>{
        const result=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        if(result.exceptionDetails)throw Error(JSON.stringify(result.exceptionDetails));return result.result.value;
    };
    const read=()=>evaluate('JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json()))');
    const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
    const wait=async(predicate,label,{reload=false,seconds=20}={})=>{
        const end=Math.min(deadline,Date.now()+seconds*1000);let state,lastError;
        while(Date.now()<end){
            try{state=await read();lastError=null;if(predicate(state))return state;}
            catch(error){if(!reload)throw error;lastError=String(error);}
            await delay(30);
        }
        throw Error(`${label}: ${lastError||JSON.stringify(state)}`);
    };
    const key=async(letter,down)=>{
        await call('Input.dispatchKeyEvent',{type:down?'keyDown':'keyUp',key:letter.toLowerCase(),code:`Key${letter}`,
            windowsVirtualKeyCode:letter.charCodeAt(0),nativeVirtualKeyCode:letter.charCodeAt(0)});
        if(down)held.add(letter);else held.delete(letter);
    };
    const focus=()=>evaluate('voxyModule.canvas.focus({preventScroll:true})');
    const uncap=async()=>{
        if(await evaluate('voxyModule._voxy_get_uncapped_fps()'))return;
        await focus();
        for(const type of ['keyDown','keyUp'])await call('Input.dispatchKeyEvent',{
            type,key:'F9',code:'F9',windowsVirtualKeyCode:120,nativeVirtualKeyCode:120});
        const end=Math.min(deadline,Date.now()+10000);
        while(Date.now()<end){
            if(await evaluate('voxyModule._voxy_get_uncapped_fps()')){
                report.presentationControl='Existing F9 uncapped toggle (also applied after reload)';return;
            }
            await delay(30);
        }
        throw Error('F9 uncapped input was not accepted');
    };
    const mouse=async point=>{
        await call('Input.dispatchMouseEvent',{type:'mouseMoved',...point});
        await call('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',buttons:1,clickCount:1,...point});
        await call('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',buttons:0,clickCount:1,...point});
    };
    const click=async id=>{
        for(let n=0;n<8;++n){
            const point=await evaluate(`(()=>{let p=document.getElementById(${JSON.stringify(id)})?.parentElement,closed;
                while(p){if(p.tagName==='DETAILS'&&!p.open)closed=p;p=p.parentElement;}if(!closed)return null;
                const e=closed.querySelector('summary');e.scrollIntoView({block:'nearest'});
                const r=e.getBoundingClientRect();return{x:r.x+r.width/2,y:r.y+r.height/2};})()`);
            if(!point)break;await mouse(point);await delay(120);
        }
        const end=Math.min(deadline,Date.now()+10000);
        const locate=()=>evaluate(`(()=>{const e=document.getElementById(${JSON.stringify(id)});
            if(!e||e.hidden||e.disabled||!e.getClientRects().length)return null;
            const r=e.getBoundingClientRect(),x=r.x+r.width/2,y=r.y+r.height/2;
            return e.contains(document.elementFromPoint(x,y))?{x,y}:null;})()`);
        while(Date.now()<end){
            await evaluate(`document.getElementById(${JSON.stringify(id)})?.scrollIntoView({block:'nearest',behavior:'instant'})`);
            const before=await locate();await delay(160);const after=await locate();
            if(before&&after&&Math.hypot(before.x-after.x,before.y-after.y)<.5){await mouse(after);return;}
        }
        throw Error(`${id} must be stable, visible, enabled and unobstructed`);
    };
    const identity=s=>({incarnation:s.boat.physicsTicks.incarnation,build:s.boat.buildId,
        topology:s.boat.topologyRevision,roots:s.boat.roots.map(r=>r.key),
        bodyIndex:s.boat.mechanisms.bodyIndex,bodyGeneration:s.boat.mechanisms.bodyGeneration});
    const mechanism=s=>{
        assert(s.ready&&!s.failed&&s.boat.active,'active Cove required');
        const m=s.boat.mechanisms;assert(m,'mechanism observations required');
        for(const key of ['rotorRadians','drumRadians','effectiveDrive'])assert(Number.isFinite(m[key]),key);
        assert(m.rotorRadians>=0&&m.rotorRadians<tau&&m.drumRadians>=0&&m.drumRadians<tau);
        assert.equal(m.incarnation,s.boat.physicsTicks.incarnation);
        assert(m.bodyIndex>0&&m.bodyGeneration>0,'live primary body identity');
        assert.equal(m.animatedParts,s.workshop.open?0:2);
        assert.equal(s.assetFixture.presentationParts,expectedPresentationParts);
        assert.equal(s.terrainSurface,'lego');assert.equal(s.boat.physicsTicks.failed,false);return m;
    };
    const invariant=s=>{
        mechanism(s);assert.equal(s.boat.parts,11);assert.equal(s.boat.massKg,1035);
        const markings=BigInt(s.assetFixture.dockMarkingGpuBytes);
        assert(markings>0n&&markings<=16384n,'bounded owned dock inlay');
        assert.equal(BigInt(s.assetFixture.gpuReservationBytes),10745384n+markings);
        assert.equal(s.assetFixture.dockMarkingDraws,s.workshop.open?0:2);
        assert.deepEqual(s.boat.paidPartIds,[]);
        assert.deepEqual(s.session.inventory,{salvageMaterial:'48',specialMachinery:'0'});
    };
    const completed=async state=>{
        invariant(state);const owner=identity(state),m=mechanism(state);
        const proof=await wait(s=>{
            assert.deepEqual(identity(s),owner,'owner must remain unchanged while proving completion');
            return BigInt(s.assetFixture.completedSerial)>=BigInt(state.assetFixture.submittedSerial)
                &&BigInt(s.boat.physicsTicks.completed)>=BigInt(m.tick);
        },'captured mechanism frame completes');
        assert.deepEqual(await evaluate('globalThis.voxyUncapturedGpuErrors||[]'),[]);
        return {state,completion:{fixture:proof.assetFixture.completedSerial,physics:proof.boat.physicsTicks.completed,owner}};
    };
    const capture=async(name,state=null,extra={})=>{
        const sample=await completed(state||await read());report.stages.push({name,...sample,...extra});
        await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));return sample.state;
    };
    const residual=(actual,expected)=>Math.atan2(Math.sin(actual-expected),Math.cos(actual-expected));
    const closePhase=(actual,expected,label,tolerance=.0003)=>assert(Math.abs(residual(actual,expected))<tolerance,
        `${label}: actual=${actual}, expected=${expected}, residual=${residual(actual,expected)}`);
    const freeze=async(name)=>{
        const a=await capture(name+'-start'),ma=mechanism(a);
        const b=await wait(s=>BigInt(s.assetFixture.submittedSerial)>=BigInt(a.assetFixture.submittedSerial)+3n,'fresh frozen frames');
        assert.equal(b.pause.phase,a.pause.phase);assert.equal(b.workshop.open,a.workshop.open);
        const mb=mechanism(b);assert.equal(mb.effectiveDrive,0);
        closePhase(mb.rotorRadians,ma.rotorRadians,name+' rotor',.000001);
        closePhase(mb.drumRadians,ma.drumRadians,name+' drum',.000001);
        return capture(name+'-frozen',b);
    };
    const drive=async(letter,expected,label)=>{
        await focus();await key(letter,true);
        try{
            const a=await wait(s=>mechanism(s).effectiveDrive===expected,'stable '+label+' drive');
            await completed(a);const ma=mechanism(a);
            const b=await wait(s=>{
                assert.equal(mechanism(s).effectiveDrive,expected,'drive stays stable throughout phase window');
                const ticks=BigInt(s.boat.mechanisms.tick)-BigInt(ma.tick);
                // Whole turns hide a frozen rotor; half turns hide wrong
                // direction. Choose an endpoint that distinguishes both.
                return ticks>=6n&&(expected===0||Math.abs(Math.sin(4*Math.PI*expected*Number(ticks)/60))>.1);
            },label+' six submitted ticks');
            assert.deepEqual(identity(b),identity(a));const mb=mechanism(b),ticks=BigInt(mb.tick)-BigInt(ma.tick);
            closePhase(mb.rotorRadians-ma.rotorRadians,4*Math.PI*expected*Number(ticks)/60,label);
            await capture(label,b,{phaseStart:a,deltaTicks:String(ticks)});
        }finally{await key(letter,false);}
        await wait(s=>mechanism(s).effectiveDrive===0,'released throttle');
        await freeze(label+'-released');
    };
    const holdWalking=async(keys,ticks)=>{
        const first=BigInt((await read()).player.tick);await focus();
        try{for(const k of keys)await key(k,true);await wait(s=>BigInt(s.player.tick)>=first+BigInt(ticks),'walking ticks');}
        finally{for(const k of keys)await key(k,false);}
        const released=BigInt((await read()).player.tick);
        await wait(s=>BigInt(s.player.tick)>released,'fresh released movement observation');
    };
    const walkTo=async(target,reached=()=>false)=>{
        for(let n=0;n<100&&Date.now()<deadline;++n){
            const s=await read();if(reached(s))return;
            const p=s.player.feet,t=typeof target==='function'?target(s):target;
            const dx=t[0]-p[0],dz=t[1]-p[2],length=Math.hypot(dx,dz);
            if(length<.25){await holdWalking([],3);return;}
            const yaw=s.camera.yaw,forward=(dx*Math.sin(yaw)+dz*Math.cos(yaw))/length,
                right=(dx*Math.cos(yaw)-dz*Math.sin(yaw))/length,keys=[];
            if(Math.abs(forward)>.4)keys.push(forward>0?'W':'S');
            if(Math.abs(right)>.4)keys.push(right>0?'D':'A');
            await holdWalking(keys,Math.max(1,Math.min(5,Math.floor(length/.06)-1)));
        }
        throw Error('Could not reach actual interaction waypoint');
    };
    const board=async()=>{
        const near=s=>s.player.interaction==='board';
        await walkTo([6,-49.5],near);await walkTo([4.5,-49.5],near);await walkTo([4.5,-53],near);
        await wait(s=>s.player.interaction==='board','boarding reach');await click('salvage-interact');
        await wait(s=>s.player.onBoat,'board');await walkTo(s=>[s.boat.helmPosition[0],s.boat.helmPosition[2]],s=>s.player.interaction==='helm');
        await wait(s=>s.player.interaction==='helm','helm reach');await click('salvage-interact');
        await wait(s=>s.player.mode==='helm','use helm');
    };
    const dock=async()=>{
        if((await read()).player.mode==='helm'){
            await click('salvage-interact');await wait(s=>s.player.mode==='walking','leave helm');
        }
        // Actual navigation metadata: boat_boarding - helm_standing = [2.1,0,.1].
        // Rotate that fixed offset by the observed body quaternion; no camera or state setter.
        await walkTo(s=>{
            const [x,y,z,w]=s.boat.orientation,v=[2.1,0,.1];
            const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
            const uv=cross([x,y,z],v),uuv=cross([x,y,z],uv),r=v.map((n,i)=>n+2*(w*uv[i]+uuv[i]));
            return[s.boat.helmPosition[0]+r[0],s.boat.helmPosition[2]+r[2]];
        },s=>s.player.interaction==='dock');
        await wait(s=>s.player.interaction==='dock','return-to-dock reach');await click('salvage-interact');
        await wait(s=>!s.player.onBoat,'return to dock');
        await walkTo([4.5,-49.5],s=>s.workshop.canOpen);
        await walkTo([6,-49],s=>s.workshop.canOpen);
        await wait(s=>s.workshop.canOpen,'workshop station');
    };
    const open=async()=>{await click('salvage-workshop-toggle');
        await wait(s=>s.workshop.open&&s.boat.mechanisms.animatedParts===0&&s.boat.mechanisms.effectiveDrive===0,'open rendered workshop');};
    const selectPropeller=async()=>{
        for(let n=0;n<12;++n){
            const s=await read();if(s.workshop.name==='Propeller')return s;
            const old=s.workshop.selected;await click('workshop-next');await wait(x=>x.workshop.selected!==old,'Next part');
        }throw Error('Propeller must be selectable through Next');
    };
    const blueprint=()=>evaluate("voxyModule.ccall('voxy_salvage_blueprint_action','string',['number','string'],[1,''])");
    const refit=async(action,settings,label)=>{
        await open();await selectPropeller();await click(action);
        await wait(s=>s.workshop.changed&&Object.entries(settings).every(([k,v])=>s.workshop.settings[k]===v),label+' preview');
        await click('workshop-keep');await wait(s=>!s.workshop.changed&&s.workshop.canLaunch,label+' kept');
        const before=await capture(label+'-free-kept');
        for(const key of ['charge','refund','machineryCharge','machineryRefund'])assert.equal(before.workshop[key],'0');
        const design=await blueprint();assert(design.startsWith('53564250'));
        await click('workshop-launch');await wait(s=>!s.workshop.open&&!s.workshop.pending&&s.workshop.launches===before.workshop.launches+1
            &&s.boat.mechanisms.animatedParts===2,label+' Launch', {seconds:40});
        await capture(label+'-launched');return design;
    };
    const ropeMotion=async(letter,count,label)=>{
        await focus();await key(letter,true);
        try{
            const sign=letter==='Q'?1:-1;
            const a=await wait(s=>s.tow.confirmed&&Math.sign(s.tow.motor)===sign&&mechanism(s).ropeIndex>0&&BigInt(s.boat.mechanisms.ropeTick)>0n,label+' motor confirmed');
            await completed(a);const ma=mechanism(a);
            assert(ma.ropeGeneration>0&&Number.isFinite(ma.ropeLength));
            const b=await wait(s=>{
                assert(s.tow.attached&&!s.tow.broken);const m=mechanism(s);
                assert.equal(m.ropeIndex,ma.ropeIndex);assert.equal(m.ropeGeneration,ma.ropeGeneration);
                const length=m.ropeLength-ma.ropeLength;
                return BigInt(m.ropeTick)>=BigInt(ma.ropeTick)+BigInt(count)
                    &&Math.abs(Math.sin(-length/.28))>.1;
            },label+' accepted rope samples');
            const mb=mechanism(b),delta=mb.ropeLength-ma.ropeLength;
            assert(sign*delta<-.02,label+' changes actual rest length in requested direction');
            closePhase(mb.drumRadians-ma.drumRadians,-delta/.28,label+' measured drum rotation');
            await capture(label,b,{phaseStart:a,lengthDelta:delta});
        }finally{await key(letter,false);}
        await wait(s=>s.tow.motor===0&&s.tow.confirmed,label+' released motor');
    };
    try{
        const initial=await wait(s=>s.ready&&s.boat?.mechanisms?.animatedParts===2&&s.workshop.canOpen,'fresh mechanism Cove',{seconds:45});
        await uncap();
        invariant(initial);await capture('fresh-mechanisms',initial);
        await board();await drive('W',1,'forward-rotor');await drive('S',-1,'reverse-throttle-rotor');await dock();
        await refit('workshop-setting-reverse',{enabled:true,reversed:true},'reversed-propeller');
        await board();await drive('W',-1,'reversed-setting-rotor');await dock();
        const finalBlueprint=await refit('workshop-setting-enabled',{enabled:false,reversed:true},'disabled-propeller');
        report.blueprint=finalBlueprint;
        await board();await drive('W',0,'disabled-propeller-under-throttle');
        await click('salvage-hook');await wait(s=>s.tow.attached&&s.tow.confirmed&&mechanism(s).ropeIndex>0,'real hook and baseline');
        await capture('hooked');await ropeMotion('Q',8,'reel');await ropeMotion('Z',3,'pay-out');
        if(Math.abs(residual((await read()).boat.mechanisms.drumRadians,0))<=.01)
            await ropeMotion('Q',3,'checkpoint-reel');
        await click('salvage-hook');await wait(s=>!s.tow.attached&&mechanism(s).ropeIndex===0,'release cable');
        await capture('released-cable');await dock();await open();await selectPropeller();
        assert.deepEqual((await read()).workshop.settings,{enabled:false,limitPercent:100,reversed:true});
        assert.equal(await blueprint(),finalBlueprint,'physical controls preserve exact kept design');
        await freeze('workshop');await click('salvage-workshop-toggle');await wait(s=>!s.workshop.open&&s.boat.mechanisms.animatedParts===2,'close rendered workshop');
        await click('salvage-pause');await wait(s=>s.pause.phase==='paused','physical pause',{seconds:40});
        const paused=await freeze('paused');
        assert(Math.abs(residual(paused.boat.mechanisms.drumRadians,0))>.01,'save must contain a deliberately nonzero ephemeral drum phase');
        await click('salvage-save');
        const saveEnd=Math.min(deadline,Date.now()+30000);let durable=false;
        while(Date.now()<saveEnd){
            durable=await evaluate('document.getElementById("salvage-save-status").textContent.startsWith("Saved.")');
            if(durable)break;await delay(100);
        }
        assert(durable,'physical save must acknowledge durable storage');const saved=await capture('saved-mechanisms');
        await call('Page.reload',{ignoreCache:true});
        const restored=await wait(s=>s.ready&&s.restore?.phase==='ready'&&s.pause.phase==='paused'&&s.boat?.mechanisms?.animatedParts===2
            &&s.boat.mechanisms.incarnation===s.boat.physicsTicks.incarnation,'saved world restore',{reload:true,seconds:60});
        await uncap();
        invariant(restored);assert.deepEqual(restored.boat.paidPartIds,saved.boat.paidPartIds);
        assert.deepEqual(restored.session.inventory,saved.session.inventory);
        const mr=mechanism(restored);assert.equal(mr.rotorRadians,0);assert.equal(mr.drumRadians,0);assert.equal(mr.effectiveDrive,0);assert.equal(mr.ropeIndex,0);
        await capture('restored-neutral-phases',restored);
        await click('salvage-pause');await wait(s=>s.pause.phase==='running','resume');await open();await selectPropeller();
        assert.deepEqual((await read()).workshop.settings,{enabled:false,limitPercent:100,reversed:true});
        assert.equal(await blueprint(),finalBlueprint,'durable reload preserves exact design bytes');
        await capture('restored-exact-design-stock-and-settings');report.status='passed';
    }catch(error){
        report.status='failed';report.error=String(error);try{report.failureState=await read();}catch{}throw error;
    }finally{
        for(const letter of [...held])try{await key(letter,false);}catch{}
        report.elapsedSeconds=(Date.now()-started)/1000;
        await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));
    }
    return report;
}
