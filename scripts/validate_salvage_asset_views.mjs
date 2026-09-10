// Real application camera/light captures from a shared native/browser recipe.
import assert from 'node:assert/strict';
import {mkdir,readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';

export async function validateSalvageAssetViews(call,directory,{guides=0,lod=0,views=null,recipePath=null}={}) {
    await mkdir(directory,{recursive:false});
    const recipeBytes=await readFile(recipePath||new URL('../docs/validation/salvage/ASSET-04/inspection-views.json',import.meta.url));
    const recipe=JSON.parse(recipeBytes);
    assert([0,1,2].includes(guides),'unknown guide mode');
    assert([0,1,2,3].includes(lod),'unknown detail mode');
    if(views)assert(views.length===new Set(views).size&&views.every(name=>recipe.views.some(view=>view.name===name)),'unknown or repeated view');
    const selectedViews=recipe.views.filter(view=>!views||views.includes(view.name));
    const registry=JSON.parse(await readFile(new URL('../'+recipe.registry,import.meta.url)));
    const uploads=registry.bundles.reduce((sum,bundle)=>sum+bundle.lod_limits.length,0);
    const expected=recipe.inspection_expectations||{maximum_model_draws:registry.placements.length,guide_boxes:[0,26,registry.schema===2?150:360]};
    assert(Number.isSafeInteger(expected.maximum_model_draws)&&expected.maximum_model_draws>=registry.placements.length&&expected.maximum_model_draws<=256);
    assert(Array.isArray(expected.guide_boxes)&&expected.guide_boxes.length===3&&expected.guide_boxes.every(value=>Number.isSafeInteger(value)&&value>=0&&value<=512));
    const report={status:'running',recipe,guides,forced_lod:lod,selected_views:views,recipe_sha256:createHash('sha256').update(recipeBytes).digest('hex'),views:[]};
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
    const getState=()=>evaluate(`({...${expression},lost:globalThis.voxyDeviceLost||null,
        errors:globalThis.voxyUncapturedGpuErrors||[],render:JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_telemetry_json())).render_gpu})`);
    try{
        await wait(`(${expression}).ready && BigInt((${expression}).assetFixture.completedSerial)>0n`,'asset ready');
        const initial=await getState();
        assert.equal(initial.resets,0,'Use a fresh view-capture run');
        const origin=initial.camera.local.map((v,i)=>v+initial.camera.sector[i]*256-registry.camera.eye[i]);
        report.origin=origin;report.initial=initial;
        assert.equal(await evaluate(`voxyModule._voxy_salvage_preview_action(${20+guides})`),1);
        assert.equal(await evaluate(`voxyModule._voxy_salvage_preview_action(${10+lod})`),1);
        for(const view of selectedViews){
            const before=await getState(), eye=view.eye.map((v,i)=>v+origin[i]);
            const direction=view.target.map((v,i)=>v-view.eye[i]);
            const yaw=Math.atan2(direction[0],direction[2]),pitch=Math.asin(direction[1]/Math.hypot(...direction));
            assert.equal(await evaluate(`voxyModule._voxy_set_camera_pose(${[...eye,yaw,pitch].join(',')})`),1);
            const azimuth=Math.atan2(view.sun[2],view.sun[0])*180/Math.PI;
            const elevation=Math.asin(view.sun[1]/Math.hypot(...view.sun))*180/Math.PI;
            for(const [key,value,commit] of [['lighting.sunAzimuth',azimuth,0],['lighting.sunElevation',elevation,1]]){
                assert.equal(await evaluate(`voxyModule.ccall('voxy_renderer_set_number','number',['string','number','number'],${JSON.stringify([key,value,commit])})`),1);
            }
            await wait(`voxyModule._voxy_renderer_get_applied_revision()===voxyModule._voxy_renderer_get_revision()
                && (${expression}).ready && BigInt((${expression}).assetFixture.completedSerial)>BigInt(${JSON.stringify(before.assetFixture.submittedSerial)})+2n`,'view/light applied and queue complete');
            const state=await getState();
            assert.equal(state.failed,false);assert.equal(state.lost,null);assert.deepEqual(state.errors,[]);
            assert.equal(state.bodies,0,'Inspection views must be free of cove scenery');
            assert.equal(state.assetFixture.uploads,uploads);
            // Close views legitimately cull parts behind the camera. Preserve
            // the actual count; require a visible part without forcing four.
            assert(state.assetFixture.draws>=1 && state.assetFixture.draws<=expected.maximum_model_draws+expected.guide_boxes[guides]);
            assert.equal(state.assetFixture.guides,guides);
            assert.equal(state.assetFixture.forcedLod,String(lod));
            if(lod)assert(state.assetFixture.lods.every(level=>level===null||level===String(lod)));
            assert.equal(state.assetFixture.guideBoxes,expected.guide_boxes[guides]);
            assert.equal(state.assetFixture.prototypeUploads,registry.prototypes?.length||0);
            assert.equal(state.assetFixture.generation,initial.assetFixture.generation);
            assert.equal(state.render.render_width,recipe.physical_viewport[0]);assert.equal(state.render.render_height,recipe.physical_viewport[1]);
            const actualEye=state.camera.local.map((v,i)=>v+state.camera.sector[i]*256);
            assert(Math.hypot(...actualEye.map((v,i)=>v-eye[i]))<0.001,'Camera did not settle at requested view');
            const settings=await evaluate(`Object.fromEntries(['lighting.sunAzimuth','lighting.sunElevation',
                'lighting.sunIntensity','lighting.ambientIntensity','lighting.exposure'].map(key=>[key,
                voxyModule.ccall('voxy_renderer_get_number','number',['string'],[key])]))`);
            const azimuthError=((settings['lighting.sunAzimuth']-azimuth+540)%360)-180;
            assert(Math.abs(azimuthError)<.001&&Math.abs(settings['lighting.sunElevation']-elevation)<.001,
                'Requested inspection light was clamped; use a supported matching native/browser recipe');
            const png=Buffer.from((await call('Page.captureScreenshot',{format:'png'})).data,'base64');
            await writeFile(`${directory}/${view.name}.png`,png);
            report.views.push({name:view.name,state,settings,requested:{eye,yaw,pitch,azimuth,elevation},png_sha256:createHash('sha256').update(png).digest('hex')});
        }
        report.status='captured; visual review required';
    }catch(error){report.status='failed';report.error=String(error);throw error;}
    finally{await writeFile(`${directory}/report.json`,JSON.stringify(report,null,2)+'\n');}
    return report;
}
