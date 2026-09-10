// Real construction, hook/lift/harbor delivery, automatic save and page reload.
// Inputs use the shipped controls. State reads only; no success injection/images.
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';
export async function validateCoveDelivery(call,directory,afterDelivery=null,resumeHarbor=false){
    await mkdir(directory,{recursive:true});
    const evaluate=async expression=>{
        const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));return r.result.value;
    };
    const read=()=>evaluate('JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json()))');
    const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
    const wait=async(predicate,label,reload=false,timeoutMs=20000)=>{
        const end=Date.now()+(reload?60000:timeoutMs);let state,error;
        while(Date.now()<end){
            try{state=await read();error=null;if(predicate(state))return state;}
            catch(e){if(!reload)throw e;error=String(e);}await delay(50);
        }
        throw Error(label+': '+(error||JSON.stringify(state)));
    };
    const click=async id=>{
        await evaluate(`document.getElementById(${JSON.stringify(id)})?.scrollIntoView({block:'nearest',behavior:'instant'})`);
        let point;const end=Date.now()+5000;
        const locate=()=>evaluate(`(()=>{const e=document.getElementById(${JSON.stringify(id)});
                if(!e||e.hidden||e.disabled||!e.getClientRects().length)return null;
                const r=e.getBoundingClientRect(),x=r.x+r.width/2,y=r.y+r.height/2;
                return e.contains(document.elementFromPoint(x,y))?{x,y}:null;})()`);
        do{
            const candidate=await locate();
            if(candidate){
                await call('Input.dispatchMouseEvent',{type:'mouseMoved',...candidate});
                await delay(160);
                const settled=await locate();
                if(settled&&Math.hypot(settled.x-candidate.x,settled.y-candidate.y)<.5)point=settled;
            }else await delay(60);
        }while(!point&&Date.now()<end);
        assert(point,id+' must be visible and enabled');
        await call('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',clickCount:1,...point});
        await call('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',clickCount:1,...point});
    };
    const key=(letter,down)=>call('Input.dispatchKeyEvent',{type:down?'keyDown':'keyUp',key:letter.toLowerCase(),code:`Key${letter}`,
        windowsVirtualKeyCode:letter.charCodeAt(0),nativeVirtualKeyCode:letter.charCodeAt(0)});
    const hold=async(keys,ticks)=>{
        const state=await read();
        // Walking has its own fixed steps. Helm/reel inputs act on completed
        // physics ticks, which can advance more slowly on a displayed browser.
        // Counting player steps here released Q before the rope actually reeled.
        const tick=s=>{
            assert(s.player&&s.boat&&!s.failed,'cove stopped during input: '+JSON.stringify(s));
            return BigInt(state.player.mode==='helm'?s.boat.observedTick:s.player.tick);
        };
        const first=tick(state);
        try{
            for(const k of keys)await key(k,true);
            await wait(s=>tick(s)>=first+BigInt(ticks),'input ticks',false,
                state.player.mode==='helm'?Math.max(20000,10000+ticks*2000):20000);
        }
        finally{for(const k of keys)await key(k,false);}
    };
    const walkTo=async target=>{
        for(let i=0;i<100;++i){
            const s=await read(),p=s.player.feet,t=typeof target==='function'?target(s):target;
            const dx=t[0]-p[0],dz=t[1]-p[2],distance=Math.hypot(dx,dz);
            // A slow displayed frame can consume several walking ticks per
            // real key press. A 14 cm waypoint made the driver oscillate around
            // a reachable dock point. Boarding/helm still require the actual
            // interaction below; this radius only ends waypoint steering.
            if(distance<.25){await hold([],20);return;}
            const yaw=s.camera.yaw,forward=(dx*Math.sin(yaw)+dz*Math.cos(yaw))/distance,right=(dx*Math.cos(yaw)-dz*Math.sin(yaw))/distance;
            const keys=[];if(Math.abs(forward)>.4)keys.push(forward>0?'W':'S');if(Math.abs(right)>.4)keys.push(right>0?'D':'A');
            await hold(keys,Math.max(1,Math.min(5,Math.floor(distance/.06)-1)));
        }throw Error('Could not reach boat interaction: '+JSON.stringify({
            target:typeof target==='function'?target(await read()):target,state:await read()}));
    };
    const report={status:'running',kind:'real paid construction, physical cargo recovery, durable delivery and restart',stages:[]};
    const record=async name=>{
        const state=await read();assert(!state.failed);assert.equal(state.terrainSurface,'lego');
        assert.deepEqual(await evaluate('globalThis.voxyUncapturedGpuErrors||[]'),[]);
        const roots=state.boat.roots,joined=BigInt(state.boat.joinedTick);
        assert.equal(state.boat.rootCount,roots.length);assert(roots.length>0&&roots.length<=32);
        assert.equal(new Set(roots.map(root=>root.key)).size,roots.length,'physical section keys must be unique');
        if(joined>0n)assert(roots.every(root=>root.active&&BigInt(root.observedTick)===joined),'all section observations must join');
        if(state.pause.phase==='paused'){
            assert(joined>0n);assert.equal(joined,BigInt(state.boat.observedTick));
        }
        const previous=report.stages.at(-1)?.state;
        if(state.pause.waterTick!==undefined&&previous?.pause.waterTick!==undefined
            &&state.pause.phase==='running'&&previous.pause.phase==='running'){
            const ticks=BigInt(state.pause.waterTick)-BigInt(previous.pause.waterTick);
            assert(ticks>=0n,'wave tick cannot go backward within a live owner');
            const expected=Number(ticks)/60,actual=state.pause.waterTime-previous.pause.waterTime;
            assert(Math.abs(actual-expected)<.002,`waves/physics drift: ${actual}s for ${ticks} ticks`);
        }
        report.stages.push({name,state});await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));return state;
    };
    try{
        if(resumeHarbor){
            assert(afterDelivery,'A focused resume requires its harbor continuation');
            report.kind='actual saved harbor resume through shipped controls';
            await wait(s=>s.ready&&s.restore?.phase==='ready'&&s.pause.phase==='paused','saved harbor ready',true);
            const delivered=await record('actual-harbor-checkpoint-restored');
            await afterDelivery({call,evaluate,read,wait,click,hold,walkTo,record,delivered});
            report.status='passed';return report;
        }
        await wait(s=>s.ready&&s.boat?.active&&s.pause.canPause,'fresh cove');await record('fresh');
        await click('salvage-workshop-toggle');await wait(s=>s.workshop.open,'workshop');
        const select=async name=>{
            for(let i=0;i<18&&(await read()).workshop.name!==name;++i)await click('workshop-next');
            assert.equal((await read()).workshop.name,name);
        };
        const keep=async()=>{await wait(s=>s.workshop.valid&&s.workshop.changed,'connected design');await click('workshop-keep');};
        const move=async target=>{
            const steps=[50,16,50],buttons=[['workshop-left','workshop-right'],['workshop-lower','workshop-raise'],['workshop-forward','workshop-back']];
            assert.equal((await read()).workshop.rotation,0,'upright part');
            for(let axis=0;axis<3;++axis){
                const start=(await read()).workshop.placement[axis],count=(target[axis]-start)/steps[axis];
                assert(Number.isInteger(count)&&Math.abs(count)<40,'bounded lattice move');
                for(let i=0;i<Math.abs(count);++i)await click(buttons[axis][count>0?1:0]);
            }
            assert.deepEqual((await read()).workshop.placement,target);
        };
        // Create lifting clearance with real paid construction. The original
        // over-pontoon tow point draws the generator into the hull.
        await select('Cargo cradle');await click('workshop-remove');await keep();
        await select('Winch');await move([75,96,-2750]);await keep();
        await click('workshop-catalog-next');await wait(s=>s.workshop.catalogName==='Beam','beam drawer');
        await click('workshop-add');await move([-50,56,-2750]);await keep();
        await select('Winch');await move([-125,112,-2750]);await keep();
        assert.equal((await read()).workshop.charge,'12');await record('lifting-rig-design');
        await click('workshop-launch');await wait(s=>s.boat.parts===11&&!s.workshop.pending&&!s.workshop.open&&s.pause.canPause,'launch lifting rig');
        assert.equal((await read()).session.inventory.salvageMaterial,'36');await record('expanded-craft');
        await click('salvage-job-accept');await wait(s=>s.job.phase==='accepted'&&!s.job.pending,'accept generator job');
        await walkTo([4.5,-49]);await walkTo([4.5,-53]);await wait(s=>s.player.interaction==='board','boarding position');
        await click('salvage-interact');await wait(s=>s.player.onBoat,'board');
        await walkTo(s=>[s.boat.helmPosition[0],s.boat.helmPosition[2]]);await wait(s=>s.player.interaction==='helm','helm position');
        await click('salvage-interact');await wait(s=>s.player.mode==='helm','helm');
        await hold(['F'],2);await wait(s=>s.tow.attached&&s.tow.confirmed,'hook generator');await record('hooked');
        for(let i=0;i<32;++i){
            // Same actual-control strategy as the native journey: bring the
            // load alongside, tow home partly submerged, then hoist in harbor.
            // Height alone is not a safe reel limit on a rolling craft.
            if((await read()).tow.ropeLength<=4)break;
            await hold(['Q'],5);await hold([],10);const lifted=await record('shorten-tow-'+i);
            assert(!lifted.tow.broken,'lifting cable broke; stop before further reel attempts');
        }
        assert((await read()).tow.ropeLength<=4,'could not shorten towing line');
        await record('load-alongside');
        // Reverse the loaded craft toward the dock; hauling is not complete
        // just because the cable lifted the generator outside the harbor zone.
        for(let i=0;i<48&&(await read()).job.harborDistance>3.1;++i){
            const s=await read(),p=s.tow.position,q=s.boat.orientation;
            const desired=Math.atan2(.5-p[0],-54-p[2]);
            const heading=Math.atan2(2*(q[0]*q[2]+q[1]*q[3]),1-2*(q[0]*q[0]+q[1]*q[1]));
            const error=Math.atan2(Math.sin(desired-heading),Math.cos(desired-heading));
            const keys=['S'];if(Math.abs(error)>.12)keys.push(error>0?'D':'A');
            await hold(keys,15);await hold([],5);
            if(i%3===0)await record('loaded-return-'+i);
        }
        assert((await read()).job.harborDistance<=3.1,'loaded return did not reach harbor');
        await record('loaded-return-complete');
        for(let i=0;i<16;++i){
            const state=await read();if(state.job.canDeliver)break;
            assert(state.tow.ropeLength>2.5,'stop before pulling cargo into the beam');
            await hold(['Q'],5);await hold([],15);
            const lifted=await record('harbor-hoist-'+i);
            assert(!lifted.tow.broken,'lifting cable broke in harbor');
        }
        await wait(s=>s.job.canDeliver,'cargo lifted into harbor and slow enough');
        const before=await record('eligible-delivery');assert.equal(before.job.phase,'accepted');
        await click('salvage-job-deliver');
        await wait(s=>s.job.phase==='completed'&&s.job.durable&&!s.job.savePending&&s.pause.phase==='paused','durable delivery');
        const delivered=await record('delivery-saved');
        assert.equal(delivered.session.inventory.salvageMaterial,'96');assert.equal(delivered.session.cargo,0);
        assert(delivered.job.secured);assert(!delivered.tow.attached);
        assert.equal(await evaluate('new URLSearchParams(location.search).get("world")'),delivered.world);
        // Storage acknowledges through the application before the next UI
        // refresh. Wait for the player-facing receipt, without advancing or
        // modifying game state, before testing the actual reload.
        let receipt='';const receiptDeadline=Date.now()+5000;
        do{
            receipt=await evaluate('document.getElementById("salvage-job-status").textContent');
            if(/delivered and saved/.test(receipt))break;
            await delay(50);
        }while(Date.now()<receiptDeadline);
        assert.match(receipt,/delivered and saved/);
        await call('Page.reload',{ignoreCache:true});await delay(200);
        await wait(s=>s.ready&&s.restore?.phase==='ready'&&s.pause.phase==='paused','reload saved delivery',true);
        const restored=await record('delivery-reloaded');
        assert(restored.job.durable&&restored.job.secured);assert.equal(restored.job.phase,'completed');
        assert.deepEqual(restored.session.inventory,delivered.session.inventory);assert.equal(restored.session.cargo,0);
        assert.deepEqual(restored.boat.paidPartIds,delivered.boat.paidPartIds);assert.equal(restored.boat.massKg,1035);
        assert.equal(BigInt(restored.pause.tick),BigInt(delivered.pause.tick)+1n);
        assert.equal(restored.pause.waterTime,delivered.pause.waterTime,'reload settling preserves wave phase');
        await click('salvage-pause');await wait(s=>s.pause.phase==='running','resume delivered expedition');
        await hold(['H'],3);await hold([],20);const repeated=await record('repeat-delivery-refused');
        assert.deepEqual(repeated.session.inventory,delivered.session.inventory);assert.equal(repeated.session.cargo,0);
        await hold(['W'],90);const sailed=await record('sail-away-from-secured-load');
        assert(Math.hypot(...sailed.tow.position.map((v,i)=>v-restored.tow.position[i]))<.001,'banked cargo stays static');
        assert(sailed.boat.speed>.2);assert.equal(sailed.session.inventory.salvageMaterial,'96');
        if(afterDelivery)await afterDelivery({call,evaluate,read,wait,click,hold,walkTo,record,delivered});
        report.status='passed';
    }catch(error){report.status='failed';report.error=String(error);throw error;}
    finally{await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));}
    return report;
}
