// Full application journey. Called by smoke_integrated_wasm with an AMD/hardware
// adapter, never used as a software-GPU performance claim.
import assert from 'node:assert/strict';
import {writeFile,mkdir} from 'node:fs/promises';
export async function validateWorld(call, directory){
    await mkdir(directory,{recursive:true});
    const evaluate=async expression=>{const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));return r.result?.value;};
    const sendKey=(value,down)=>call('Input.dispatchKeyEvent',{type:down?'keyDown':'keyUp',key:value===119?'F8':String.fromCharCode(value).toLowerCase(),code:value===119?'F8':`Key${String.fromCharCode(value)}`,windowsVirtualKeyCode:value,nativeVirtualKeyCode:value});
    const key=async code=>{await sendKey(code,true);await new Promise(r=>setTimeout(r,80));await sendKey(code,false);};
    const capture=async name=>{const r=await call('Page.captureScreenshot',{format:'png'});await writeFile(`${directory}/${name}.png`,Buffer.from(r.data,'base64'));};
    const sample=async(name,seconds)=>{
        const result=await evaluate(`new Promise(resolve=>{
            const frames=[],gpu=[],cpu=[];let last=performance.now(),start=last,frame=-1;
            function tick(now){if(frames.length<1800){frames.push(now-last);last=now;
                const t=JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_telemetry_json()));
                cpu.push(t.frame.cpu_ms);if(t.render_gpu.available&&t.render_gpu.frame!==frame){gpu.push(t.render_gpu);frame=t.render_gpu.frame;}
            }
            if(now-start<${seconds*1000})requestAnimationFrame(tick);else resolve({frames,cpu,gpu,heapUsedBytes:voxyModule._voxy_get_heap_used_bytes?.(),lost:globalThis.voxyDeviceLost,errors:globalThis.voxyUncapturedGpuErrors});}
            requestAnimationFrame(tick);
        })`);
        assert(!result.lost&&!result.errors.length);
        const summary=a=>{a=[...a].sort((a,b)=>a-b);return {n:a.length,p50:a[Math.floor(a.length*.5)],p95:a[Math.floor(a.length*.95)],p99:a[Math.floor(a.length*.99)],over33ms:a.filter(x=>x>33.3).length,max:a.at(-1)};};
        result.summary={presentation:summary(result.frames),cpu:summary(result.cpu),gpu:summary(result.gpu.map(x=>x.gpu_frame_ms))};
        await writeFile(`${directory}/${name}.json`,JSON.stringify(result,null,2));return result.summary;
    };
    const results={device:await evaluate('window.voxyDeviceProfile'),viewport:await evaluate('({width:innerWidth,height:innerHeight,dpr:devicePixelRatio})'),note:'rAF measures browser presentation cadence; GPU timestamps measure GPU execution; CPU telemetry measures application work. These are different clocks.'};
    results.idle=await sample('idle',12);await capture('shoreline');
    await sendKey(87,true);results.walk=await sample('walk',10);await sendKey(87,false);await capture('walking');
    const grouped=()=>evaluate('JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_lego_hud_json())).grouped');
    await key(75);assert.equal(await grouped(),false);await capture('single');
    await key(75);assert.equal(await grouped(),true);await capture('grouped');
    await key(119); // F8: fly before explicit world-space teleports.
    for(const [name,pose] of [
        ['inland',[-1164,-177,3418,0,-.6]],
        ['shore-overview',[-1140,-155,3430,3.14,-.65]],
        ['cliff',[179,140,-29,-1.4,-.4]],
        ['cliff-close',[2815,-25,3299,-1.5707963,-.15]],
        ['distant',[0,700,0,2.1,-.55]],
        ['teleport-back',[-1164,-184,3398,2.92,-.5]],
    ]){
        await evaluate(`voxyModule._voxy_set_camera_pose(${pose.join(',')})`);
        results[name]=await sample(name,6);await capture(name);
    }
    await writeFile(`${directory}/summary.json`,JSON.stringify(results,null,2));return results;
}
