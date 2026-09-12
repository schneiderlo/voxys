// One actual-control visual-effects journey. Read-only state/blueprint export;
// no state setters, screenshots, repeated refits or artificial impact events.
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';

export async function validateCoveEffects(call,directory,onReady){
    await mkdir(directory,{recursive:true});
    const started=Date.now(),deadline=started+210000,held=new Set();
    const report={status:'running',kind:'Accepted movement effects and authored environment through real controls; no images',
        maximumSeconds:210,stages:[],limits:'This checks live effect encoding and ownership. Numeric renderer tests separately prove pixels/depth. CPU water-entry gates use the bounded cosmetic wave approximation. Dust requires a naturally paired contact; no collision is injected.'};
    const persist=()=>writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));
    const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
    const evaluate=async expression=>{
        const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));return r.result.value;
    };
    const read=()=>evaluate('JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json()))');
    const wait=async(predicate,label,seconds=20)=>{
        const end=Math.min(deadline,Date.now()+seconds*1000);let state;
        while(Date.now()<end){state=await read();assert(!state.failed,'game failed');if(predicate(state))return state;await delay(25);}
        throw Error(`${label}: ${JSON.stringify(state)}`);
    };
    const focus=()=>evaluate('voxyModule.canvas.focus({preventScroll:true})');
    const key=async(name,down)=>{
        const [value,code,number]=name==='Space'?[' ','Space',32]:name==='F9'?['F9','F9',120]
            :[name.toLowerCase(),`Key${name}`,name.charCodeAt(0)];
        await call('Input.dispatchKeyEvent',{type:down?'keyDown':'keyUp',key:value,code,
            windowsVirtualKeyCode:number,nativeVirtualKeyCode:number});
        if(down)held.add(name);else held.delete(name);
    };
    const press=async name=>{await focus();try{await key(name,true);await delay(110);}finally{await key(name,false);}};
    const uncap=async()=>{
        if(!await evaluate('voxyModule._voxy_get_uncapped_fps()'))await press('F9');
        const end=Math.min(deadline,Date.now()+10000);
        while(Date.now()<end){if(await evaluate('voxyModule._voxy_get_uncapped_fps()'))return;await delay(25);}
        throw Error('Existing F9 uncapped control was not accepted');
    };
    const canonical=s=>({world:s.world,build:s.boat.buildId,topology:s.boat.topologyRevision,
        rootKeys:s.boat.roots.map(r=>r.key),parts:s.boat.parts,massKg:s.boat.massKg,paidPartIds:s.boat.paidPartIds,
        inventory:s.session.inventory,buildParts:s.session.buildParts,buildConnections:s.session.buildConnections,
        cargo:s.session.cargo,jobs:s.session.jobs,jobPhase:s.job?.phase});
    const owner=s=>({world:s.world,incarnation:s.boat.physicsTicks.incarnation,generation:s.assetFixture.generation,
        build:s.boat.buildId,topology:s.boat.topologyRevision,roots:s.boat.roots.map(r=>r.key),
        bodyIndex:s.boat.mechanisms.bodyIndex,bodyGeneration:s.boat.mechanisms.bodyGeneration});
    let baseline;
    const invariant=s=>{
        assert(s.ready&&!s.failed&&s.boat.active);assert.equal(s.terrainSurface,'lego');
        assert.equal(s.boat.physicsTicks.failed,false);assert.equal(s.assetFixture.presentationParts,9);
        assert(s.boat.environmentCollision,'admitted environment collision');assert.equal(s.boat.environmentProxies,39);
        assert.equal(s.assetFixture.sceneryGpuBytes,1241888,'all authored scenery is resident');
        assert(s.assetFixture.environmentReady&&BigInt(s.assetFixture.environmentGpuBytes)>0n,'owned baked environment');
        assert(BigInt(s.assetFixture.gpuReservationBytes)<=16n*1024n*1024n);
        assert.equal(s.assetFixture.externalGpuReserve,33328);assert.equal(s.boat.effects.ownerBytes,33328);
        assert(s.boat.effects.active>=0&&s.boat.effects.active<=512&&s.boat.effects.highWater<=512);
        assert.equal(s.boat.effects.rejected,'0','no refused visual packet');
        assert.equal(s.boat.effects.epoch,s.boat.physicsTicks.incarnation);
        assert(BigInt(s.boat.effects.tick)<=BigInt(s.boat.physicsTicks.completed));
        assert(s.boat.mechanisms.bodyIndex>0&&s.boat.mechanisms.bodyGeneration>0);
        if(baseline)assert.deepEqual(canonical(s),baseline,'cosmetics and movement preserve canonical ownership');
    };
    const rendered=s=>s.workshop.open?s.assetFixture.sceneryDraws===0
        :s.characterCamera.valid&&s.characterCamera.geometryTick===s.characterCamera.presentedTick
            &&s.boat.effects.tick===s.characterCamera.presentedTick
            &&s.assetFixture.sceneryDraws>=19&&s.assetFixture.sceneryDraws<=20;
    const capture=async(name,state=undefined,extra={})=>{
        state??=await read();invariant(state);const requested=state;
        if(!rendered(state))state=await wait(s=>{
            invariant(s);assert.deepEqual(owner(s),owner(requested));
            return s.workshop.open===requested.workshop.open&&s.pause.phase===requested.pause.phase&&rendered(s);
        },name+' matched current effects and scenery presentation');
        const owned=owner(state),submitted=BigInt(state.assetFixture.submittedSerial);
        // Encoded effect counts are per-render observations, not atomically
        // tagged submission facts. Require a later same-owner submission and
        // its completion, retaining both proof points without claiming more.
        const later=await wait(s=>{
            invariant(s);assert.deepEqual(owner(s),owned);return rendered(s)&&s.workshop.open===state.workshop.open
                &&s.pause.phase===state.pause.phase&&BigInt(s.assetFixture.submittedSerial)>submitted;
        },name+' later same-mode submission');
        const frontier={serial:later.assetFixture.submittedSerial,tick:later.boat.effects.tick};
        const done=await wait(s=>{
            invariant(s);assert.deepEqual(owner(s),owned);return rendered(s)&&s.workshop.open===state.workshop.open
                &&s.pause.phase===state.pause.phase&&BigInt(s.assetFixture.completedSerial)>=BigInt(frontier.serial)
                &&BigInt(s.boat.physicsTicks.completed)>=BigInt(frontier.tick);
        },name+' actual GPU and physics completion');
        assert.deepEqual(await evaluate('globalThis.voxyUncapturedGpuErrors||[]'),[]);
        report.stages.push({name,state,...(requested===state?{}:{requestedState:requested}),submitted:{state:later,...frontier},completion:{serial:done.assetFixture.completedSerial,
            tick:done.boat.physicsTicks.completed,owner:owned},...extra});await persist();return state;
    };
    const fresh=async()=>{const tick=BigInt((await read()).player.tick);return wait(s=>BigInt(s.player.tick)>tick,'released input accepted');};
    const hold=async(names,ticks,predicate=()=>true)=>{
        await focus();const first=BigInt((await read()).player.tick);let result;
        try{for(const n of names)await key(n,true);result=await wait(s=>BigInt(s.player.tick)>=first+BigInt(ticks)&&predicate(s),'accepted movement');}
        finally{for(const n of names)await key(n,false);}
        await fresh();return result;
    };
    const direction=(s,dx,dz)=>{
        const length=Math.hypot(dx,dz);if(length<1e-9)return [];
        const yaw=s.characterCamera.yaw,forward=(-dx*Math.sin(yaw)-dz*Math.cos(yaw))/length,
            right=(dx*Math.cos(yaw)-dz*Math.sin(yaw))/length,keys=[];
        if(Math.abs(forward)>.4)keys.push(forward>0?'W':'S');if(Math.abs(right)>.4)keys.push(right>0?'D':'A');return keys;
    };
    const walk=async(target,reached=()=>false,seconds=30)=>{
        const end=Math.min(deadline,Date.now()+seconds*1000),trace=[];
        while(Date.now()<end){
            const s=await read();if(reached(s))return s;const p=s.player.feet,t=typeof target==='function'?target(s):target;
            const dx=t[0]-p[0],dz=t[1]-p[2],length=Math.hypot(dx,dz);if(length<.28)return s;
            const keys=direction(s,dx,dz);trace.push({feet:p,target:t,keys,tick:s.player.tick});
            await hold(keys,Math.max(1,Math.min(5,Math.floor(length/.06)-1)));
        }
        report.failedApproach=trace;throw Error('Real movement route did not reach its interaction');
    };
    const rotate=(s,v)=>{
        const [x,y,z,w]=s.boat.orientation,t=[2*(y*v[2]-z*v[1]),2*(z*v[0]-x*v[2]),2*(x*v[1]-y*v[0])];
        const cross=[y*t[2]-z*t[1],z*t[0]-x*t[2],x*t[1]-y*t[0]];return v.map((n,i)=>n+w*t[i]+cross[i]);
    };
    const boarding=s=>{const p=rotate(s,[2.1,0,.1]),h=s.boat.helmPosition;return[h[0]+p[0],h[2]+p[2]];};
    const blueprint=()=>evaluate("voxyModule.ccall('voxy_salvage_blueprint_action','string',['number','string'],[1,''])");
    try{
        await wait(s=>s.characterCamera?.valid&&s.boat?.effects?.ownerBytes===33328&&s.boat.environmentCollision
            &&s.assetFixture.environmentReady&&s.workshop.canOpen,'fresh authored Cove',50);
        await uncap();
        if(onReady)await onReady(); // Optional observer starts after the real F9 control.
        const initial=await read();assert.equal(initial.pause.phase,'running');
        assert.equal(initial.boat.parts,11);assert.equal(initial.boat.massKg,1035);
        assert.deepEqual(initial.boat.paidPartIds,[]);assert.deepEqual(initial.session.inventory,{salvageMaterial:'48',specialMachinery:'0'});
        baseline=canonical(initial);await capture('authored-cove-and-original-ownership',initial);
        await press('B');await wait(s=>s.workshop.open,'open original blueprint');
        const originalBlueprint=await blueprint();assert(originalBlueprint.startsWith('53564250'));
        report.originalBlueprint=originalBlueprint;await press('B');await wait(s=>!s.workshop.open,'close workshop');
        const near=s=>s.player.interaction==='board';
        await walk([6,-49.5],near);await walk([4.5,-49.5],near);await walk([4.5,-53],near);
        await wait(near,'real board prompt');await press('E');await wait(s=>s.player.onBoat,'board');
        await capture('walked-authored-dock-and-boarded');
        await walk(s=>[s.boat.helmPosition[0],s.boat.helmPosition[2]],s=>s.player.interaction==='helm');
        await wait(s=>s.player.interaction==='helm','real helm prompt');await press('E');await wait(s=>s.player.mode==='helm','use helm');
        const before=await read(),wake=BigInt(before.boat.effects.wake),foam=BigInt(before.boat.effects.foam);
        const horizontalTravel=s=>Math.hypot(s.boat.position[0]-before.boat.position[0],
            s.boat.position[2]-before.boat.position[2]);
        await focus();await key('W',true);
        try{
            const moving=await wait(s=>s.boat.speed>.4&&horizontalTravel(s)>.35&&s.boat.mechanisms.effectiveDrive>0
                &&BigInt(s.boat.effects.wake)>wake&&BigInt(s.boat.effects.foam)>foam&&s.boat.effects.encoded>0,
            'real travelled wake and wet driven propeller foam',25);
            assert(horizontalTravel(moving)>.35,'actual horizontal travel excludes wave-driven vertical motion');
            await capture('actual-drive-emits-wake-and-foam',moving,{before,horizontalTravelMetres:horizontalTravel(moving)});
        }finally{await key('W',false);}
        await wait(s=>s.boat.mechanisms.effectiveDrive===0,'released throttle');
        await wait(s=>s.pause.canPause,'safe pause');await press('P');await wait(s=>s.pause.phase==='paused','joined pause');
        const frozen=await capture('paused-effects-start');
        const after=await wait(s=>BigInt(s.assetFixture.submittedSerial)>=BigInt(frozen.assetFixture.submittedSerial)+5n,'fresh paused frames');
        for(const k of ['tick','active','emitted','dropped','wake','foam','splash','playerEntrySplashes','runoff','dust'])
            assert.equal(after.boat.effects[k],frozen.boat.effects[k],`paused ${k}`);
        assert.equal(after.pause.waterTick,frozen.pause.waterTick);await capture('paused-effects-remain-frozen',after);
        await press('P');await wait(s=>s.pause.phase==='running','resume');
        await press('E');await wait(s=>s.player.onBoat&&s.player.mode==='walking','leave helm');await walk(boarding,()=>false,20);
        const splash=BigInt((await read()).boat.effects.playerEntrySplashes);
        await press('Space');await wait(s=>s.player.mode==='airborne'&&!s.player.onBoat,'jump from deck');
        const out=rotate(await read(),[1,0,0]),names=direction(await read(),out[0],out[2]);
        try{for(const n of names)await key(n,true);await wait(s=>s.player.mode==='swimming','actual water entry',15);}
        finally{for(const n of names)await key(n,false);}
        const swimming=await wait(s=>s.player.mode==='swimming'&&rendered(s)
            &&s.boat.effects.encoded>0&&BigInt(s.boat.effects.playerEntrySplashes)>splash,
            'accepted player-attributed water-entry splash with live effect encoding');
        await capture('robot-water-entry-emits-splash',swimming);
        const rescues=BigInt(swimming.rescue.completed);await press('R');
        const rescued=await wait(s=>BigInt(s.rescue.completed)>rescues&&!s.rescue.pending&&s.pause.phase==='paused',
            'real Rescue and automatic durable acknowledgment',45);
        assert.equal(rescued.boat.effects.active,0);assert.equal(rescued.boat.effects.emitted,'0');
        await capture('rescue-clears-ephemeral-effects-and-preserves-owned-build',rescued);
        await press('P');await wait(s=>s.pause.phase==='running'&&s.workshop.canOpen,'resumed dock workshop');
        await press('B');await wait(s=>s.workshop.open,'reopen exact owned blueprint');
        const finalBlueprint=await blueprint();assert.equal(finalBlueprint,originalBlueprint,'all part/connection/settings bytes unchanged');
        await capture('identical-blueprint-after-movement-water-and-rescue',undefined,{blueprint:finalBlueprint});
        await press('B');await wait(s=>!s.workshop.open,'close workshop');
        report.status='passed';
    }catch(error){report.status='failed';report.error=String(error);try{report.failureState=await read();
        report.gpuErrors=await evaluate('globalThis.voxyUncapturedGpuErrors||[]');}catch{}}
    finally{for(const name of [...held])try{await key(name,false);}catch{}
        report.seconds=(Date.now()-started)/1000;await persist();}
    assert.equal(report.status,'passed',report.error);return report;
}
