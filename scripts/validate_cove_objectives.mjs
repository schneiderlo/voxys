// One short real-control objective/card journey. Read-only game observations;
// no gameplay injection, screenshots, forced scrolling or repeated sailing.
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';

export async function validateCoveObjectives(call,directory,{expectedPresentationParts=7}={}) {
    assert(Number.isInteger(expectedPresentationParts)&&expectedPresentationParts>=1&&expectedPresentationParts<=16);
    await mkdir(directory,{recursive:true});
    const started=Date.now(),deadline=started+90000;
    const report={status:'running',maximumSeconds:90,expectedPresentationParts,
        kind:'Real objective Accept, workshop hide/restore and natural 1280/640 layout; no images',stages:[]};
    const evaluate=async expression=>{
        const result=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        if(result.exceptionDetails)throw Error(JSON.stringify(result.exceptionDetails));return result.result.value;
    };
    const read=()=>evaluate('JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json()))');
    const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
    const wait=async(predicate,label)=>{
        const end=Math.min(deadline,Date.now()+15000);let state;
        while(Date.now()<end){state=await read();if(await predicate(state))return state;await delay(30);}
        throw Error(`${label}: ${JSON.stringify(state)}`);
    };
    const focus=()=>evaluate('voxyModule.canvas.focus({preventScroll:true})');
    const press=async(key,code,keyCode)=>{
        await focus();
        try{await call('Input.dispatchKeyEvent',{type:'keyDown',key,code,windowsVirtualKeyCode:keyCode,nativeVirtualKeyCode:keyCode});}
        finally{await call('Input.dispatchKeyEvent',{type:'keyUp',key,code,windowsVirtualKeyCode:keyCode,nativeVirtualKeyCode:keyCode});}
    };
    const uncap=async()=>{
        const before=await evaluate('voxyModule._voxy_get_uncapped_fps()');
        if(!before)await press('F9','F9',120);
        await wait(()=>evaluate('Boolean(voxyModule._voxy_get_uncapped_fps())'),'existing F9 uncapped control');
        report.presentationControl={initiallyUncapped:Boolean(before),physicalF9:!before,uncapped:true};
    };
    // Same stable, unobstructed center proof as the mechanism journey. The
    // objective is already visible; deliberately do not scroll it into view.
    const traceBegin=()=>evaluate(`(()=>{
        const entries=[],types=['pointerdown','mousedown','pointerup','mouseup','click'];
        const rect=e=>{if(!e)return null;const r=e.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height};};
        const target=e=>e?{id:e.id,tag:e.tagName,hidden:Boolean(e.hidden),disabled:Boolean(e.disabled),rect:rect(e)}:null;
        const snapshot=(kind,event=null)=>{
            if(entries.length>=64)return;
            const action=document.getElementById('salvage-objective-action');
            entries.push({kind,time:performance.now(),type:event?.type,phase:event?.eventPhase,
                trusted:event?.isTrusted,button:event?.button,buttons:event?.buttons,x:event?.clientX,y:event?.clientY,
                defaultPrevented:event?.defaultPrevented,target:target(event?.target),active:target(document.activeElement),
                hit:event?target(document.elementFromPoint(event.clientX,event.clientY)):null,
                action:target(action),originalAccept:target(document.getElementById('salvage-job-accept')),
                step:document.getElementById('salvage-objective').dataset.step});
        };
        const capture=event=>snapshot('capture',event),bubble=event=>snapshot('bubble',event);
        for(const type of types){document.addEventListener(type,capture,{capture:true,passive:true});document.addEventListener(type,bubble,{passive:true});}
        globalThis.voxyObjectiveInputTrace={entries,snapshot,stop(){for(const type of types){document.removeEventListener(type,capture,true);document.removeEventListener(type,bubble);}}};
        snapshot('before-single-click');return entries;
    })()`);
    const traceMark=async name=>{
        report.inputTrace=await evaluate(`(()=>{const t=globalThis.voxyObjectiveInputTrace;t?.snapshot(${JSON.stringify(name)});return t?.entries||[];})()`);
    };
    const click=async id=>{
        const locate=()=>evaluate(`(()=>{const e=document.getElementById(${JSON.stringify(id)});
            if(!e||e.hidden||e.disabled||!e.getClientRects().length)return null;
            const r=e.getBoundingClientRect(),x=r.x+r.width/2,y=r.y+r.height/2;
            return e.contains(document.elementFromPoint(x,y))?{x,y}:null;})()`);
        const end=Math.min(deadline,Date.now()+10000);
        while(Date.now()<end){
            const before=await locate();await delay(160);const after=await locate();
            if(before&&after&&Math.hypot(before.x-after.x,before.y-after.y)<.5){
                report.clickPoint=after;await traceMark('before-move');
                await call('Input.dispatchMouseEvent',{type:'mouseMoved',...after});await traceMark('after-move');
                await call('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',buttons:1,clickCount:1,...after});await traceMark('after-press');
                await call('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',buttons:0,clickCount:1,...after});await traceMark('after-release');return;
            }
        }
        throw Error(`${id} must be naturally visible, stable, enabled and unobstructed`);
    };
    const identity=s=>({world:s.world,incarnation:s.boat.physicsTicks.incarnation,build:s.boat.buildId,
        topology:s.boat.topologyRevision,roots:s.boat.roots.map(r=>r.key),
        bodyIndex:s.boat.mechanisms.bodyIndex,bodyGeneration:s.boat.mechanisms.bodyGeneration});
    const ownership=s=>({parts:s.boat.parts,mass:s.boat.massKg,paidPartIds:s.boat.paidPartIds,inventory:s.session.inventory});
    let initialOwner,initialStock;
    const invariant=s=>{
        assert(s.ready&&s.active&&!s.failed&&s.boat.active,'active Cove');
        assert.equal(s.boat.physicsTicks.failed,false);assert.equal(s.terrainSurface,'lego');
        assert.equal(s.assetFixture.presentationParts,expectedPresentationParts);
        assert(s.boat.mechanisms.bodyIndex>0&&s.boat.mechanisms.bodyGeneration>0,'live body identity');
        assert.equal(s.boat.mechanisms.incarnation,s.boat.physicsTicks.incarnation);
        assert.equal(s.boat.mechanisms.animatedParts,s.workshop.open?0:2);
        assert.equal(s.session.admissionOpen,true);assert.equal(s.pause.phase,'running');
        if(initialOwner)assert.deepEqual(identity(s),initialOwner,'same world and body throughout');
        if(initialStock)assert.deepEqual(ownership(s),initialStock,'job/UI controls preserve exact boat ownership and stock');
    };
    const dom=()=>evaluate(`(()=>{
        const rect=r=>({left:r.left,top:r.top,right:r.right,bottom:r.bottom,width:r.width,height:r.height});
        const element=id=>{const e=document.getElementById(id);if(!e)return null;
            const style=getComputedStyle(e),r=e.getBoundingClientRect(),range=document.createRange();range.selectNodeContents(e);
            return {id,text:e.textContent.trim(),hidden:e.hidden,disabled:Boolean(e.disabled),
                visible:!e.hidden&&style.display!=='none'&&style.visibility!=='hidden'&&Boolean(e.getClientRects().length),
                rect:rect(r),textRects:[...range.getClientRects()].map(rect),clientWidth:e.clientWidth,scrollWidth:e.scrollWidth,
                centerTarget:e.contains(document.elementFromPoint(r.x+r.width/2,r.y+r.height/2))};};
        const p=document.getElementById('salvage-preview'),r=p.getBoundingClientRect();
        return {width:innerWidth,height:innerHeight,documentWidth:document.documentElement.scrollWidth,
            documentClientWidth:document.documentElement.clientWidth,scrollX,scrollY,
            panel:{rect:rect(r),scrollTop:p.scrollTop,scrollLeft:p.scrollLeft,clientWidth:p.clientWidth,
                clientHeight:p.clientHeight,scrollWidth:p.scrollWidth,scrollHeight:p.scrollHeight,
                clip:{left:r.left+p.clientLeft,top:r.top+p.clientTop,right:r.left+p.clientLeft+p.clientWidth,bottom:r.top+p.clientTop+p.clientHeight}},
            step:document.getElementById('salvage-objective').dataset.step,
            card:element('salvage-objective'),label:element('salvage-objective-label'),title:element('salvage-objective-title'),
            detail:element('salvage-objective-detail'),action:element('salvage-objective-action'),originalAccept:element('salvage-job-accept')};
    })()`);
    const within=(r,b,label)=>assert(r.width>0&&r.height>0&&r.left>=b.left-1&&r.top>=b.top-1
        &&r.right<=b.right+1&&r.bottom<=b.bottom+1,`${label} clipped: ${JSON.stringify({r,b})}`);
    const bounds=d=>{
        assert.equal(d.scrollX,0);assert.equal(d.scrollY,0);assert.equal(d.panel.scrollTop,0);assert.equal(d.panel.scrollLeft,0);
        assert(d.documentWidth<=d.documentClientWidth+1,'no horizontal page overflow');
        assert(d.panel.scrollWidth<=d.panel.clientWidth+1,'no horizontal panel overflow');
        const viewport={left:0,top:0,right:d.width,bottom:d.height};
        for(const name of ['card','label','title','detail','action']){
            const e=d[name];assert(e?.visible,`${name} visible`);within(e.rect,viewport,name+' viewport');
            within(e.rect,d.panel.clip,name+' panel');
            if(name!=='card'){
                within(e.rect,d.card.rect,name+' card');assert(e.scrollWidth<=e.clientWidth+1,name+' text wraps');
                for(const r of e.textRects)if(r.width>0&&r.height>0)within(r,e.rect,name+' text');
            }
        }
        assert(d.action.centerTarget,'objective action unobstructed');
        assert.equal(d.label.text,'Next objective');assert.equal(d.action.disabled,false);
        assert.equal(d.step,'accept');assert.equal(d.action.text,d.originalAccept.text);
    };
    const capture=async(name,{layout=false,hidden=false,step}={})=>{
        const state=await read();invariant(state);const observed=await dom(),owner=identity(state);
        if(hidden){assert(observed.card.hidden&&!observed.card.visible);assert(observed.action.hidden&&observed.action.disabled);}
        else{assert(observed.card.visible&&!observed.card.hidden);if(step)assert.equal(observed.step,step);}
        if(layout)bounds(observed);
        const completion=await wait(s=>{
            invariant(s);assert.deepEqual(identity(s),owner);
            return BigInt(s.assetFixture.completedSerial)>=BigInt(state.assetFixture.submittedSerial)
                &&BigInt(s.boat.physicsTicks.completed)>=BigInt(state.boat.mechanisms.tick);
        },'captured state has later completed GPU and physics work');
        assert.deepEqual(await evaluate('globalThis.voxyUncapturedGpuErrors||[]'),[],'no GPU errors');
        report.stages.push({name,state,dom:observed,completion:{fixture:completion.assetFixture.completedSerial,
            physics:completion.boat.physicsTicks.completed,owner}});
        await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));return state;
    };
    const resize=async(width,height)=>{
        const serial=BigInt((await read()).assetFixture.submittedSerial);
        await call('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:false});
        await wait(async s=>BigInt(s.assetFixture.submittedSerial)>=serial+3n&&await evaluate(`innerWidth===${width}&&innerHeight===${height}`),'resized viewport and fresh rendered frames');
    };
    try{
        await wait(s=>s.ready&&s.boat?.mechanisms&&s.workshop.canOpen&&s.job.phase==='available'&&!s.job.pending,'fresh objective Cove');
        await uncap();const initial=await read();initialOwner=identity(initial);initialStock=ownership(initial);
        assert.equal(typeof initial.world,'string');assert(initial.world.length>0);
        assert.deepEqual(initialStock,{parts:11,mass:1035,paidPartIds:[],inventory:{salvageMaterial:'48',specialMachinery:'0'}});
        await resize(1280,800);
        await wait(async()=>{const d=await dom();return d.step==='accept'&&d.action.visible&&!d.action.disabled;},'initial available objective');
        await capture('wide-available-objective',{layout:true,step:'accept'});
        await resize(640,480);await capture('narrow-available-objective',{layout:true,step:'accept'});
        await traceBegin();await click('salvage-objective-action');
        await wait(async s=>s.job.phase==='accepted'&&!s.job.pending&&(await dom()).step==='board','real objective Accept handler');
        await capture('accepted-job-preserves-boat-stock',{step:'board'});
        await press('b','KeyB',66);
        await wait(async s=>s.workshop.open&&(await dom()).card.hidden,'B opens workshop and hides objective');
        await capture('workshop-hides-objective',{hidden:true});
        await press('b','KeyB',66);
        await wait(async s=>!s.workshop.open&&(await dom()).step==='board'&&(await dom()).card.visible,'B closes workshop and restores objective');
        const final=await capture('workshop-restores-objective',{step:'board'});assert.equal(final.job.phase,'accepted');
        report.status='passed';
    }catch(error){
        report.status='failed';report.error=String(error);
        try{report.failureState=await read();report.failureDom=await dom();}catch{}
        throw error;
    }finally{
        try{await traceMark('journey-finished');await evaluate('globalThis.voxyObjectiveInputTrace?.stop()');}catch{}
        // Restore the runner's documented wide viewport through browser controls.
        try{await call('Emulation.setDeviceMetricsOverride',{width:1280,height:800,deviceScaleFactor:1,mobile:false});}catch{}
        report.elapsedSeconds=(Date.now()-started)/1000;
        await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));
    }
    return report;
}
