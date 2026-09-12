// One real painted brick, repaint in place, and durable reload. No screenshots,
// state setters or synthetic engine actions; blueprint export is read-only.
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';

export async function validateCovePaint(call,directory,{expectedPresentationParts=3}={}) {
    assert(Number.isInteger(expectedPresentationParts)&&expectedPresentationParts>=1&&expectedPresentationParts<=16,
        'expectedPresentationParts must be an integer between 1 and 16');
    await mkdir(directory,{recursive:true});
    const report={status:'running',kind:'One painted brick, free repaint, identity and exact durable reload; no images',expectedPresentationParts,stages:[]};
    const expression='JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json()))';
    const evaluate=async expression=>{
        const result=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        if(result.exceptionDetails)throw Error(JSON.stringify(result.exceptionDetails));
        return result.result.value;
    };
    const read=()=>evaluate(expression);
    const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
    const wait=async(predicate,label,seconds=30)=>{
        const end=Date.now()+seconds*1000;let state;
        do{state=await read();if(predicate(state))return state;await delay(30);}while(Date.now()<end);
        throw Error(`${label}: ${JSON.stringify(state)}`);
    };
    const mouse=async point=>{
        await call('Input.dispatchMouseEvent',{type:'mouseMoved',...point});
        await call('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',buttons:1,clickCount:1,...point});
        await delay(80);
        await call('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',buttons:0,clickCount:1,...point});
    };
    const click=async id=>{
        for(let depth=0;depth<8;++depth){
            const closed=await evaluate(`(()=>{let parent=document.getElementById(${JSON.stringify(id)})?.parentElement,closed;
                while(parent){if(parent.tagName==='DETAILS'&&!parent.open)closed=parent;parent=parent.parentElement;}
                if(!closed)return null;const summary=closed.querySelector('summary');summary.scrollIntoView({block:'nearest'});
                const r=summary.getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2};})()`);
            if(!closed)break;await mouse(closed);await delay(120);
        }
        const end=Date.now()+15000;
        while(Date.now()<end){
            await evaluate(`document.getElementById(${JSON.stringify(id)})?.scrollIntoView({block:'nearest'})`);
            await delay(180);
            const point=await evaluate(`(()=>{const button=document.getElementById(${JSON.stringify(id)});
                if(!button||button.hidden||button.disabled||!button.getClientRects().length)return null;
                const r=button.getBoundingClientRect(),x=r.x+r.width/2,y=r.y+r.height/2;
                return button.contains(document.elementFromPoint(x,y))?{x,y}:null;})()`);
            if(point){await mouse(point);return;}
        }
        throw Error(id+' must be visible, enabled and unobstructed');
    };
    const record=async(name,extra={})=>{
        const state=await read();assert.equal(state.failed,false);assert.equal(state.terrainSurface,'lego');
        assert.equal(state.assetFixture.presentationParts,expectedPresentationParts,'exact configured presentation parts');
        assert.deepEqual(await evaluate('globalThis.voxyUncapturedGpuErrors||[]'),[]);
        report.stages.push({name,state,...extra});
        await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));return state;
    };
    const frames=before=>wait(state=>BigInt(state.assetFixture.submittedSerial)>=BigInt(before)+2n
        &&!state.workshop.camera.framePending,'workshop frames',60);
    const aim=async point=>{
        const state=await read(),matrix=state.camera.viewProjection;
        assert.equal(matrix?.length,16,'read-only view projection is required');
        const local=[...point.map((v,i)=>v+state.workshop.displayOrigin[i]-state.camera.sector[i]*256),1];
        const clip=[0,1,2,3].map(row=>local.reduce((value,t,column)=>value+t*matrix[column*4+row],0));
        assert(clip[3]>0,'deck target must be in front of the camera');
        const rect=await evaluate('(()=>{const r=voxyModule.canvas.getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height}})()');
        const x=rect.x+(clip[0]/clip[3]+1)*rect.w/2,y=rect.y+(1-clip[1]/clip[3])*rect.h/2;
        assert(await evaluate(`document.elementFromPoint(${x},${y})===voxyModule.canvas`),'deck point must be visible on the actual canvas');
        const before=(await read()).assetFixture.submittedSerial;
        await call('Input.dispatchMouseEvent',{type:'mouseMoved',x,y});await frames(before);return {x,y};
    };
    const blueprint=()=>evaluate("voxyModule.ccall('voxy_salvage_blueprint_action','string',['number','string'],[1,''])");
    const geometry=workshop=>({name:workshop.name,placement:workshop.placement,rotation:workshop.rotation});
    const teal=[35,145,137,255],blue=[50,108,190,255];
    const color=(workshop,index,rgba)=>{
        assert.equal(workshop.canPaint,true);assert.equal(workshop.paintIndex,index);assert.deepEqual(workshop.paint,rgba);
    };
    const soleBrick=async()=>{
        const count=(await read()).workshop.parts;
        assert.equal(count,11);
        for(let n=0;n<count;++n){
            const state=await read();if(state.workshop.name==='Brick 2 x 4')return state;
            const selected=state.workshop.selected;await click('workshop-next');
            await wait(s=>s.workshop.selected!==selected,'Next selects another part');
        }
        throw Error('The single painted brick must be selectable through Next');
    };
    const launched=async label=>{
        const previous=(await read()).workshop.launches;
        await click('workshop-launch');
        // Both refits have eleven parts. Wait for the actual acknowledged
        // launch counter, so the previous boat cannot satisfy this check.
        await wait(s=>s.workshop.launches===previous+1&&!s.workshop.open&&!s.workshop.pending&&s.boat.parts===11,label,60);
        const state=await record(label);
        assert.equal(state.boat.massKg,993);assert.deepEqual(state.boat.paidPartIds,['35']);
        assert.equal(state.session.inventory.salvageMaterial,'43');
        assert.equal(state.session.inventory.specialMachinery,'0');return state;
    };
    try {
        const initial=await wait(s=>s.ready&&s.workshop?.canOpen,'fresh cove ready',60);
        assert.equal(initial.boat.parts,11);assert.deepEqual(initial.boat.paidPartIds,[]);
        assert.equal(initial.session.inventory.salvageMaterial,'48');
        await click('salvage-workshop-toggle');await wait(s=>s.workshop.open,'open workshop');
        for(let n=0;n<11&&(await read()).workshop.name!=='Cargo cradle';++n)await click('workshop-next');
        await wait(s=>s.workshop.name==='Cargo cradle','select cargo cradle');
        await click('workshop-remove');await wait(s=>s.workshop.changed,'cradle removal preview');
        await click('workshop-keep');await wait(s=>s.workshop.parts===10&&!s.workshop.changed,'clear deck');
        await click('workshop-brick-2x4');await wait(s=>s.workshop.brickTool&&s.workshop.canPaint,'2x4 brush');
        await click('workshop-paint-teal');
        await wait(s=>s.workshop.paintIndex===2&&s.workshop.brushPaint?.[0]===35,'Teal chosen through palette');
        color((await read()).workshop,2,teal);
        assert(await evaluate('document.activeElement===voxyModule.canvas'),'brush paint returns keyboard focus to the canvas');
        const before=(await read()).assetFixture.submittedSerial;await click('workshop-frame');await frames(before);
        let pointer;report.pointerTargets=[];
        for(const [dx,dz] of [[0,0],[.5,0],[-.5,0],[0,.5],[0,-.5],[.5,.5],[-.5,.5],[.5,-.5],[-.5,-.5]]){
            const target=await aim([2+dx,.96,-55+dz]),w=(await read()).workshop;
            report.pointerTargets.push({point:[2+dx,.96,-55+dz],valid:w.valid,pointerTarget:w.pointerTarget,placement:w.placement});
            if(w.valid&&w.pointerTarget&&w.placement[1]===72){pointer=target;break;}
        }
        assert(pointer,'Teal brick must fit a visible deck socket');
        const ghost=(await read()).workshop;const expectedGeometry=geometry(ghost);color(ghost,2,teal);
        await mouse(pointer);await wait(s=>s.workshop.brickTool&&s.workshop.placedBricks===1&&s.workshop.placedParts===11,'place exactly one brick');
        let state=await record('placed-teal-brick',{geometry:expectedGeometry});
        color(state.workshop,2,teal);assert.deepEqual(state.workshop.brushPaint,teal);assert.equal(state.workshop.parts,12);
        assert.equal(state.workshop.charge,'5');assert.equal(state.session.inventory.salvageMaterial,'48');
        await click('workshop-brick-1x2');await wait(s=>s.workshop.catalogName==='Brick 1 x 2'&&s.workshop.brickTool,'switch to 1x2 brush');
        state=await record('switched-brush-retains-teal');color(state.workshop,2,teal);assert.deepEqual(state.workshop.brushPaint,teal);
        assert.equal(state.workshop.placedBricks,1);assert.equal(state.workshop.charge,'5');
        await click('workshop-select');await wait(s=>!s.workshop.brickTool&&!s.workshop.changed&&s.workshop.parts===11,'discard unused brush');
        const tealBlueprint=await blueprint();assert(tealBlueprint.startsWith('53564250'));
        const firstLaunch=await launched('launched-one-teal-brick');
        await click('salvage-workshop-toggle');await wait(s=>s.workshop.open,'reopen to repaint');
        state=await soleBrick();color(state.workshop,2,teal);assert.deepEqual(geometry(state.workshop),expectedGeometry);
        await click('workshop-paint-blue');await wait(s=>s.workshop.paintIndex===3&&s.workshop.changed,'Blue repaint preview');
        color((await read()).workshop,3,blue);
        await click('workshop-keep');await wait(s=>!s.workshop.changed&&s.workshop.paintIndex===3,'keep Blue repaint');
        state=await record('kept-free-blue-repaint');assert.equal(state.workshop.charge,'0');assert.equal(state.workshop.refund,'0');
        assert.equal(state.workshop.machineryCharge,'0');assert.deepEqual(geometry(state.workshop),expectedGeometry);
        const blueBlueprint=await blueprint();assert(blueBlueprint.startsWith('53564250'));assert.notEqual(blueBlueprint,tealBlueprint);
        report.blueprint=blueBlueprint;
        const repainted=await launched('launched-free-blue-repaint');
        assert.deepEqual(repainted.boat.paidPartIds,firstLaunch.boat.paidPartIds);
        assert.deepEqual(repainted.session.inventory,firstLaunch.session.inventory);
        await click('salvage-pause');await wait(s=>s.pause.phase==='paused','pause to save');
        await click('salvage-save');
        let durable=false;
        for(let n=0;n<200;++n){
            durable=await evaluate('document.getElementById("salvage-save-status").textContent.startsWith("Saved.")');
            if(durable)break;await delay(100);
        }
        assert(durable,'manual save must acknowledge durable storage');
        const saved=await record('saved-blue-brick');await call('Page.reload',{ignoreCache:true});await delay(400);
        const restored=await wait(s=>s.ready&&s.restore?.phase==='ready'&&s.pause.phase==='paused','restore saved paint',60);
        assert.equal(restored.boat.parts,11);assert.equal(restored.boat.massKg,993);
        assert.deepEqual(restored.boat.paidPartIds,saved.boat.paidPartIds);assert.deepEqual(restored.session.inventory,saved.session.inventory);
        await click('salvage-pause');await wait(s=>s.pause.phase==='running','resume saved expedition');
        await click('salvage-workshop-toggle');await wait(s=>s.workshop.open,'open restored workshop');
        state=await soleBrick();color(state.workshop,3,blue);assert.deepEqual(geometry(state.workshop),expectedGeometry);
        assert.equal(await blueprint(),blueBlueprint,'restored design must match exact painted blueprint');
        await record('restored-exact-blue-paint-geometry-and-ownership');report.status='passed';
    } catch(error) {
        report.status='failed';report.error=String(error);try{report.failureState=await read();}catch{}
        throw error;
    } finally {
        await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));
    }
    return report;
}
