// Read/capture/preflight only. The caller reaches pause through actual controls.
// This verifies archive preparation, not a durable save or live restoration.
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {mkdir,writeFile} from 'node:fs/promises';

export async function validateCoveArchive(call,directory,name) {
    const evaluate=async expression=>{
        const result=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        if(result.exceptionDetails)throw Error(JSON.stringify(result.exceptionDetails));
        return result.result.value;
    };
    const read=()=>evaluate('JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json()))');
    const action=(kind,text='')=>evaluate(`voxyModule.ccall('voxy_salvage_expedition_action','string',['number','string'],[${kind},${JSON.stringify(text)}])`);
    const before=await read();assert.equal(before.pause.phase,'paused');
    const hex=await action(1);assert(hex.length>846,'the actual paused cove must produce an archive');
    assert(/^[0-9a-f]+$/.test(hex));const bytes=Buffer.from(hex,'hex');
    assert.equal(bytes.subarray(0,4).toString(),'SVCE');const schema=bytes.readUInt32LE(4);assert(schema>=1&&schema<=6);
    assert.equal(createHash('sha256').update(bytes.subarray(0,-32)).digest('hex'),bytes.subarray(-32).toString('hex'));
    let at=8;
    const u64=()=>{const n=bytes.readBigUInt64LE(at);at+=8;return n.toString();};
    const f64=()=>{const n=bytes.readDoubleLE(at);at+=8;assert(Number.isFinite(n));return n;};
    const f32=()=>{const n=bytes.readFloatLE(at);at+=4;assert(Number.isFinite(n));return n;};
    const id=()=>{const world=bytes.subarray(at,at+16).toString('hex');at+=16;return {world,counter:u64()};};
    const vector64=()=>[f64(),f64(),f64()];
    const vector32=()=>[f32(),f32(),f32()];
    const motion=()=>({position:vector64(),orientation:[f32(),f32(),f32(),f32()],velocity:vector32(),angularVelocity:vector32()});
    const archive={schema,tick:u64(),origin:vector64(),boat:id(),cargo:id(),job:id(),definition:id()};
    archive.definition.version=bytes.readUInt32LE(at);at+=4;
    archive.boatMotion=motion();archive.cargoMotion=motion();
    archive.player={feet:vector64(),verticalSpeed:f64(),tick:u64(),interactions:u64(),mode:bytes[at++],aboard:Boolean(bytes[at++]),yaw:f32(),pitch:f32()};
    archive.waterModel=bytes.readUInt32LE(at);at+=4;archive.waterSeconds=f64();
    archive.waterParameters=Array.from({length:13},f32);
    archive.cargoState=bytes[at++];archive.winch=id();archive.ropeLength=f32();
    assert.equal(at,419,'frozen SVCE v1 physical section size');
    if(schema>=2){
        archive.harbor={profile:bytes.readUInt32LE(at),mode:bytes[at+4],brokenMask:bytes[at+5]};at+=6;
        archive.harbor.lengths=Array.from({length:4},f32);
    }
    if(schema>=3){
        const count=bytes.readUInt32LE(at);at+=4;assert(count<=4&&(schema>=4||count>0));
        archive.recoveryDesigns=[];
        for(let i=0;i<count;i++){
            const size=bytes.readUInt32LE(at);at+=4;assert(size>0&&size<=131072&&at+size<=bytes.length-32);
            assert.equal(bytes.subarray(at,at+4).toString(),'SVBP');
            archive.recoveryDesigns.push({bytes:size,sha256:createHash('sha256').update(bytes.subarray(at,at+size)).digest('hex')});at+=size;
        }
    }
    if(schema>=4){
        archive.controlPart=id();archive.playerRoot=id();const count=bytes.readUInt32LE(at);at+=4;
        assert(count>0&&count<=32&&at+88*count<=bytes.length-32);
        archive.roots=Array.from({length:count},()=>({key:id(),motion:motion()}));
    }
    if(schema>=5){
        const count=bytes.readUInt32LE(at);at+=4;
        assert(count<=1&&(schema>=6||count>0));
        // This established journey owns exactly one load. Reading the v6
        // count is necessary even when zero; it is not a two-job acceptance.
        assert.equal(count,0,'this single-cargo journey must not gain another load');
        archive.additionalCargoCount=count;
    }
    if(schema>=6){
        const start=at,profile=bytes.readUInt32LE(at);at+=4;assert.equal(profile,1);
        const worldVelocity=vector64(),facingYaw=f64(),cameraDistance=f64();
        const flag=()=>{const value=bytes[at++];assert(value===0||value===1);return Boolean(value);};
        archive.character={profile,worldVelocity,facingYaw,cameraDistance,chaseCamera:flag(),reducedMotion:flag(),loadView:flag()};
        assert.equal(at-start,47,'frozen SVCE v6 character extension');
        assert(worldVelocity.every(v=>Math.abs(v)<=150)&&Math.abs(facingYaw)<=Math.PI&&cameraDistance>=1.5&&cameraDistance<=12);
        if(archive.player.mode===1||archive.player.mode===2){
            assert.equal(archive.player.aboard,false);assert.equal(archive.player.verticalSpeed,worldVelocity[1]);
        }else assert.equal(archive.player.verticalSpeed,0);
    }
    const logicalSize=bytes.readUInt32LE(at);at+=4;
    assert.equal(bytes.subarray(at,at+4).toString(),'SVSC');at+=logicalSize;
    const parentSize=bytes.readUInt32LE(at);at+=4;
    if(parentSize)assert.equal(bytes.subarray(at,at+4).toString(),'SVSC');
    at+=parentSize;assert.equal(at,bytes.length-32);
    assert.equal(archive.tick,before.pause.tick);assert.equal(archive.tick,before.session.tick);
    assert.equal(archive.boat.counter,before.boat.buildId);
    const near=(a,b,tolerance=1e-3)=>assert(Math.abs(a-b)<=tolerance,`${a} differs from ${b}`);
    if(schema>=4){
        assert.equal(archive.controlPart.counter,before.boat.controlPart);
        assert.equal(archive.playerRoot.counter,before.player.rootKey);
        assert.equal(archive.roots.length,before.boat.rootCount);
        assert.equal(before.boat.joinedTick,archive.tick);
        for(let index=0;index<archive.roots.length;index++){
            const saved=archive.roots[index],observed=before.boat.roots[index];
            assert.equal(saved.key.counter,observed.key);assert.equal(saved.key.world,archive.boat.world);
            assert.equal(observed.observedTick,archive.tick);assert.equal(observed.active,true);
            for(let axis=0;axis<3;axis++)near(saved.motion.position[axis]-archive.origin[axis],observed.position[axis]);
            const sign=saved.motion.orientation.reduce((sum,value,axis)=>sum+value*observed.orientation[axis],0)<0?-1:1;
            saved.motion.orientation.forEach((value,axis)=>near(value,sign*observed.orientation[axis]));
            for(const field of ['velocity','angularVelocity'])
                saved.motion[field].forEach((value,axis)=>near(value,observed[field][axis]));
        }
    }
    if(schema>=6){
        assert(before.character&&before.characterCamera,'live v6 must expose the saved character and camera settings');
        archive.character.worldVelocity.forEach((value,axis)=>near(value,before.character.worldVelocity[axis]));
        near(archive.character.facingYaw,before.character.facingYaw);
        near(archive.character.cameraDistance,before.characterCamera.distance);
        assert.equal(archive.character.chaseCamera,before.characterCamera.mode==='chase');
        assert.equal(archive.character.reducedMotion,before.characterCamera.reducedMotion);
        assert.equal(archive.character.loadView,before.characterCamera.frameLoad);
        near(archive.player.yaw,before.characterCamera.yaw);near(archive.player.pitch,before.characterCamera.elevation);
    }
    for(let i=0;i<3;i++){
        near(archive.boatMotion.position[i]-archive.origin[i],before.boat.position[i]);
        near(archive.cargoMotion.position[i]-archive.origin[i],before.tow.position[i]);
    }
    near(Math.hypot(...archive.boatMotion.velocity),before.boat.speed);
    near(archive.waterSeconds,before.pause.waterTime);
    assert.equal(archive.player.tick,before.player.tick);
    assert.equal(archive.player.aboard,before.player.onBoat);
    assert.equal(archive.player.mode,['walking','airborne','swimming','helm'].indexOf(before.player.mode));
    assert.equal(archive.cargoState,before.job.phase==='completed'?3:before.tow.broken?2:before.tow.attached?1:0);
    if(before.tow.attached){near(archive.ropeLength,before.tow.ropeLength);assert.notEqual(archive.winch.counter,'0');}
    else assert.equal(archive.ropeLength,0);
    assert.equal(await action(2,hex),'ok','saved build and player must pass actual installed-content reconstruction');
    assert.equal(await action(1),hex,'capture must not change any accepted state or clock');
    const corrupt=Buffer.from(bytes);corrupt[140]^=1;
    assert.equal(await action(2,corrupt.toString('hex')),'','corrupted archive must refuse before reconstruction');
    // Isolated storage integration with an ACTUAL captured archive and host
    // validator. Reopen the database connection; do not claim browser reload.
    const storage=await evaluate(`(async()=>{
        const hex=${JSON.stringify(hex)},world=${JSON.stringify(archive.boat.world)},databaseName=${JSON.stringify('voxys-cove-archive-integration-v1-'+name)};
        const payload=Uint8Array.from(hex.match(/../g),pair=>parseInt(pair,16));
        const toHex=data=>Array.from(data,b=>b.toString(16).padStart(2,'0')).join('');
        const validate=data=>voxyModule.ccall('voxy_salvage_expedition_action','string',['number','string'],[2,toHex(data)])==='ok';
        let store=await VoxyExpeditionStore.openStore(world,validate,{databaseName});
        try {
            const empty=await store.load();if(empty.generation!==0n)throw Error('Expected private fresh slot');
            const generation=await store.publish(0n,payload);await store.close();
            store=await VoxyExpeditionStore.openStore(world,validate,{databaseName});
            const loaded=await store.load();
            return {generation:generation.generation.toString(),reopenedGeneration:loaded.generation.toString(),
                sameBytes:toHex(loaded.payload)===hex,needsRepair:loaded.needsRepair};
        }finally {await store.close();}
    })()`);
    assert.deepEqual(storage,{generation:'1',reopenedGeneration:'1',sameBytes:true,needsRepair:false});
    const after=await read();
    for(const key of ['session','player','boat','tow','pause'])assert.deepEqual(after[key],before[key],`${key} changed during archive preparation`);
    const result={status:'passed',storage,kind:'actual paused capture and nonactivating load preflight',bytes:bytes.length,
        sha256:createHash('sha256').update(bytes).digest('hex'),logicalBytes:logicalSize,parentBytes:parentSize,archive,
        parts:before.boat.parts,massKg:before.boat.massKg,paidPartIds:before.boat.paidPartIds,inventory:before.session.inventory};
    await writeFile(`${directory}/${name}.svce`,bytes);
    await writeFile(`${directory}/${name}.json`,JSON.stringify(result,null,2));
    return result;
}

export async function validateCoveArchiveJourney(call,directory){
    await mkdir(directory,{recursive:true});
    const evaluate=async expression=>{
        const result=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        if(result.exceptionDetails)throw Error(JSON.stringify(result.exceptionDetails));return result.result.value;
    };
    const read=()=>evaluate('JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json()))');
    const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
    const wait=async(predicate,label)=>{
        const deadline=Date.now()+20000;let state;
        do {state=await read();if(predicate(state))return state;await delay(40);}while(Date.now()<deadline);
        throw Error(`${label}: ${JSON.stringify(state)}`);
    };
    const click=async id=>{
        await delay(160);
        const point=await evaluate(`(()=>{const e=document.getElementById(${JSON.stringify(id)});
            if(!e||e.hidden||e.disabled)return null;e.scrollIntoView({block:'nearest'});
            const r=e.getBoundingClientRect(),x=r.x+r.width/2,y=r.y+r.height/2;
            return e.contains(document.elementFromPoint(x,y))?{x,y}:null;})()`);
        assert(point,`${id} must be available`);
        await call('Input.dispatchMouseEvent',{type:'mouseMoved',...point});
        await call('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',clickCount:1,...point});
        await call('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',clickCount:1,...point});
    };
    const result={status:'running',kind:'real two-pontoon purchase, pause, archive and storage preparation; no live load',stages:[]};
    const record=async name=>{const state=await read();assert.equal(state.failed,false);assert.equal(state.terrainSurface,'lego');
        result.stages.push({name,state});return state;};
    try {
        await wait(s=>s.ready&&s.workshop?.canOpen&&s.boat.observedTick>0,'cove ready');
        await record('fresh-world');
        assert.equal(await evaluate("voxyModule.ccall('voxy_salvage_expedition_action','string',['number','string'],[1,''])"),'','capture must refuse running state');
        await click('salvage-workshop-toggle');await wait(s=>s.workshop.open,'workshop');
        for(let i=0;i<2;i++){
            await click('workshop-add');await wait(s=>s.workshop.valid&&s.workshop.changed,'fitted pontoon');
            await click('workshop-keep');await wait(s=>!s.workshop.changed,'kept pontoon');
        }
        const draft=await record('two-pontoon-draft');assert.equal(draft.workshop.charge,'48');
        await click('workshop-launch');await wait(s=>!s.workshop.open&&!s.workshop.pending&&s.boat.parts===13&&s.pause.canPause,'paid physical launch');
        const launched=await record('two-pontoons-purchased');assert.equal(launched.boat.massKg,1275);
        assert.equal(launched.session.inventory.salvageMaterial,'0');assert.equal(launched.boat.paidPartIds.length,2);
        await click('salvage-pause');await wait(s=>s.pause.phase==='paused','joined pause');await record('paused');
        result.archive=await validateCoveArchive(call,directory,'paid-boat-archive');
        assert.equal(result.archive.parentBytes,0,'fresh test world has no retired parent');
        await record('archive-and-database-reopen');
        await click('salvage-leave');const deadline=Date.now()+20000;
        while(Date.now()<deadline&&!await evaluate('location.search.includes("experience=lego-world")'))await delay(50);
        assert(await evaluate('location.search.includes("experience=lego-world")'),'Leave must drain from pause');
        result.status='passed';
    }catch(error){result.status='failed';result.error=String(error);throw error;}
    finally {await writeFile(`${directory}/summary.json`,JSON.stringify(result,null,2));}
    return result;
}
