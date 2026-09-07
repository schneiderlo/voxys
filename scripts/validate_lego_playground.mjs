// Operate the shipped controls in the actual full application, then verify
// resident GPU state. No substitute physics or success injection.
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';
export async function validatePlayground(call,directory){
    await mkdir(directory,{recursive:true});
    const evaluate=async expression=>{const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));return r.result?.value;};
    const state=()=>evaluate('JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_lego_hud_json()))');
    const waitFor=async(test,label,timeout=18000)=>{const start=Date.now();let s;do{s=await state();if(test(s))return s;await new Promise(r=>setTimeout(r,100));}while(Date.now()-start<timeout);throw Error(`${label}: ${JSON.stringify(s)}`);};
    const click=async selector=>{
        const start=Date.now();
        while(await evaluate(`document.querySelector(${JSON.stringify(selector)}).disabled`)) {
            if(Date.now()-start>18000)throw Error(`Disabled control ${selector}`);
            await new Promise(r=>setTimeout(r,100));
        }
        const rect=await evaluate(`(()=>{const r=document.querySelector(${JSON.stringify(selector)}).getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2,w:r.width};})()`);
        assert(rect.w>0,`Hidden control ${selector}`);
        await call('Input.dispatchMouseEvent',{type:'mousePressed',x:rect.x,y:rect.y,button:'left',clickCount:1});
        await call('Input.dispatchMouseEvent',{type:'mouseReleased',x:rect.x,y:rect.y,button:'left',clickCount:1});
    };
    const key=async(code,key,value)=>{
        await evaluate('document.activeElement?.blur()');
        await call('Input.dispatchKeyEvent',{type:'keyDown',code,key,windowsVirtualKeyCode:value});
        await new Promise(r=>setTimeout(r,80));
        await call('Input.dispatchKeyEvent',{type:'keyUp',code,key,windowsVirtualKeyCode:value});
    };
    const capture=async name=>{await new Promise(r=>setTimeout(r,200));const r=await call('Page.captureScreenshot',{format:'png'});await writeFile(`${directory}/${name}.png`,Buffer.from(r.data,'base64'));await writeFile(`${directory}/${name}.json`,JSON.stringify(await state(),null,2));};
    await click('#lego-build-enter');let s=await waitFor(s=>s.active,'enter');const origin=s.origin;
    const aimBuild=async()=>{
        const y=origin[1]+9,z=origin[2]+12;
        // Aim at the central pad. The same cursor ray selects each new top.
        await evaluate(`voxyModule._voxy_set_camera_pose(${origin[0]},${y},${z},Math.PI,${-Math.atan2(9,12)})`);
    };
    await aimBuild();await capture('empty-pad');
    for(let i=1;i<=3;i++){
        await waitFor(s=>s.valid,'valid preview');
        if(i===2)await key('Enter','Enter',13);else await click('#lego-place');
        s=await waitFor(s=>s.bricks===i&&s.sleeping>=i&&s.awake===0,'stack settles');await capture(`level-${i}`);
    }
    s=await waitFor(s=>s.phase===1,'target ready');assert(s.sleepTransitions>=3);
    // Let the target settle: success must not occur spontaneously.
    await waitFor(s=>s.awake===0,'target settles');await capture('target-ready');
    await click('#lego-aim');await click('[data-lego-action="2"]');
    s=await waitFor(s=>s.phase===2,'ball knocks down target');
    assert(s.impacts>0&&s.wakeTransitions>0);assert(s.balls<=8&&s.bricks<=48&&s.dust<=48);
    await capture('success');const success=s;
    await click('[data-lego-action="3"]');s=await waitFor(s=>s.bricks===0&&s.balls===0&&s.phase===0,'reset');
    assert.equal(s.dust,0);await aimBuild();await waitFor(s=>s.valid,'replay preview');await click('#lego-place');
    s=await waitFor(s=>s.bricks===1&&s.sleeping===1,'replay brick');await capture('replay');
    // Exercise the full brick pool through the same aim/placement actions.
    await click('[data-lego-action="3"]');
    await click('[data-lego-action="4"]');
    const filled=await evaluate(`(()=>{
        let placed=0;
        for(let z=-3;z<3;z++)for(let x=-4;x<4;x++){
            voxyModule._voxy_set_camera_pose(${origin[0]}+x+.5,${origin[1]}+12,${origin[2]}+z+.5,0,-1.5707);
            placed+=voxyModule._voxy_lego_action(1);
        }return placed;
    })()`);
    assert.equal(filled,48,'all 48 placeable bricks fit on the pad');
    await waitFor(s=>s.bricks===48&&s.sleeping===48,'full pool sleeps');await capture('full-pool');
    await evaluate(`voxyModule._voxy_set_camera_pose(${origin[0]},${origin[1]}+3,${origin[2]}+10,Math.PI,-.1)`);
    const stress=await evaluate(`new Promise(resolve=>{
        const frames=[],cpu=[],gpu=[];let start=performance.now(),last=start,gpuFrame=-1,shots=0,peakAwake=0;
        const fire=setInterval(()=>{voxyModule._voxy_lego_action(2);if(++shots>=8)clearInterval(fire);},350);
        function tick(now){
            frames.push(now-last);last=now;
            const t=JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_telemetry_json()));
            const s=JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_lego_hud_json()));
            peakAwake=Math.max(peakAwake,s.awake);cpu.push(t.frame.cpu_ms);
            if(t.render_gpu.available&&t.render_gpu.frame!==gpuFrame){gpuFrame=t.render_gpu.frame;gpu.push(t.render_gpu.gpu_frame_ms);}
            if(now-start<10000&&frames.length<1800)requestAnimationFrame(tick);
            else{clearInterval(fire);resolve({frames,cpu,gpu,peakAwake,shots,state:s});}
        }requestAnimationFrame(tick);
    })`);
    assert.equal(stress.shots,8);assert(stress.state.balls<=8&&stress.state.bricks<=48&&stress.state.dust<=48);
    await capture('after-impacts');
    await writeFile(`${directory}/stress.json`,JSON.stringify(stress,null,2));
    await click('[data-lego-action="3"]');
    await waitFor(s=>s.bricks===0&&s.balls===0,'full pool reset');
    await key('KeyP','p',80);await waitFor(s=>!s.active,'P exits playground');
    await key('KeyP','p',80);await waitFor(s=>s.active,'P re-enters playground');
    const diagnostics=await evaluate('({lost:globalThis.voxyDeviceLost,errors:globalThis.voxyUncapturedGpuErrors,heap:voxyModule._voxy_get_heap_used_bytes(),telemetry:JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_telemetry_json()))})');
    assert(!diagnostics.lost&&!diagnostics.errors.length);
    assert.equal(diagnostics.telemetry.physics.backend,'webgpu_soft');
    assert.equal(diagnostics.telemetry.render.terrain_width,8192);
    const report={status:'passed',success,replay:s,stress,diagnostics};
    await writeFile(`${directory}/report.json`,JSON.stringify(report,null,2));return report;
}
