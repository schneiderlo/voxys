// Real workshop, resize and Leave/re-entry controls. Numeric observations only.
import assert from 'node:assert/strict';
import {mkdir, writeFile} from 'node:fs/promises';

export async function validateCoveSceneShadows(call, directory) {
    await mkdir(directory, {recursive:true});
    const report={status:'running',kind:'Live scene-shadow integration; no images or state injection',stages:[]};
    const evaluate=async expression=>{
        const result=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        if(result.exceptionDetails)throw Error(JSON.stringify(result.exceptionDetails));
        return result.result.value;
    };
    const read=()=>evaluate(`(() => {
        if(typeof voxyModule==='undefined'||!voxyModule?._voxy_is_initialized?.())return null;
        return {state:JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json())),
            telemetry:JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_telemetry_json())),
            errors:globalThis.voxyUncapturedGpuErrors||[],lost:globalThis.voxyDeviceLost||null};
    })()`);
    const wait=async(predicate,label)=>{
        const until=Date.now()+180000;
        let sample;
        while(Date.now()<until) {
            try{sample=await read();}catch(error){
                // A real page navigation briefly destroys its old JS context.
                if(!/context|voxyModule/i.test(String(error)))throw error;
            }
            if(sample) {
                assert.deepEqual(sample.errors,[]);assert.equal(sample.lost,null);
                assert.equal(sample.state.failed,false);
                if(predicate(sample))return sample;
            }
            await new Promise(resolve=>setTimeout(resolve,100));
        }
        throw Error(label+': '+JSON.stringify(sample));
    };
    const sceneReady=s=>s.state.ready&&s.state.assetFixture?.sceneSunShadows
        &&s.telemetry.frame.count>=12&&s.telemetry.render_gpu.available;
    const record=async(name,predicate=()=>true)=>{
        const sample=await wait(s=>sceneReady(s)&&predicate(s),name);
        assert.equal(sample.state.terrainSurface,'lego');
        assert.equal(sample.state.assetFixture.presentationParts,3);
        assert.equal(sample.state.boat.parts,11);
        assert.equal(sample.state.session.inventory.salvageMaterial,'48');
        report.stages.push({name,...sample});
        await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2)+'\n');
        return sample;
    };
    const click=async id=>{
        const point=await evaluate(`(() => {
            const e=document.getElementById(${JSON.stringify(id)});
            if(!e||e.disabled||!e.checkVisibility())return null;
            e.scrollIntoView({block:'center'});const r=e.getBoundingClientRect();
            return {x:r.x+r.width/2,y:r.y+r.height/2};
        })()`);
        assert(point,`${id} must be visible and enabled`);
        for(const type of ['mousePressed','mouseReleased'])
            await call('Input.dispatchMouseEvent',{type,button:'left',clickCount:1,...point});
    };
    try {
        const url=await evaluate('location.href');
        await record('dock-shadows-active');
        await click('salvage-workshop-toggle');
        await record('workshop-shadows-active',s=>s.state.workshop.open);
        await call('Emulation.setDeviceMetricsOverride',{width:1152,height:720,deviceScaleFactor:1,mobile:false});
        await record('resized-shadow-targets',s=>s.state.workshop.open
            &&s.telemetry.render_gpu.render_width===1152&&s.telemetry.render_gpu.render_height===720);
        await click('salvage-workshop-toggle');
        await record('returned-to-dock',s=>!s.state.workshop.open);
        await wait(s=>s.state.pause.canPause,'pause available');await click('salvage-pause');
        await record('paused-shadows-active',s=>s.state.pause.phase==='paused');
        await click('salvage-leave');
        await wait(s=>!s.state.active&&s.telemetry.frame.count>=12&&s.telemetry.render_gpu.available,'Leave drains into LEGO World');
        assert((await evaluate('location.search')).includes('experience=lego-world'));
        report.stages.push({name:'leave-drained-to-lego-world',sample:await read()});
        await call('Page.navigate',{url});
        await record('reentered-with-current-shadow-owner');
        report.status='passed';
    } catch(error) {
        report.status='failed';report.error=String(error);throw error;
    } finally {
        await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2)+'\n');
    }
    return report;
}
