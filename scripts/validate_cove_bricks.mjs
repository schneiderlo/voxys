// Real pointer/keyboard building and durable reload. No screenshots or state setters.
import assert from 'node:assert/strict';
import {mkdir,writeFile,readFile} from 'node:fs/promises';
export async function validateCoveBricks(call,directory) {
    await mkdir(directory,{recursive:true});
    const expression='JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json()))';
    const evaluate=async expression=>{
        const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));
        return r.result.value;
    };
    const read=()=>evaluate(expression);
    const delay=ms=>new Promise(r=>setTimeout(r,ms));
    const wait=async(predicate,label,seconds=20)=>{
        const until=Date.now()+seconds*1000;
        let state;
        do{try{state=await read(); if(predicate(state))return state;}catch{} await delay(30);}while(Date.now()<until);
        throw Error(`${label}: ${JSON.stringify(state)}`);
    };
    const key=(letter,down)=>call('Input.dispatchKeyEvent',{type:down?'keyDown':'keyUp',
        key:letter==='Space'?' ':letter.toLowerCase(),code:letter==='Space'?'Space':`Key${letter}`,
        windowsVirtualKeyCode:letter==='Space'?32:letter.charCodeAt(0),nativeVirtualKeyCode:letter==='Space'?32:letter.charCodeAt(0)});
    const hold=async(keys,ticks)=>{
        const physical=(await read()).player.mode==='helm';
        const tick=s=>BigInt(physical?s.boat.physicsTicks.completed:s.player.tick);
        const first=tick(await read());
        try{
            for(const k of keys)await key(k,true);
            await wait(s=>tick(s)>=first+BigInt(ticks),'movement ticks',physical?120:20);
        }finally{for(const k of keys)await key(k,false);}
    };
    const mouse=async p=>{
        await call('Input.dispatchMouseEvent',{type:'mouseMoved',...p});
        await call('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',buttons:1,clickCount:1,...p});
        await delay(80);
        await call('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',buttons:0,clickCount:1,...p});
    };
    const click=async id=>{
        for(let depth=0;depth<8;++depth){
            const disclosure=await evaluate(`(()=>{let p=document.getElementById(${JSON.stringify(id)})?.parentElement,closed;
                while(p){if(p.tagName==='DETAILS'&&!p.open)closed=p;p=p.parentElement;}
                if(!closed)return null;const e=closed.querySelector('summary');e.scrollIntoView({block:'nearest'});
                const r=e.getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2};})()`);
            if(!disclosure)break;await mouse(disclosure);await delay(120);
        }
        const end=Date.now()+15000;
        while(Date.now()<end) {
            await evaluate(`document.getElementById(${JSON.stringify(id)})?.scrollIntoView({block:'nearest'})`);
            await delay(180);
            const p=await evaluate(`(()=>{const e=document.getElementById(${JSON.stringify(id)});
                if(!e||e.hidden||e.disabled||!e.getClientRects().length)return null;
                const r=e.getBoundingClientRect(),x=r.x+r.width/2,y=r.y+r.height/2;
                return e.contains(document.elementFromPoint(x,y))?{x,y}:null;})()`);
            if(p){await mouse(p);return;}
        }
        throw Error(id+' must be visible, enabled and unobstructed');
    };
    const walkTo=async(x,z)=>{
        for(let n=0;n<100;++n){
            const s=await read(), p=s.player.feet, target=typeof x==='function'?x(s):[x,z];
            const dx=target[0]-p[0],dz=target[1]-p[2],length=Math.hypot(dx,dz);
            // A displayed frame can consume several walking steps per key
            // event. Use the existing delivery driver's waypoint tolerance;
            // boarding and helm still require the actual interaction below.
            if(length<.25){await hold([],20);return;}
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

    const report={status:'running',kind:'Eight actual bricks, pointer placement, rotation, undo, launch and reload; no images',stages:[]};
    const record=async name=>{const state=await read();assert.equal(state.failed,false);assert.equal(state.terrainSurface,'lego');
        assert.deepEqual(await evaluate('globalThis.voxyUncapturedGpuErrors||[]'),[]);
        report.stages.push({name,state});await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));return state;};
    const project=async point=>{
        const s=await read(),m=s.camera.viewProjection;
        assert.equal(m?.length,16,'read-only view projection is required');
        const p=[...point.map((v,i)=>v+s.workshop.displayOrigin[i]-s.camera.sector[i]*256),1];
        const clip=[0,1,2,3].map(r=>p.reduce((v,t,c)=>v+t*m[c*4+r],0));assert(clip[3]>0);
        const rect=await evaluate('(()=>{const r=voxyModule.canvas.getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height}})()');
        const x=rect.x+(clip[0]/clip[3]+1)*rect.w/2,y=rect.y+(1-clip[1]/clip[3])*rect.h/2;
        assert(await evaluate(`document.elementFromPoint(${x},${y})===voxyModule.canvas`),'build point must be visible on the actual canvas');
        return {x,y};
    };
    // A slow GPU can take longer than a fixed delay to apply a camera request
    // or mouse move. Wait for actual application frames before reading a ghost.
    const frames=async before=>wait(s=>BigInt(s.assetFixture.submittedSerial)>=BigInt(before)+2n&&!s.workshop.camera.framePending,'workshop frames',60);
    const aim=async point=>{
        const p=await project(point),before=(await read()).assetFixture.submittedSerial;
        await call('Input.dispatchMouseEvent',{type:'mouseMoved',...p});await frames(before);return p;
    };
    const blueprint=()=>evaluate("voxyModule.ccall('voxy_salvage_blueprint_action','string',['number','string'],[1,''])");
    const sail=async saved=>{
        await walkTo(4.5,-49);await walkTo(4.5,-53);
        await wait(s=>s.player.interaction==='board','boarding reach');await click('salvage-interact');await wait(s=>s.player.onBoat,'board brick boat');
        await walkTo(s=>[s.boat.helmPosition[0],s.boat.helmPosition[2]]);await wait(s=>s.player.interaction==='helm','helm reach');
        await click('salvage-interact');await wait(s=>s.player.mode==='helm','use helm');const before=await read();
        await hold(['W'],90);const sailed=await record('sailed-brick-built-boat');
        assert(Math.hypot(...sailed.boat.position.map((v,i)=>v-before.boat.position[i]))>.3);
        assert.deepEqual(sailed.boat.paidPartIds,saved.boat.paidPartIds);
    };
    try {
        if(process.env.VOXY_SMOKE_COVE_BRICKS_RESUME) {
            const prior=JSON.parse(await readFile(process.env.VOXY_SMOKE_COVE_BRICKS_RESUME,'utf8'));
            report.continues=process.env.VOXY_SMOKE_COVE_BRICKS_RESUME;
            const saved=prior.stages.find(s=>s.name==='saved-eight-bricks').state;
            const restored=await wait(s=>s.ready&&s.restore?.phase==='ready'&&s.pause.phase==='paused','resume saved brick build');
            assert.deepEqual(restored.boat.paidPartIds,saved.boat.paidPartIds);assert.deepEqual(restored.session.inventory,saved.session.inventory);
            assert.equal(restored.boat.massKg,saved.boat.massKg);assert.equal(restored.boat.parts,18);
            await click('salvage-pause');await wait(s=>s.pause.phase==='running','resume button');
            await click('salvage-workshop-toggle');await wait(s=>s.workshop.open,'restored workshop');
            const geometry=w=>JSON.stringify([w.name,w.placement,w.rotation]);
            const expected=prior.stages.filter(s=>s.name.startsWith('placed-brick-')).map(s=>geometry(s.state.workshop));
            const actual=[];
            for(let i=0;i<18;++i){const w=(await read()).workshop;if(w.name.startsWith('Brick '))actual.push(geometry(w));await click('workshop-next');}
            assert.deepEqual(actual.sort(),expected.sort());await record('resumed-exact-brick-layout');
            await click('salvage-workshop-toggle');
            await sail(saved);report.status='passed';return report;
        }
        await wait(s=>s.ready&&s.workshop?.canOpen,'cove ready');await click('salvage-workshop-toggle');
        await wait(s=>s.workshop.open,'workshop open');
        // Another real-control journey can leave a different part selected.
        // Select the named cradle through the UI, without assuming an index.
        assert.equal((await read()).workshop.parts,11,'start with the intact starter');
        for(let attempts=0;attempts<11&&(await read()).workshop.name!=='Cargo cradle';++attempts)
            await click('workshop-next');
        await wait(s=>s.workshop.name==='Cargo cradle','select cargo cradle');
        await click('workshop-remove');await click('workshop-keep');await wait(s=>s.workshop.parts===10&&!s.workshop.changed,'clear the cargo deck');
        let previous,cost=0;const sizes=['2x4','2x2','1x2','2x4','2x2','1x2','2x2','1x2'];
        for(let i=0;i<sizes.length;++i) {
            await click('workshop-brick-'+sizes[i]);await wait(s=>s.workshop.changed&&s.workshop.pointerPlacement,'palette creates ghost');
            cost+=Number((await read()).workshop.partCost);
            if(i===2){await click('workshop-rotate');}
            if(previous){const before=(await read()).assetFixture.submittedSerial;await click('workshop-frame');await frames(before);}
            // Aim through the solid top of the supporting brick. A point at
            // stud-cap height can sit over the gap between studs and miss.
            const point=previous?previous.placement.map((n,k)=>n*.02+(k===1?.42:0)):[2,.96,-55];
            // Aim at actual top studs, not only the gap at the part's centre.
            // Different viewport/panel framing changes which rim the ray hits.
            // A bounded set of real pointer moves must still produce a valid
            // connection at the expected stack height before any click.
            let p;const targets=[];
            for(const [dx,dz] of [[0,0],[.5,0],[-.5,0],[0,.5],[0,-.5],[.5,.5],[-.5,.5],[.5,-.5],[-.5,-.5]]){
                const candidate=[point[0]+dx,point[1],point[2]+dz];
                const pointer=await aim(candidate),w=(await read()).workshop;
                targets.push({point:candidate,valid:w.valid,pointerTarget:w.pointerTarget,placement:w.placement});
                if(w.pointerTarget&&w.valid&&(!previous||w.placement[1]===previous.placement[1]+48)){p=pointer;break;}
            }
            (report.pointerTargets??=[]).push(targets);
            assert(p,'brick '+i+' must fit a visible top stud: '+JSON.stringify(targets));
            const ghost=(await read()).workshop;
            if(previous)assert.equal(ghost.placement[1],previous.placement[1]+48,'engaged stack height');
            if(i===2)assert.notEqual(ghost.rotation,previous.rotation,'upper brick rotated relative to support');
            await mouse(p);await wait(s=>!s.workshop.changed&&s.workshop.parts===11+i,'click keeps brick '+i);
            previous=(await read()).workshop;
            assert.equal((await read()).session.inventory.salvageMaterial,'48','preview spends nothing');
            await record('placed-brick-'+(i+1));
        }
        const design=await blueprint();assert(design.startsWith('53564250'));report.design=design;
        await click('workshop-lower');await wait(s=>s.workshop.changed&&!s.workshop.valid,'overlap is refused');
        assert(await evaluate('document.getElementById("workshop-keep").disabled'));
        await click('workshop-revert');await wait(s=>!s.workshop.changed,'cancel overlap');
        await click('workshop-remove');await click('workshop-keep');await wait(s=>s.workshop.parts===17&&!s.workshop.changed,'remove upper brick');
        await click('workshop-undo');await wait(s=>s.workshop.parts===18,'undo brick removal');assert.equal(await blueprint(),design);
        await record('overlap-refused-and-remove-undone');
        await click('workshop-launch');await wait(s=>!s.workshop.open&&!s.workshop.pending&&s.boat.parts===18,'physical brick launch');
        const launched=await record('launched-eight-bricks');assert.equal(launched.boat.paidPartIds.length,8);
        assert.equal(launched.session.inventory.salvageMaterial,String(48-cost));
        await click('salvage-pause');await wait(s=>s.pause.phase==='paused','pause to save');
        await click('salvage-save');
        const saved=await read();
        for(let i=0;i<200&&!await evaluate('document.getElementById("salvage-save-status").textContent.startsWith("Saved.")');++i)await delay(100);
        assert(await evaluate('document.getElementById("salvage-save-status").textContent.startsWith("Saved.")'));
        await record('saved-eight-bricks');await call('Page.reload',{ignoreCache:true});await delay(400);
        const restored=await wait(s=>s.ready&&s.restore?.phase==='ready'&&s.pause.phase==='paused','restore brick build');
        assert.deepEqual(restored.boat.paidPartIds,saved.boat.paidPartIds);assert.deepEqual(restored.session.inventory,saved.session.inventory);
        assert.equal(restored.boat.massKg,saved.boat.massKg);assert.equal(restored.boat.parts,18);
        await click('salvage-pause');await wait(s=>s.pause.phase==='running','resume');
        await click('salvage-workshop-toggle');await wait(s=>s.workshop.open,'reopen restored design');
        assert.equal(await blueprint(),design);await click('salvage-workshop-toggle');
        await record('restored-exact-design-and-cost');
        await sail(saved);
        report.status='passed';
    }catch(error){report.status='failed';report.error=String(error);try{report.failureState=await read();}catch{}throw error;}
    finally{await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));}
    return report;
}
