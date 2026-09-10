// Exercise shipped input and UI. No screenshots, camera setters or state injection.
import assert from 'node:assert/strict';
import {mkdir, writeFile} from 'node:fs/promises';

export async function validateCovePlayer(call, directory) {
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
    const click=async id=>{
        const p=await evaluate(`(()=>{const e=document.getElementById(${JSON.stringify(id)});
            if(!e||e.hidden||e.disabled)return null; const r=e.getBoundingClientRect();
            return {x:r.x+r.width/2,y:r.y+r.height/2};})()`);
        assert(p,`${id} must be enabled and visible`);
        await call('Input.dispatchMouseEvent',{type:'mouseMoved',...p});
        await call('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',clickCount:1,...p});
        await call('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',clickCount:1,...p});
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
    const report={status:'running',kind:'Real keyboard/button towing and sailing; GPU boat and cargo; no images',stages:[]};
    const record=async name=>{
        const state=await read();
        assert.equal(state.failed,false);
        assert.equal(state.terrainSurface,'lego','playable cove must retain the brick terrain');
        const ticks=state.boat?.physicsTicks;
        if(ticks?.supported) {
            assert.equal(ticks.failed,false,'physics completion proof must remain valid');
            const [completed,submitted,encoded,scheduled]=['completed','submitted','encoded','scheduled'].map(k=>BigInt(ticks[k]));
            assert(completed<=submitted && submitted<=encoded && encoded<=scheduled,'ordered physics frontier');
            assert(scheduled-completed<=BigInt(ticks.maximumInFlight),'bounded queued physics ticks');
            assert(BigInt(state.boat.eventsThrough)<=completed,'events require completed ticks');
            assert(BigInt(state.boat.observedTick)<=completed,'boat observation requires completed tick');
            if(state.tow) {
                assert(BigInt(state.tow.observedTick)<=completed,'cargo observation requires completed tick');
                assert(BigInt(state.tow.ropeObservedTick)<=completed,'rope observation requires completed tick');
                if(state.tow.attached && state.tow.confirmed) {
                    assert(Number.isFinite(state.tow.ropeLength) && state.tow.ropeLength>=.5
                        && state.tow.ropeLength<=40,'confirmed rope must report its actual bounded solver target');
                }
            }
        }
        const errors=await evaluate('globalThis.voxyUncapturedGpuErrors||[]');
        assert.deepEqual(errors,[]);
        assert.equal(state.session.builds,1);assert.equal(state.session.buildParts,11);assert.equal(state.session.buildConnections,17);
        assert.equal(state.boat.buildId,'6');assert.equal(state.boat.topologyRevision,'0');
        assert.equal(state.session.cargo,state.job?.phase==='completed'||!state.job?0:1);
        assert.deepEqual(state.session.inventory,{salvageMaterial:state.job?.phase==='completed'?'108':'48',specialMachinery:'0'});
        report.stages.push({name,state}); return state;
    };
    try{
        await wait(s=>s.ready&&s.player?.mode==='walking'&&s.boat?.active&&s.boat.observedTick>0,'physical cove startup');
        const initial=await record('dock-spawn');
        assert(initial.player.collisionBoxes>24);
        assert.equal(initial.boat.sceneryCollision,true,'physical dock must be admitted before the skiff');
        assert.equal(initial.boat.physicsTicks.supported,true);
        assert(BigInt(initial.boat.physicsTicks.completed)>0n,'real GPU completion must be observed');
        assert(initial.boat.sceneryProxies>0);
        assert.equal(initial.player.interaction,'none');
        if(process.env.VOXY_SMOKE_COVE_JOB==='1') {
            await hold(['J'],2);
            await wait(s=>s.job?.phase==='accepted'&&!s.job.pending,'accept recovery job through J');
            await record('recovery-job-accepted');
            await hold(['H'],2);await hold([],3);
            const refused=await record('distant-delivery-refused');
            assert.equal(refused.job.phase,'accepted');assert.equal(refused.session.cargo,1);
        }
        await walkTo(4.5,-49);
        await walkTo(4.5,-53);
        const beside=await record('beside-boat');
        if(process.env.VOXY_SMOKE_COVE_TERRAIN==='1') {
            assert(Math.hypot(beside.tow.position[0]+4.5,beside.tow.position[2]+58)<.5,
                'unhooked generator must stay at its authored brick-seabed placement');
        }
        assert.equal(beside.player.interaction,'board');
        await hold(['E'],2);
        await wait(s=>s.player.onBoat,'board using E');
        await record('boarded');
        await walkTo(s=>[s.boat.helmPosition[0],s.boat.helmPosition[2]]);
        await wait(s=>s.player.interaction==='helm','helm nearby');
        await delay(120);
        await click('salvage-interact');
        await wait(s=>s.player.mode==='helm','helm button');
        const helm=await record('at-helm');
        assert(helm.tow?.active && helm.tow.inRange,'separate salvage load must be reachable');
        assert.equal(helm.tow.massKg,420);assert.equal(helm.tow.attached,false);
        await hold(['F'],2);
        await wait(s=>s.tow.attached&&s.tow.confirmed,'F attaches actual tow eye');
        const attached=await record('salvage-hooked');
        await hold(['Q'],60);
        await wait(s=>s.tow.motor===0&&s.tow.confirmed,'Q release stops reel motor');
        const reeled=await record('salvage-reeled');
        assert.equal(reeled.tow.broken,false,'normal winching must keep rope intact');
        assert(reeled.tow.distance<attached.tow.distance-.25,'reeling must shorten actual separation');
        assert(reeled.tow.ropeLength<attached.tow.ropeLength-.25,'reeling must shorten actual cable target');
        if(process.env.VOXY_SMOKE_COVE_PAUSE==='1') {
            await key('Q',true);
            await wait(s=>s.tow.motor>0&&s.tow.confirmed&&s.pause.canPause,'live reel ready for pause');
            await click('salvage-pause');
            await wait(s=>s.pause.phase==='paused','pause joins complete simulation evidence');
            const frozen=await record('paused-during-live-reel');
            if(process.env.VOXY_SMOKE_COVE_ARCHIVE==='1') {
                const {validateCoveArchive}=await import('./validate_cove_archive.mjs');
                report.archive=await validateCoveArchive(call,directory,'live-tow-archive');
            }
            const tick=frozen.pause.tick;
            for(const name of ['scheduled','encoded','submitted','completed'])assert.equal(frozen.boat.physicsTicks[name],tick);
            assert.equal(String(frozen.boat.observedTick),tick);assert.equal(String(frozen.tow.observedTick),tick);
            assert.equal(frozen.tow.ropeObservedTick,tick);assert.equal(String(frozen.boat.eventsThrough),tick);
            assert.equal(frozen.session.tick,tick);assert.equal(frozen.tow.motor,0);
            const stable=s=>({player:s.player,session:s.session,pause:s.pause,
                boat:{position:s.boat.position,orientation:s.boat.orientation,speed:s.boat.speed,ticks:s.boat.physicsTicks},
                tow:{position:s.tow.position,speed:s.tow.speed,ropeLength:s.tow.ropeLength,motor:s.tow.motor,attached:s.tow.attached}});
            // Real input while paused must not move, edit, reset, bank or advance time.
            const attempted=['W','F','R','E','H','B','Space'];
            try {for(const k of attempted)await key(k,true);await delay(800);}
            finally {for(const k of [...attempted,'Q'])await key(k,false);}
            const held=await record('paused-input-does-not-change-world');
            assert.deepEqual(stable(held),stable(frozen));
            await key('P',true);await key('P',false);
            await wait(s=>s.pause.phase==='running'&&BigInt(s.boat.physicsTicks.completed)>BigInt(tick),'P resumes simulation');
            const resumed=await record('resumed-with-neutral-controls');
            assert.equal(resumed.player.mode,'helm');assert.equal(resumed.player.onBoat,true);
            assert.equal(resumed.tow.motor,0);assert.equal(resumed.tow.attached,true);
            assert(Math.abs(resumed.tow.ropeLength-frozen.tow.ropeLength)<.001,'resume must not replay the held reel input');
        }
        if(process.env.VOXY_SMOKE_COVE_JOB==='1') {
            // Real winch input lifts the generator. No position setters or
            // success flags: the shipping harbour predicate must become true.
            for(let attempt=0;attempt<20;attempt++) {
                const s=await read();
                report.lifting??=[];report.lifting.push({boat:s.boat.position,tow:s.tow,job:s.job});
                if(s.tow.position[1]>-.6) break;
                assert.equal(s.tow.broken,false,'lifting load must not break cable');
                await hold(['Q'],12);
            }
            await wait(s=>s.job.canDeliver,'physical load near harbour, raised and slow');
            await record('generator-ready-at-harbor');
            await hold(['H'],2);
            await wait(s=>s.job.phase==='completed'&&s.job.secured&&!s.job.pending,'confirmed physical delivery');
            const banked=await record('generator-delivered');
            assert.equal(banked.tow.attached,false);
            assert.equal(banked.session.cargo,0);assert.equal(banked.session.inventory.salvageMaterial,'108');
            await hold(['H'],2);await hold([],10);
            const retry=await record('delivery-retry-does-not-pay');
            assert.equal(retry.session.revision,banked.session.revision);
            await hold(['S'],120);
            const secured=await record('boat-departs-secured-generator');
            assert(Math.hypot(...secured.tow.position.map((v,i)=>v-banked.tow.position[i]))<.005,'delivered load stays secured');
        } else {
        await hold(['Z'],30);
        await wait(s=>s.tow.motor===0&&s.tow.confirmed,'Z release stops payout motor');
        const paidOut=await record('cable-paid-out');
        assert(paidOut.tow.ropeLength>reeled.tow.ropeLength+.25,'paying out must lengthen actual cable target');
        await hold(['W'],180);
        const sailing=await record('sailing');
        assert(Math.hypot(sailing.boat.position[0]-helm.boat.position[0],sailing.boat.position[2]-helm.boat.position[2])>.5,'throttle must move actual hull');
        assert(sailing.boat.speed>.2,'boat must have physical velocity');
        if(process.env.VOXY_SMOKE_COVE_TERRAIN==='1' && sailing.tow.broken) {
            // Brick seabed snagging can overload the cable. This terrain
            // journey verifies real break handling; it does not pass SIM-08's
            // intact towing/lifting acceptance or change that original check.
            assert.equal(sailing.tow.attached,false);
            assert(BigInt(sailing.boat.attachmentBreaks)>BigInt(attached.boat.attachmentBreaks));
            await record('brick-seabed-overload-confirmed');
        } else { assert.equal(sailing.tow.attached,true);assert.equal(sailing.tow.broken,false); }
        assert(Math.hypot(...sailing.tow.position.map((v,i)=>v-attached.tow.position[i]))>.5,'rope must move physical cargo');
        if(sailing.tow.attached) {
            await click('salvage-hook');
            await wait(s=>!s.tow.attached&&s.tow.confirmed,'release button removes rope');
            await record('salvage-released');
        }
        await hold(['W','D'],180);
        const turning=await record('steering');
        const a=sailing.boat.orientation,b=turning.boat.orientation;
        assert(Math.abs(a.reduce((sum,v,i)=>sum+v*b[i],0))<.999,'helm must change hull orientation');
        assert(Math.hypot(turning.player.feet[0]-helm.player.feet[0],turning.player.feet[2]-helm.player.feet[2])>.5,'player must follow moving helm');
        }
        const turning=await read();
        await hold(['E'],2);
        await wait(s=>s.player.mode==='walking','leave helm');
        await record('walking-on-moving-deck');
        const resets=(await read()).resets;
        await key('R',true); await key('R',false);
        await wait(s=>s.ready&&s.resets>resets&&s.player.mode==='walking','R recovery');
        await wait(s=>s.boat.observedTick>turning.boat.observedTick
            && Math.hypot(s.boat.position[0]+.5,s.boat.position[2]+54)<.5,'observed boat recovery to berth');
        if(process.env.VOXY_SMOKE_COVE_JOB==='1') {
            await wait(s=>s.tow.observedTick>turning.tow.observedTick && s.job.secured,'secured cargo survives reset');
            const stable=await read();
            assert(Math.hypot(...stable.tow.position.map((v,i)=>v-turning.tow.position[i]))<.005,'reset cannot respawn delivered cargo');
            assert.equal(stable.session.inventory.salvageMaterial,'108');
        } else await wait(s=>!s.tow.attached && s.tow.observedTick>turning.tow.observedTick
            && Math.hypot(s.tow.position[0]+4.5,s.tow.position[2]+58)<.25,'cargo recovers with boat');
        const reset=await record('recovered-on-dock');
        assert(Math.hypot(...reset.player.feet.map((v,i)=>v-initial.player.feet[i]))<.01);
        assert.equal(reset.player.interactions,'0');
        await call('Emulation.setDeviceMetricsOverride',{width:1280,height:720,deviceScaleFactor:1,mobile:false});
        await hold([],10);
        await record('resized');
        if(process.env.VOXY_SMOKE_COVE_PAUSE==='1') {
            await wait(s=>s.pause.canPause,'dock pause available');await click('salvage-pause');
            await wait(s=>s.pause.phase==='paused','dock pause completes');
            await record('paused-before-leave');
        }
        await click('salvage-leave');
        // The existing UI navigates only after the owned scene has drained.
        for(let n=0;n<600;++n){
            if((await evaluate('location.search')).includes('experience=lego-world'))break;
            if(n===599)throw Error('Leave did not drain/navigate');
            await delay(50);
        }
        report.status='passed';
    }catch(error){report.status='failed';report.error=String(error);try{report.finalState=await read();}catch{}throw error;}
    finally{await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2)+'\n');}
    return report;
}
