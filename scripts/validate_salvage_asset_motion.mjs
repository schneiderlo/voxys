// Continuous camera motion in the shipping application. These are inspection
// captures, not a benchmark: telemetry polling and screenshots add overhead.
import assert from 'node:assert/strict';
import {mkdir,readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';

export async function validateSalvageAssetMotion(call,directory,{recipePath=null}={}) {
    await mkdir(directory,{recursive:false});
    const recipeBytes=await readFile(recipePath||new URL('../docs/validation/salvage/ASSET-04/lod-motion.json',import.meta.url));
    const recipe=JSON.parse(recipeBytes);
    const registryBytes=await readFile(new URL('../'+recipe.registry,import.meta.url));
    const registry=JSON.parse(registryBytes);
    const workload=recipe.inspection_expectations||{parts:6,connections:5,uploads:3,
        prototype_uploads:1,model_draws:6,tracked_lod_placements:[0,1,2,3]};
    assert([1,2].includes(registry.schema));assert.equal(workload.parts,registry.placements.length);
    assert(registry.schema===2||recipe.inspection_expectations,'Gallery motion requires an explicit workload');
    assert.equal(workload.connections,registry.schema===2?registry.connections.length:0);
    const forced=recipe.forced_lod??0;
    assert(Number.isSafeInteger(forced)&&forced>=0&&forced<=3,'Forced detail must be an integer from 0 to 3');
    for(const key of ['parts','connections','uploads','prototype_uploads','model_draws'])
        assert(Number.isSafeInteger(workload[key])&&workload[key]>=0&&workload[key]<=512);
    const tracked=workload.tracked_lod_placements;
    assert(Array.isArray(tracked)&&tracked.length>0&&tracked.length<=32&&new Set(tracked).size===tracked.length);
    assert(tracked.every(i=>Number.isSafeInteger(i)&&i>=0&&i<registry.placements.length&&registry.placements[i].prototype===undefined));
    const assembly=registry.schema===2?{parts:workload.parts,connections:workload.connections}:undefined;
    const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
    const report={status:'running',scope:'Real continuous browser camera motion; capture overhead prevents performance claims; visual review required',
        recipe,recipe_sha256:hash(recipeBytes),registry_sha256:hash(registryBytes),
        runner_sha256:hash(await readFile(new URL(import.meta.url))),frames:[]};
    const expression='JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json()))';
    const evaluate=async expression=>{
        const result=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        if(result.exceptionDetails)throw Error(JSON.stringify(result.exceptionDetails));
        return result.result?.value;
    };
    const wait=async (expression,label)=>{
        const deadline=Date.now()+45000;
        do{const value=await evaluate(expression);if(value)return value;await new Promise(r=>setTimeout(r,100));}while(Date.now()<deadline);
        throw Error('Timed out: '+label);
    };
    const state=()=>evaluate(`({...${expression},lost:globalThis.voxyDeviceLost||null,errors:globalThis.voxyUncapturedGpuErrors||[]})`);
    try {
        assert.equal(recipe.schema,1);assert(recipe.duration_seconds>=10&&recipe.duration_seconds<=60);
        assert.equal(recipe.trace_capacity,8192);
        await wait(`(${expression}).ready && BigInt((${expression}).assetFixture.completedSerial)>0n`,'assembly ready');
        assert.equal(await evaluate(`voxyModule._voxy_salvage_preview_action(${10+forced})`),1);
        const initial=await state();report.initial=initial;
        report.render=await evaluate('JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_telemetry_json())).render_gpu');
        assert.equal(report.render.render_width,recipe.physical_viewport[0]);
        assert.equal(report.render.render_height,recipe.physical_viewport[1]);
        assert.equal(initial.resets,0);assert.deepEqual(initial.assetFixture.assembly,assembly);
        assert.equal(initial.assetFixture.forcedLod,String(forced));assert.equal(initial.assetFixture.guides,0);
        const origin=initial.camera.local.map((v,i)=>v+initial.camera.sector[i]*256-registry.camera.eye[i]);
        const target=recipe.target.map((v,i)=>v+origin[i]);
        const ray=recipe.eye_direction.map(v=>v/Math.hypot(...recipe.eye_direction));
        const yaw=Math.atan2(-ray[0],-ray[2]),pitch=Math.asin(-ray[1]);
        const eye=ray.map((v,i)=>target[i]+v*recipe.near_distance_metres);
        assert.equal(await evaluate(`voxyModule._voxy_set_camera_pose(${[...eye,yaw,pitch].join(',')})`),1);
        await wait(`BigInt((${expression}).assetFixture.completedSerial)>BigInt(${JSON.stringify(initial.assetFixture.submittedSerial)})+2n`,'near view rendered');
        report.start=await state();report.origin=origin;
        // Only camera pose changes. The real registry, model admission, LOD
        // selection, renderer, queue and session stay authoritative.
        await evaluate(`(() => {
            if(globalThis.voxyInspectionMotion)throw Error('Motion run already exists');
            const target=${JSON.stringify(target)},ray=${JSON.stringify(ray)},recipe=${JSON.stringify(recipe)};
            const run=globalThis.voxyInspectionMotion={started:performance.now(),done:false,error:null,trace:[],request:null};
            const move=now=>{
                try {
                    const elapsed=Math.max(0,(now-run.started)/1000),u=Math.min(1,elapsed/recipe.duration_seconds);
                    const leg=u<=.5?2*u:2*(1-u),smooth=leg*leg*(3-2*leg);
                    const distance=recipe.near_distance_metres*Math.pow(recipe.far_distance_metres/recipe.near_distance_metres,smooth);
                    const eye=ray.map((v,i)=>target[i]+v*distance);
                    if(voxyModule._voxy_set_camera_pose(...eye,${yaw},${pitch})!==1)throw Error('Camera motion refused');
                    const state=${expression};
                    if(run.trace.length>=recipe.trace_capacity)throw Error('Trace capacity exhausted');
                    run.trace.push({elapsed,requested_eye:eye,distance,state});
                    if(u===1){run.done=true;return;}
                    run.request=requestAnimationFrame(move);
                }catch(error){run.error=String(error);run.done=true;}
            };
            run.request=requestAnimationFrame(move);
        })()`);
        const deadline=Date.now()+(recipe.duration_seconds+30)*1000;
        while(Date.now()<deadline){
            const before=await evaluate('({elapsed:(performance.now()-voxyInspectionMotion.started)/1000,done:voxyInspectionMotion.done,error:voxyInspectionMotion.error})');
            assert.equal(before.error,null);
            if(before.done)break;
            const image=await call('Page.captureScreenshot',{format:'jpeg',quality:90});
            const bytes=Buffer.from(image.data,'base64');
            const after=await evaluate(`({elapsed:(performance.now()-voxyInspectionMotion.started)/1000,state:${expression}})`);
            const filename=`frame-${String(report.frames.length).padStart(4,'0')}.jpg`;
            await writeFile(`${directory}/${filename}`,bytes);
            report.frames.push({filename,sha256:hash(bytes),before_seconds:before.elapsed,after_seconds:after.elapsed,state:after.state});
            assert(report.frames.length<=recipe.frame_capacity,'Screenshot capacity exceeded');
            await new Promise(r=>setTimeout(r,recipe.minimum_capture_interval_ms));
        }
        const run=await evaluate('({done:voxyInspectionMotion.done,error:voxyInspectionMotion.error,trace:voxyInspectionMotion.trace})');
        const traceBytes=Buffer.from(JSON.stringify(run.trace)+'\n');
        await writeFile(`${directory}/trace.json`,traceBytes);
        report.trace={samples:run.trace.length,sha256:hash(traceBytes),seconds:run.trace.at(-1)?.elapsed};
        assert(run.done,'Continuous motion timed out');assert.equal(run.error,null);
        assert(run.trace.length>60&&run.trace.length<=recipe.trace_capacity);
        const expected=initial.assetFixture;
        for(const sample of run.trace){
            const s=sample.state,a=s.assetFixture;
            assert.equal(s.ready,true);assert.equal(s.failed,false);assert.equal(s.bodies,0);
            assert.equal(a.generation,expected.generation);assert.equal(a.uploads,workload.uploads);assert.equal(a.prototypeUploads,workload.prototype_uploads);
            assert.equal(a.gpuReservationBytes,expected.gpuReservationBytes);assert.equal(a.forcedLod,String(forced));assert.equal(a.guides,0);
            assert.deepEqual(a.assembly,assembly);assert.equal(a.draws,workload.model_draws);
            assert.equal(a.lods.length,registry.placements.length);
            for(let i=0;i<a.lods.length;i++)assert.equal(a.lods[i]===null,registry.placements[i].prototype!==undefined);
            assert.equal(s.session.builds,0);assert.deepEqual(s.session.inventory,initial.session.inventory);
        }
        report.transitions=[];
        report.tracked_lod_placements=tracked;report.lod_sequences=[];
        for(const placement of tracked){
            const observed=new Set(run.trace.map(t=>t.state.assetFixture.lods[placement]));
            assert.deepEqual([...observed].sort(),forced?[String(forced)]:['1','2','3'],`Placement ${placement} must obey the declared detail mode`);
            let previous=run.trace[0].state.assetFixture.lods[placement];
            const sequence=[previous];
            for(const t of run.trace){
                const next=t.state.assetFixture.lods[placement];
                if(next!==previous){sequence.push(next);report.transitions.push({placement,elapsed:t.elapsed,from:previous,to:next,distance:t.distance,camera:t.state.camera});previous=next;}
            }
            assert.deepEqual(sequence,forced?[String(forced)]:['1','2','3','2','1'],`Placement ${placement} must obey the declared detail sequence`);
            report.lod_sequences.push(sequence);
        }
        const serial=run.trace.at(-1).state.assetFixture.submittedSerial;
        await wait(`BigInt((${expression}).assetFixture.completedSerial)>BigInt(${JSON.stringify(serial)})+2n`,'final motion GPU completion');
        report.final=await state();assert.equal(report.final.lost,null);assert.deepEqual(report.final.errors,[]);
        assert(report.frames.length>=30,'Too few captured motion frames');
        report.status='captured; visual review required';
    }catch(error){report.status='failed';report.error=String(error);throw error;}
    finally{
        await evaluate('if(globalThis.voxyInspectionMotion){cancelAnimationFrame(voxyInspectionMotion.request);delete globalThis.voxyInspectionMotion;}').catch(()=>{});
        await writeFile(`${directory}/report.json`,JSON.stringify(report,null,2)+'\n');
    }
    return {status:report.status,trace:report.trace,transitions:report.transitions,frames:report.frames.length};
}
