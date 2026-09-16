#!/usr/bin/env node
// Real startup, not a throughput benchmark. Node 22+, Chrome and a directory
// with web assets and built voxy_wasm.{js,wasm,data}. Each scene has its own
// browser so closing a busy software-GPU tab cannot poison the next fixture.
import assert from 'node:assert/strict';
import {readFile,writeFile,mkdtemp,rm} from 'node:fs/promises';
import {spawn} from 'node:child_process';
import http from 'node:http';
import path from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=path.resolve(process.argv[2]||'smoke-web');
const selected=process.argv[3];
if(!selected){
    const reports=[];
    for(const experience of ['default','lego-world','lego','terrain','ridgebreak','salvage']){
        const output=path.resolve(`startup-${experience}-report.json`);
        const child=spawn(process.execPath,[fileURLToPath(import.meta.url),root,experience],
            {stdio:'inherit',env:{...process.env,VOXY_SMOKE_REPORT:output}});
        const code=await new Promise((r,j)=>{child.on('error',j);child.on('close',r);});
        try{reports.push(JSON.parse(await readFile(output,'utf8')));}catch{}
        if(code!==0){await writeFile('integrated-startup-report.json',JSON.stringify({status:'failed',reports},null,2));process.exit(1);}
    }
    await writeFile('integrated-startup-report.json',JSON.stringify({status:'passed',reports},null,2));
    process.exit(0);
}
assert(['default','lego-world','lego','terrain','ridgebreak','salvage','salvage-asset','salvage-materials','salvage-material-detail','salvage-cove','salvage-assembly','salvage-hierarchy','salvage-rotations-a','salvage-rotations-b','salvage-kit-narrow','salvage-kit-broad','salvage-kit-cargo','presentation'].includes(selected),'unknown scene');
const isWorld=selected==='default'||selected==='lego-world';
const isLego=isWorld||selected==='lego';
const isSalvageRotations=selected==='salvage-rotations-a'||selected==='salvage-rotations-b';
const isSalvageKit=['salvage-kit-narrow','salvage-kit-broad','salvage-kit-cargo'].includes(selected);
const isSalvageMaterials=selected==='salvage-materials'||selected==='salvage-material-detail';
const isSalvageAsset=selected==='salvage-cove'||isSalvageMaterials||isSalvageKit||isSalvageRotations||selected==='salvage-asset'||selected==='salvage-assembly'||selected==='salvage-hierarchy';
const isSalvage=selected==='salvage'||isSalvageAsset;
const memoryEnabled=process.env.VOXY_SMOKE_MEMORY==='1';
const memoryIntervalMs=Number(process.env.VOXY_SMOKE_MEMORY_INTERVAL_MS||200);
const memoryCapacity=Number(process.env.VOXY_SMOKE_MEMORY_SAMPLES||2048);
if(memoryEnabled){
    assert(Number.isFinite(memoryIntervalMs)&&memoryIntervalMs>=100&&memoryIntervalMs<=250,'memory interval must be 100..250 ms');
    assert(Number.isInteger(memoryCapacity)&&memoryCapacity>=1&&memoryCapacity<=10000,'memory samples must be 1..10000');
}
const directory=process.env.VOXY_SMOKE_PROFILE?path.resolve(process.env.VOXY_SMOKE_PROFILE)
    :await mkdtemp(path.join(tmpdir(),'voxys-startup-'));
const profileMarker=path.join(directory,'voxys-smoke-profile');
if(process.env.VOXY_SMOKE_PROFILE)assert.equal(await readFile(profileMarker,'utf8'),'isolated-voxys-smoke-v1');
else await writeFile(profileMarker,'isolated-voxys-smoke-v1');
// An explicitly supplied profile belongs to the caller and may contain older
// test worlds. Only profiles created by this invocation are disposable.
const retainedProfile=Boolean(process.env.VOXY_SMOKE_PROFILE)||process.env.VOXY_SMOKE_KEEP_PROFILE==='1';
const listenPort=Number(process.env.VOXY_SMOKE_PORT||0);
assert(Number.isInteger(listenPort)&&(listenPort===0||(listenPort>=1024&&listenPort<=65535)));
const delay=ms=>new Promise(r=>setTimeout(r,ms));
const mime={'.html':'text/html','.js':'text/javascript','.mjs':'text/javascript','.css':'text/css','.wasm':'application/wasm','.data':'application/octet-stream'};
const server=http.createServer(async(req,res)=>{
    try{
        const requested=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
        if(requested==='/__presentation.html'){
            res.writeHead(200,{'Content-Type':'text/html'});
            res.end(`<!doctype html><canvas width="960" height="540"></canvas><script>
                (async()=>{
                    const a=await navigator.gpu.requestAdapter();
                    const d=await a.requestDevice();
                    d.lost.then(i=>{console.error(i.reason,i.message);globalThis.probe={lost:i.message};});
                    const c=document.querySelector('canvas').getContext('webgpu');
                    c.configure({device:d,format:'bgra8unorm',usage:GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.COPY_SRC});
                    for(let i=0;i<16;i++){
                        const e=d.createCommandEncoder();
                        const p=e.beginRenderPass({colorAttachments:[{view:c.getCurrentTexture().createView(),loadOp:'clear',storeOp:'store',clearValue:{r:.1,g:.6,b:.3,a:1}}]});
                        p.end();d.queue.submit([e.finish()]);await d.queue.onSubmittedWorkDone();
                        await new Promise(r=>setTimeout(r,50));
                    }
                    globalThis.probe={passed:true,adapter:{vendor:a.info.vendor,architecture:a.info.architecture}};
                })().catch(e=>{console.error(e);globalThis.probe={error:String(e)};});
            </script>`);return;
        }
        // The unmodified main page enables local telemetry automatically.
        if(requested==='/api/telemetry'&&req.method==='POST'){
            req.resume();res.writeHead(204);res.end();return;
        }
        const filename=path.resolve(root,'.'+(requested==='/'?'/index.html':requested));
        if(!filename.startsWith(root+path.sep)){res.writeHead(403);res.end();return;}
        const data=await readFile(filename);
        res.writeHead(200,{'Content-Type':mime[path.extname(filename)]||'application/octet-stream'});res.end(data);
    }catch{res.writeHead(404);res.end();}
});
await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(listenPort,'127.0.0.1',resolve);});
const gpuFlags=process.env.VOXY_SMOKE_GPU==='gaming'
    ? ['--ozone-platform=wayland','--enable-features=Vulkan']
    : process.env.VOXY_SMOKE_GPU==='gaming-x11'
    ? ['--ozone-platform=x11','--enable-features=Vulkan']
    : process.env.VOXY_SMOKE_GPU==='swiftshader-window'
    ? ['--ozone-platform=x11','--use-webgpu-adapter=swiftshader','--use-angle=swiftshader','--enable-features=Vulkan','--use-vulkan=swiftshader']
    : process.env.VOXY_SMOKE_GPU==='swiftshader-legacy'
    ? ['--use-webgpu-adapter=swiftshader','--use-gpu-in-tests','--enable-accelerated-2d-canvas']
    : process.env.VOXY_SMOKE_GPU==='swiftshader'
    ? ['--use-webgpu-adapter=swiftshader','--use-angle=swiftshader','--enable-features=Vulkan','--use-vulkan=swiftshader','--disable-vulkan-surface']
    : ['--use-angle=vulkan','--enable-features=Vulkan','--use-vulkan=native','--disable-vulkan-surface'];
const chrome=spawn(process.env.VOXY_TEST_CHROME||'google-chrome',[
    ...(['gaming','gaming-x11','swiftshader-window'].includes(process.env.VOXY_SMOKE_GPU)?[]:['--headless=new']),'--no-sandbox','--no-first-run','--no-default-browser-check',
    '--disable-background-networking','--disable-dev-shm-usage','--enable-unsafe-webgpu','--enable-unsafe-swiftshader',
    '--disable-gpu-watchdog','--disable-background-timer-throttling','--disable-renderer-backgrounding',...gpuFlags,
    '--remote-debugging-port=0',`--user-data-dir=${directory}`,'about:blank'
],{stdio:['ignore','ignore','pipe']});
let logs='',spawnError,socket,chromeClosed=false;
chrome.once('close',()=>{chromeClosed=true;});
const browserErrors=[],consoleMessages=[];
const diagnostics={allocationSemantics:'cumulative creation calls/requested buffer bytes; not live memory',allocations:{buffers:0,textures:0,bufferBytes:0},destroyCalls:[]};
chrome.stderr.on('data',d=>logs+=d);chrome.on('error',e=>spawnError=e);
const report={kind:'application startup, no FPS acceptance',experience:selected,gpuMode:process.env.VOXY_SMOKE_GPU||'hardware',flags:gpuFlags};
let memoryObserver,memoryObserverClosed,memoryObserverError,memoryObserverStderr='';
let memoryTimer,memoryActive=false,memoryBusy=false,memoryPhase='browser-startup';
const memoryOutput=path.join(directory,'process-memory.json');
const memoryPhaseFile=path.join(directory,'memory-phase.txt');
const memorySamples=[],memoryPeaks={},memoryErrors=[];
let memorySampleCount=0,memoryErrorCount=0;
const setMemoryPhase=async phase=>{
    memoryPhase=phase;
    await writeFile(memoryPhaseFile,phase);
};
const recordMemoryError=error=>{
    memoryErrorCount++;
    if(memoryErrors.length<32)memoryErrors.push(String(error));
};
const startBrowserMemory=call=>{
    memoryActive=true;
    const sample=async()=>{
        if(!memoryActive||memoryBusy)return;
        memoryBusy=true;
        const started=Date.now(),phase=memoryPhase;
        try{
            const [heapOutcome,wasmOutcome]=await Promise.allSettled([
                call('Runtime.getHeapUsage'),
                call('Runtime.evaluate',{returnByValue:true,expression:`(() => {
                    if(typeof voxyModule==='undefined'||!voxyModule)return null;
                    return {linear_capacity_bytes:voxyModule.HEAPU8?.byteLength??null,
                        allocator_used_bytes:voxyModule._voxy_get_heap_used_bytes?.()??null};
                })()`}),
            ]);
            if(!memoryActive)return;
            const heap=heapOutcome.status==='fulfilled'?heapOutcome.value:{};
            let result=wasmOutcome.status==='fulfilled'?wasmOutcome.value:{};
            if(heapOutcome.status==='rejected')recordMemoryError('JS heap: '+String(heapOutcome.reason));
            if(wasmOutcome.status==='rejected')recordMemoryError('WASM heap: '+String(wasmOutcome.reason));
            if(result.exceptionDetails){
                recordMemoryError('WASM heap: '+(result.exceptionDetails.text||'sampling failed'));
                result={};
            }
            const values={js_used_bytes:heap.usedSize??null,js_allocated_bytes:heap.totalSize??null,
                js_embedder_heap_used_bytes:heap.embedderHeapUsedSize??null,
                js_backing_storage_bytes:heap.backingStorageSize??null,
                wasm_linear_capacity_bytes:result.result?.value?.linear_capacity_bytes??null,
                wasm_allocator_used_bytes:result.result?.value?.allocator_used_bytes??null};
            const observation={started_unix_ms:started,completed_unix_ms:Date.now(),phase,...values};
            memorySampleCount++;
            if(memorySamples.length===memoryCapacity)memorySamples.shift();
            memorySamples.push(observation);
            for(const [key,value] of Object.entries(values)){
                if(value!==null&&Number.isFinite(value)&&(!memoryPeaks[key]||value>memoryPeaks[key].bytes)){
                    memoryPeaks[key]={bytes:value,completed_unix_ms:observation.completed_unix_ms,phase};
                }
            }
        }catch(error){if(memoryActive)recordMemoryError(error);}
        finally{memoryBusy=false;}
    };
    memoryTimer=setInterval(()=>{void sample();},memoryIntervalMs);
    void sample();
};
const timeoutMs=Number(process.env.VOXY_SMOKE_TIMEOUT_MS||240000);
assert(Number.isInteger(timeoutMs)&&timeoutMs>=60000&&timeoutMs<=1800000,'smoke timeout must be 60..1800 seconds');
report.timeout_ms=timeoutMs;
const timer=setTimeout(()=>{report.timeout_expired=true;chrome.kill('SIGKILL');},timeoutMs);
try{
    if(memoryEnabled){
        report.memory_instrumented=true;
        report.kind='instrumented memory observation; timing is not an uninstrumented performance baseline';
        assert(chrome.pid,'Chrome did not start for memory observation');
        await setMemoryPhase('browser-startup');
        memoryObserver=spawn(process.env.PYTHON||'python3',[
            path.join(path.dirname(fileURLToPath(import.meta.url)),'salvage_memory_probe.py'),
            '--root-pid',String(chrome.pid),'--output',memoryOutput,
            '--interval',String(memoryIntervalMs/1000),'--max-samples',String(memoryCapacity),
            '--phase-file',memoryPhaseFile,
        ],{stdio:['ignore','pipe','pipe']});
        memoryObserverClosed=new Promise(resolve=>{
            memoryObserver.once('close',(code,signal)=>resolve({code,signal}));
            memoryObserver.once('error',error=>{memoryObserverError=String(error);resolve({error:String(error)});});
        });
        memoryObserver.stderr.on('data',data=>{memoryObserverStderr=(memoryObserverStderr+data).slice(-8192);});
        const ready=new Promise((resolve,reject)=>{
            let line='';
            memoryObserver.stdout.on('data',data=>{
                line=(line+data).slice(-8192);
                if(line.includes('\n')){
                    try{assert.equal(JSON.parse(line.split('\n')[0]).ready,true);resolve();}
                    catch(error){reject(error);}
                }
            });
        });
        await Promise.race([ready,
            memoryObserverClosed.then(outcome=>{throw Error('Memory observer exited before ready: '+JSON.stringify(outcome)+' '+memoryObserverStderr);}),
            delay(5000).then(()=>{throw Error('Memory observer startup timed out');}),
        ]);
    }
    let port;
    for(let i=0;i<1200&&!port;++i){
        if(spawnError)throw spawnError;
        if(chrome.exitCode!==null||chrome.signalCode!==null)
            throw new Error(`Chrome exited before debugging was ready (${chrome.exitCode??chrome.signalCode}): ${logs}`);
        // A retained profile keeps the previous process's DevToolsActivePort.
        // Only use the endpoint announced by this newly spawned child.
        const announced=logs.match(/DevTools listening on ws:\/\/127\.0\.0\.1:(\d+)\//);
        if(announced)port=Number(announced[1]);else await delay(50);
    }
    assert(port,'Chrome debugging unavailable');
    const target=await(await fetch(`http://127.0.0.1:${port}/json/new?about:blank`,{method:'PUT'})).json();
    socket=new WebSocket(target.webSocketDebuggerUrl);
    const pending=new Map();let id=0;
    socket.addEventListener('message',e=>{const m=JSON.parse(e.data),p=pending.get(m.id);
        if(m.method==='Runtime.consoleAPICalled')consoleMessages.push(m.params);
        if(m.method==='Runtime.exceptionThrown')browserErrors.push(m.params.exceptionDetails.exception?.description||m.params.exceptionDetails.text);
        if(m.method==='Runtime.consoleAPICalled'&&m.params.type==='error')browserErrors.push(m.params.args.map(a=>a.value??a.description??'').join(' '));
        if(p){pending.delete(m.id);m.error?p.reject(new Error(JSON.stringify(m.error))):p.resolve(m.result);}});
    socket.addEventListener('close',()=>{for(const p of pending.values())p.reject(new Error('Chrome closed'));pending.clear();});
    await new Promise((r,j)=>{socket.addEventListener('open',r,{once:true});socket.addEventListener('error',j,{once:true});});
    const call=(method,params={})=>new Promise((resolve,reject)=>{
        if(socket.readyState!==WebSocket.OPEN){reject(new Error(`Chrome connection is closed: ${method}`));return;}
        const n=++id;
        const timeout=setTimeout(()=>{pending.delete(n);reject(new Error(`Chrome request timed out: ${method}`));},30000);
        pending.set(n,{
            resolve:value=>{clearTimeout(timeout);resolve(value);},
            reject:error=>{clearTimeout(timeout);reject(error);},
        });
        try{socket.send(JSON.stringify({id:n,method,params}));}
        catch(error){pending.get(n)?.reject(error);pending.delete(n);}
    });
    await call('Page.enable');await call('Runtime.enable');await call('Network.enable');
    if(memoryEnabled)startBrowserMemory(call);
    report.browser=await call('Browser.getVersion');
    await call('Page.addScriptToEvaluateOnNewDocument',{source:`(() => {
        globalThis.voxyStartupDiagnostics=${JSON.stringify(diagnostics)};
        const d=globalThis.voxyStartupDiagnostics;
        for(const name of ['createBuffer','createTexture','destroy']){
            const original=GPUDevice.prototype[name];
            GPUDevice.prototype[name]=function(descriptor){
                if(name==='destroy')d.destroyCalls.push(new Error('GPUDevice.destroy').stack);
                else if(name==='createBuffer'){d.allocations.buffers++;d.allocations.bufferBytes+=descriptor.size;}
                else d.allocations.textures++;
                return original.apply(this,arguments);
            };
        }
    })();`});
    await call('Network.setBlockedURLs',{urls:['*googletagmanager.com*','*google-analytics.com*']});
    await call('Emulation.setDeviceMetricsOverride',{width:Number(process.env.VOXY_SMOKE_WIDTH)||((isLego||isSalvage)?960:320),height:Number(process.env.VOXY_SMOKE_HEIGHT)||((isLego||isSalvage)?540:240),deviceScaleFactor:1,mobile:false});
    // Allows a previously compiled integration artifact to exercise newer WGSL
    // without a C++ rebuild. It must be explicitly requested and is reported.
    if(process.env.VOXY_SMOKE_SHADER_DIR){
        const overrides={};
        for(const name of ['ray_blit.wgsl','water_clipmap.wgsl'])overrides[name]=await readFile(path.join(process.env.VOXY_SMOKE_SHADER_DIR,name),'utf8');
        await call('Page.addScriptToEvaluateOnNewDocument',{source:`(() => {
            const files=${JSON.stringify(overrides)};const original=GPUDevice.prototype.createShaderModule;
            GPUDevice.prototype.createShaderModule=function(d){const code=files[d.label];return original.call(this,code?{...d,code}:d);};
        })();`});
        report.shader_source_override=path.resolve(process.env.VOXY_SMOKE_SHADER_DIR);
    }
    // Exercise the user's normal URL and capacity, without benchmark/self-test
    // shortcuts that skip Application::init or replace the chosen scene.
    const base=`http://127.0.0.1:${server.address().port}`;
    const localUrl=selected==='presentation'?`${base}/__presentation.html`:selected==='default'?`${base}/`:
        `${base}/index.html?experience=${selected}`;
    const resumeWorld=process.env.VOXY_SMOKE_RESUME_WORLD;
    if(resumeWorld)assert(selected==='salvage-cove'&&/^[0-9a-f]{32}$/.test(resumeWorld));
    const url=process.env.VOXY_SMOKE_URL||(localUrl+(resumeWorld?'&world='+resumeWorld:''));
    report.url=url;
    if(retainedProfile)report.retained_profile=directory;
    const navigationStarted=Date.now();
    if(memoryEnabled)await setMemoryPhase('cold-startup');
    await call('Page.navigate',{url});
    await call('Page.bringToFront');
    await call('Emulation.setFocusEmulationEnabled',{enabled:true});
    if(selected==='presentation'){
        const start=Date.now();
        while(Date.now()-start<30000){
            const r=await call('Runtime.evaluate',{expression:'globalThis.probe',returnByValue:true});
            report.probe=r.result?.value;
            if(report.probe)break;
            await delay(100);
        }
        assert.equal(report.probe?.passed,true,JSON.stringify(report.probe));
        assert.equal(browserErrors.length,0,browserErrors.join('\n'));
        report.status='passed';
    }else{
    let sample;const started=Date.now();
    while(Date.now()-started<180000){
        assert.equal(browserErrors.length,0,browserErrors.join('\n'));
        const r=await call('Runtime.evaluate',{returnByValue:true,expression:`(() => {
            const error=document.getElementById('error');
            if(error&&getComputedStyle(error).display!=='none')throw new Error(error.textContent);
            if(typeof voxyModule==='undefined'||!voxyModule?._voxy_is_initialized?.())return null;
            const pointer=voxyModule._voxy_get_telemetry_json();const moto=voxyModule._voxy_get_moto_hud_json?.();
            return {telemetry:JSON.parse(voxyModule.UTF8ToString(pointer)),
                moto:moto?JSON.parse(voxyModule.UTF8ToString(moto)):null,
                errors:globalThis.voxyUncapturedGpuErrors||[],lost:globalThis.voxyDeviceLost,
                adapter:window.voxyDeviceProfile?.adapter,title:document.title,buildId:window.voxyBuildId,
                heapBytes:voxyModule.HEAPU8?.byteLength,
                heapUsedBytes:voxyModule._voxy_get_heap_used_bytes?.(),
                loadingVisible:getComputedStyle(document.getElementById('loading')).display!=='none',
                legoControlsVisible:document.getElementById('lego-shore-controls')?.hidden===false,
                salvageControlsVisible:document.getElementById('salvage-preview')?.hidden===false,
                salvage:voxyModule._voxy_get_salvage_preview_json
                    ? JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json())) : null};
        })()`});
        if(r.exceptionDetails)throw new Error(JSON.stringify(r.exceptionDetails));
        sample=r.result?.value;report.sample=sample;
        if(sample?.errors?.length||sample?.lost)throw new Error('GPU device error: '+JSON.stringify(sample.lost||sample.errors));
        // Passing the eight-frame queue limit requires a completion callback.
        // A timestamp sample additionally proves that GPU work/readback retired.
        if(sample?.telemetry?.frame?.count>=12&&sample.telemetry.render_gpu?.available&&!sample.loadingVisible)break;
        await delay(500);
    }
    assert(sample?.telemetry?.frame?.count>=12&&sample.telemetry.render_gpu?.available,'GPU did not retire startup frames');
    assert.equal(sample.loadingVisible,false,'loading overlay still covers the application');
    if(process.env.VOXY_SMOKE_PRESENTATION_PARTS!==undefined)
        assert.equal(sample.salvage?.assetFixture?.presentationParts,Number(process.env.VOXY_SMOKE_PRESENTATION_PARTS),'active presentation parts');
    assert.equal(sample.telemetry.physics.backend,'webgpu_soft');
    assert.equal(sample.telemetry.render_gpu.frame_interval_available,true);
    assert(sample.telemetry.render_gpu.gpu_frame_ms>0,'missing complete-frame timestamp');
    assert(sample.telemetry.render_gpu.render_width>0&&sample.telemetry.render_gpu.render_height>0);
    const budget=await call('Runtime.evaluate',{returnByValue:true,
        expression:`(() => {const p=voxyModule._voxy_get_telemetry_json();
            const t=JSON.parse(voxyModule.UTF8ToString(p));
            return {api:typeof voxyMeasureGpuBudget,summary:VoxyFrameBudget.summarize([t.render_gpu])};})()`});
    if(budget.exceptionDetails)throw new Error(JSON.stringify(budget.exceptionDetails));
    assert.equal(budget.result.value.api,'function');
    assert.equal(budget.result.value.summary.status,'insufficient_samples');
    assert.equal(budget.result.value.summary.sample_count,1);
    report.budget_single_sample=budget.result.value.summary;
    if(process.env.VOXY_SMOKE_BUILD_ID)assert.equal(sample.buildId,process.env.VOXY_SMOKE_BUILD_ID,'deployed revision mismatch');
    assert.equal(Boolean(sample.moto?.active),selected==='ridgebreak','experience activation mismatch');
    assert.equal(Boolean(sample.salvage?.active),isSalvage,'salvage activation mismatch');
    assert.equal(sample.salvageControlsVisible,isSalvage,'salvage UI route mismatch');
    if(isSalvage){
        assert.equal(sample.title,selected==='salvage-cove'?'Salvage Cove — Voxys':isSalvageMaterials?'Material Inspection — Voxys':isSalvageKit?'Salvage Kit Inspection — Voxys':isSalvageRotations?'Assembly Rotations — Voxys':selected==='salvage-hierarchy'?'Hierarchy Inspection — Voxys':selected==='salvage-assembly'?'Pontoon Assembly Check — Voxys':isSalvageAsset?'Pontoon Inspection — Voxys':'Cove Preview — Voxys');
        assert.equal(sample.salvage.ready,true);
        assert.equal(sample.salvage.failed,false);
        if(isSalvageAsset)assert.equal(sample.salvage.bodies,0,'asset inspector must not spawn cove scenery');
        else assert(sample.salvage.bodies>0&&sample.salvage.bodies<=64,'preview body ownership is unbounded or empty');
        assert.equal(sample.legoControlsVisible,false);
        assert.equal(sample.telemetry.render.terrain_width,256);
        assert.equal(sample.telemetry.render.terrain_height,256);
        assert.equal(sample.telemetry.render.terrain_mips,9);
        assert.equal(sample.heapBytes,512*1024*1024,'fixed WASM memory budget changed');
    }
    if(isLego){
        assert.equal(sample.title,isWorld?'LEGO Landscape — Voxys':'LEGO Shore — Voxys');
        assert.equal(sample.legoControlsVisible,false,'playground toolbar should be hidden by default');
        assert.equal(sample.telemetry.render.terrain_width,isWorld?8192:256,'LEGO source was upscaled');
        assert.equal(sample.telemetry.render.terrain_height,isWorld?8192:256,'LEGO source was upscaled');
        assert.equal(sample.telemetry.render.terrain_mips,isWorld?14:9);
        assert.equal(sample.heapBytes,512*1024*1024,'fixed WASM memory budget changed');
    }
    assert.equal(browserErrors.length,0,browserErrors.join('\n'));
    if(process.env.VOXY_SMOKE_NO_SCREENSHOT!=='1' && !process.env.VOXY_SMOKE_COVE_CONTINUE && !process.env.VOXY_SMOKE_COVE_BUILDER_TOOLS && !process.env.VOXY_SMOKE_COVE_EFFECTS && !process.env.VOXY_SMOKE_COVE_UI){
    const screenshot=await call('Page.captureScreenshot',{format:'png'});
    const screenshotPath=process.env.VOXY_SMOKE_SCREENSHOT||`startup-${selected}.png`;
    await writeFile(screenshotPath,Buffer.from(screenshot.data,'base64'));
    const check=spawn(process.env.PYTHON||'python3',[path.join(path.dirname(fileURLToPath(import.meta.url)),'check_startup_image.py'),screenshotPath],{stdio:'inherit'});
    const imageCode=await new Promise((resolve,reject)=>{check.on('error',reject);check.on('close',resolve);});
    assert.equal(imageCode,0,'main page has no visible landscape');
    report.screenshot=screenshotPath;
    }
    report.startupElapsedMs=Date.now()-navigationStarted;
    if(memoryEnabled){
        await setMemoryPhase('steady-startup');
        await delay(2000);
    }
    if(process.env.VOXY_SMOKE_JOURNEY){
        if(memoryEnabled)await setMemoryPhase('world-journey');
        const {validateWorld}=await import('./validate_lego_world.mjs');
        report.journey=await validateWorld(call,process.env.VOXY_SMOKE_JOURNEY);
    }
    if(process.env.VOXY_SMOKE_PLAYGROUND){
        if(memoryEnabled)await setMemoryPhase('playground-journey');
        const {validatePlayground}=await import('./validate_lego_playground.mjs');
        report.playground=await validatePlayground(call,process.env.VOXY_SMOKE_PLAYGROUND);
    }
    if(process.env.VOXY_SMOKE_SALVAGE){
        assert(selected==='salvage','salvage journey requires the salvage route');
        if(memoryEnabled)await setMemoryPhase('salvage-journey');
        const {validateSalvagePreview}=await import('./validate_salvage_preview.mjs');
        report.salvage_journey=await validateSalvagePreview(call,process.env.VOXY_SMOKE_SALVAGE);
    }
    if(process.env.VOXY_SMOKE_ASSET_FIXTURE){
        assert(isSalvageAsset,'asset fixture journey requires the salvage-asset route');
        const {validateSalvageAssetFixture}=await import('./validate_salvage_asset_fixture.mjs');
        report.asset_fixture_journey=await validateSalvageAssetFixture(call,process.env.VOXY_SMOKE_ASSET_FIXTURE);
    }
    if(process.env.VOXY_SMOKE_COVE_EFFECTS){
        assert.equal(selected,'salvage-cove');
        let finishVisual,beginVisual;
        if(process.env.VOXY_SMOKE_VISUAL_CAPTURE){
            const {beginCoveVisualCapture}=await import('./record_cove_visual.mjs');
            beginVisual=async()=>{finishVisual=await beginCoveVisualCapture(call,process.env.VOXY_SMOKE_VISUAL_CAPTURE);};
        }
        const {validateCoveEffects}=await import('./validate_cove_effects.mjs');
        let journeyError;
        try { report.cove_effects=await validateCoveEffects(call,process.env.VOXY_SMOKE_COVE_EFFECTS,beginVisual); }
        catch(error){journeyError=error;throw error;}
        finally {
            if(finishVisual)try { report.cove_visual=await finishVisual(); }
            catch(error){if(journeyError)report.visual_capture_error=String(error);else throw error;}
        }
    }
    if(process.env.VOXY_SMOKE_COVE_CHARACTER){
        assert.equal(selected,'salvage-cove');
        const {validateCoveCharacter}=await import('./validate_cove_character.mjs');
        report.cove_character=await validateCoveCharacter(call,process.env.VOXY_SMOKE_COVE_CHARACTER);
    }
    if(process.env.VOXY_SMOKE_COVE_HARBOR){
        assert.equal(selected,'salvage-cove');
        const {validateCoveHarbor}=await import('./validate_cove_harbor.mjs');
        report.cove_harbor=await validateCoveHarbor(call,process.env.VOXY_SMOKE_COVE_HARBOR,Boolean(process.env.VOXY_SMOKE_RESUME_WORLD));
    }
    if(process.env.VOXY_SMOKE_COVE_RECOVERY_DESIGN){
        assert.equal(selected,'salvage-cove');
        const {validateCoveRecoveryDesign}=await import('./validate_cove_recovery_design.mjs');
        report.cove_recovery_design=await validateCoveRecoveryDesign(call,process.env.VOXY_SMOKE_COVE_RECOVERY_DESIGN,Boolean(process.env.VOXY_SMOKE_RESUME_WORLD));
    }
    if(process.env.VOXY_SMOKE_COVE_STARTER){
        assert.equal(selected,'salvage-cove');
        const {validateCoveStarter}=await import('./validate_cove_starter.mjs');
        report.cove_starter=await validateCoveStarter(call,process.env.VOXY_SMOKE_COVE_STARTER,Boolean(process.env.VOXY_SMOKE_RESUME_WORLD));
    }
    if(process.env.VOXY_SMOKE_COVE_CUT){
        assert.equal(selected,'salvage-cove');
        const {validateCoveCut}=await import('./validate_cove_cut.mjs');
        report.cove_cut=await validateCoveCut(call,process.env.VOXY_SMOKE_COVE_CUT,Boolean(process.env.VOXY_SMOKE_RESUME_WORLD));
    }
    if(process.env.VOXY_SMOKE_COVE_RESCUE){
        assert.equal(selected,'salvage-cove');
        const {validateCoveRescue}=await import('./validate_cove_rescue.mjs');
        report.cove_rescue=await validateCoveRescue(call,process.env.VOXY_SMOKE_COVE_RESCUE,Boolean(process.env.VOXY_SMOKE_RESUME_WORLD),
            process.env.VOXY_SMOKE_COVE_RESCUE_CHECKPOINT_ONLY==='1');
    }
    if(process.env.VOXY_SMOKE_COVE_DELIVERY){
        assert.equal(selected,'salvage-cove');
        const {validateCoveDelivery}=await import('./validate_cove_delivery.mjs');
        report.cove_delivery=await validateCoveDelivery(call,process.env.VOXY_SMOKE_COVE_DELIVERY);
    }
    if(process.env.VOXY_SMOKE_COVE_SAVES){
        assert.equal(selected,'salvage-cove');
        const {validateCoveSaves}=await import('./validate_cove_saves.mjs');
        report.cove_saves=await validateCoveSaves(call,process.env.VOXY_SMOKE_COVE_SAVES,Boolean(process.env.VOXY_SMOKE_RESUME_WORLD));
    }
    if(process.env.VOXY_SMOKE_COVE_ARCHIVE_ONLY){
        assert.equal(selected,'salvage-cove');
        const {validateCoveArchiveJourney}=await import('./validate_cove_archive.mjs');
        report.cove_archive=await validateCoveArchiveJourney(call,process.env.VOXY_SMOKE_COVE_ARCHIVE_ONLY);
    }
    if(process.env.VOXY_SMOKE_COVE_DESIGNS){
        assert.equal(selected,'salvage-cove');
        const {validateDesignStorage}=await import('./validate_design_storage.mjs');
        report.design_storage=await validateDesignStorage(call);
        const {validateCoveDesigns}=await import('./validate_cove_designs.mjs');
        report.cove_designs=await validateCoveDesigns(call,process.env.VOXY_SMOKE_COVE_DESIGNS);
    }
    if(process.env.VOXY_SMOKE_COVE_SETTINGS){
        assert.equal(selected,'salvage-cove');
        const {validateCoveSettings}=await import('./validate_cove_settings.mjs');
        report.cove_settings=await validateCoveSettings(call,process.env.VOXY_SMOKE_COVE_SETTINGS);
    }
    if(process.env.VOXY_SMOKE_WORKSHOP_CAMERA){
        const {validateWorkshopCamera}=await import('./validate_workshop_camera.mjs');
        report.workshop_camera=await validateWorkshopCamera(call,process.env.VOXY_SMOKE_WORKSHOP_CAMERA);
    }
    if(process.env.VOXY_SMOKE_COVE_BRICKS){
        assert.equal(selected,'salvage-cove');
        const {validateCoveBricks}=await import('./validate_cove_bricks.mjs');
        report.cove_bricks=await validateCoveBricks(call,process.env.VOXY_SMOKE_COVE_BRICKS);
    }
    if(process.env.VOXY_SMOKE_COVE_CONTINUE){
        assert.equal(selected,'salvage-cove');
        assert(process.env.VOXY_SMOKE_RESUME_WORLD,'Continue requires an existing saved world');
        assert(process.env.VOXY_SMOKE_CONTINUE_BASELINE,'Continue requires the original combined report');
        const {validateCoveContinue}=await import('./validate_cove_continue.mjs');
        report.cove_continue=await validateCoveContinue(call,process.env.VOXY_SMOKE_COVE_CONTINUE,
            {baselineReportPath:process.env.VOXY_SMOKE_CONTINUE_BASELINE,expectedPresentationParts:Number(process.env.VOXY_SMOKE_PRESENTATION_PARTS??9)});
    }
    if(process.env.VOXY_SMOKE_COVE_CARGO_COMPATIBILITY){
        assert.equal(selected,'salvage-cove');
        assert(process.env.VOXY_SMOKE_RESUME_WORLD,'cargo compatibility requires the older saved world');
        assert(process.env.VOXY_SMOKE_CARGO_BASELINE,'cargo compatibility requires the original combined report');
        const {validateCoveCargoCompatibility}=await import('./validate_cove_cargo_compatibility.mjs');
        report.cove_cargo_compatibility=await validateCoveCargoCompatibility(call,process.env.VOXY_SMOKE_COVE_CARGO_COMPATIBILITY,
            {expectedPresentationParts:Number(process.env.VOXY_SMOKE_PRESENTATION_PARTS??9),baselineReportPath:process.env.VOXY_SMOKE_CARGO_BASELINE});
    }
    if(process.env.VOXY_SMOKE_COVE_UI){
        assert.equal(selected,'salvage-cove');
        assert.equal(process.env.VOXY_SMOKE_NO_SCREENSHOT,'1','Cove UI journey requires no screenshots');
        if(process.env.VOXY_SMOKE_COVE_UI_CONTINUE_REPORT)assert(process.env.VOXY_SMOKE_RESUME_WORLD,'UI continuation requires its actual saved world');
        if(process.env.VOXY_SMOKE_COVE_UI_RESTORED_DESIGN)assert(process.env.VOXY_SMOKE_COVE_UI_CONTINUE_REPORT,'restored design proof requires the actual saved baseline');
        const {validateCoveUI}=await import('./validate_cove_ui.mjs');
        report.cove_ui=await validateCoveUI(call,process.env.VOXY_SMOKE_COVE_UI,
            {expectedPresentationParts:Number(process.env.VOXY_SMOKE_PRESENTATION_PARTS??9),
                appliedSettingsReportPath:process.env.VOXY_SMOKE_COVE_UI_APPLIED_SETTINGS||null,
                savedContinuationReportPath:process.env.VOXY_SMOKE_COVE_UI_CONTINUE_REPORT||null,
                restoredDesignReportPath:process.env.VOXY_SMOKE_COVE_UI_RESTORED_DESIGN||null});
    }
    if(process.env.VOXY_SMOKE_COVE_BUILDER_TOOLS){
        assert.equal(selected,'salvage-cove');
        assert.equal(process.env.VOXY_SMOKE_NO_SCREENSHOT,'1','builder tools journey requires no screenshots');
        const {validateCoveBuilderTools}=await import('./validate_cove_builder_tools.mjs');
        report.cove_builder_tools=await validateCoveBuilderTools(call,process.env.VOXY_SMOKE_COVE_BUILDER_TOOLS,
            {expectedPresentationParts:Number(process.env.VOXY_SMOKE_PRESENTATION_PARTS??9)});
    }
    if(process.env.VOXY_SMOKE_COVE_OBJECTIVES){
        assert.equal(selected,'salvage-cove');
        const {validateCoveObjectives}=await import('./validate_cove_objectives.mjs');
        report.cove_objectives=await validateCoveObjectives(call,process.env.VOXY_SMOKE_COVE_OBJECTIVES,
            {expectedPresentationParts:Number(process.env.VOXY_SMOKE_PRESENTATION_PARTS??7)});
    }
    if(process.env.VOXY_SMOKE_COVE_MECHANISMS){
        assert.equal(selected,'salvage-cove');
        const {validateCoveMechanisms}=await import('./validate_cove_mechanisms.mjs');
        report.cove_mechanisms=await validateCoveMechanisms(call,process.env.VOXY_SMOKE_COVE_MECHANISMS,
            {expectedPresentationParts:Number(process.env.VOXY_SMOKE_PRESENTATION_PARTS??7)});
    }
    if(process.env.VOXY_SMOKE_COVE_PAINT){
        assert.equal(selected,'salvage-cove');
        const {validateCovePaint}=await import('./validate_cove_paint.mjs');
        report.cove_paint=await validateCovePaint(call,process.env.VOXY_SMOKE_COVE_PAINT,
            {expectedPresentationParts:Number(process.env.VOXY_SMOKE_PRESENTATION_PARTS??3)});
    }
    if(process.env.VOXY_SMOKE_COVE_PARTS){
        assert.equal(selected,'salvage-cove');
        const {validateCoveParts}=await import('./validate_cove_parts.mjs');
        report.cove_parts=await validateCoveParts(call,process.env.VOXY_SMOKE_COVE_PARTS);
    }
    if(process.env.VOXY_SMOKE_COVE_LAUNCH){
        assert.equal(selected,'salvage-cove');
        const {validateCoveLaunch}=await import('./validate_cove_launch.mjs');
        report.cove_launch=await validateCoveLaunch(call,process.env.VOXY_SMOKE_COVE_LAUNCH);
    }
    if(process.env.VOXY_SMOKE_COVE_WORKSHOP){
        assert.equal(selected,'salvage-cove');
        const {validateCoveWorkshop}=await import('./validate_cove_workshop.mjs');
        report.cove_workshop=await validateCoveWorkshop(call,process.env.VOXY_SMOKE_COVE_WORKSHOP);
    }
    if(process.env.VOXY_SMOKE_COVE_SCENE_SHADOWS){
        assert.equal(selected,'salvage-cove');
        const {validateCoveSceneShadows}=await import('./validate_cove_scene_shadows.mjs');
        report.cove_scene_shadows=await validateCoveSceneShadows(call,process.env.VOXY_SMOKE_COVE_SCENE_SHADOWS);
    }
    if(process.env.VOXY_SMOKE_COVE_PLAYER){
        assert.equal(selected,'salvage-cove');
        const {validateCovePlayer}=await import('./validate_cove_player.mjs');
        report.cove_player=await validateCovePlayer(call,process.env.VOXY_SMOKE_COVE_PLAYER);
    }
    if(process.env.VOXY_SMOKE_ASSET_VIEWS){
        assert(isSalvageAsset,'asset views require the salvage-asset route');
        assert(!process.env.VOXY_SMOKE_ASSET_FIXTURE&&!process.env.VOXY_SMOKE_ASSET_LOSS,'asset views require a separate fresh run');
        const {validateSalvageAssetViews}=await import('./validate_salvage_asset_views.mjs');
        report.asset_views=await validateSalvageAssetViews(call,process.env.VOXY_SMOKE_ASSET_VIEWS,{
            guides:Number(process.env.VOXY_SMOKE_ASSET_GUIDES||0),
            views:process.env.VOXY_SMOKE_ASSET_VIEW_NAMES?.split(',')||null,
            lod:Number(process.env.VOXY_SMOKE_ASSET_LOD||0),
            recipePath:process.env.VOXY_SMOKE_ASSET_RECIPE||null});
    }
    if(process.env.VOXY_SMOKE_ASSET_MOTION){
        assert(['salvage-assembly','salvage-kit-broad','salvage-kit-narrow','salvage-kit-cargo'].includes(selected),'motion capture requires a supported asset inspection route');
        assert(!process.env.VOXY_SMOKE_ASSET_FIXTURE&&!process.env.VOXY_SMOKE_ASSET_LOSS&&!process.env.VOXY_SMOKE_ASSET_VIEWS,'motion capture requires a separate fresh run');
        const {validateSalvageAssetMotion}=await import('./validate_salvage_asset_motion.mjs');
        report.asset_motion=await validateSalvageAssetMotion(call,process.env.VOXY_SMOKE_ASSET_MOTION,{
            recipePath:process.env.VOXY_SMOKE_ASSET_RECIPE||null});
    }
    if(process.env.VOXY_SMOKE_ASSET_SECTORS){
        assert(selected==='salvage-assembly','sector flight requires the assembly inspector');
        assert(!process.env.VOXY_SMOKE_ASSET_FIXTURE&&!process.env.VOXY_SMOKE_ASSET_LOSS&&!process.env.VOXY_SMOKE_ASSET_VIEWS&&!process.env.VOXY_SMOKE_ASSET_MOTION,'sector flight requires a separate fresh run');
        const {validateSalvageAssetSectors}=await import('./validate_salvage_asset_sectors.mjs');
        report.asset_sectors=await validateSalvageAssetSectors(call,process.env.VOXY_SMOKE_ASSET_SECTORS);
    }
    assert.equal(browserErrors.length,0,browserErrors.join('\n'));
    if(process.env.VOXY_SMOKE_ASSET_LOSS){
        assert(isSalvageAsset,'asset loss test requires the salvage-asset route');
        assert(!process.env.VOXY_SMOKE_ASSET_FIXTURE,'loss test requires its own fresh browser run');
        const {validateSalvageAssetLoss}=await import('./validate_salvage_asset_loss.mjs');
        report.asset_loss=await validateSalvageAssetLoss(call,process.env.VOXY_SMOKE_ASSET_LOSS);
        // Only this dedicated, explicitly requested negative run permits the
        // actual destroyed-device notification. Preserve every message.
        const engineLoss=/^\[ERROR\] \[[0-9:.]+\] Asset fixture failed: platform reported device loss or terminal GPU failure$/;
        const unexpected=browserErrors.filter(message=>!message.startsWith('WebGPU device lost (destroyed):') && !engineLoss.test(message));
        assert.equal(unexpected.length,0,unexpected.join('\n'));
        assert.equal(browserErrors.filter(message=>message.startsWith('WebGPU device lost (destroyed):')).length,1);
        assert.equal(browserErrors.filter(message=>engineLoss.test(message)).length,1);
        report.expected_loss_messages=browserErrors;
    }
    report.status='passed';
    }
}catch(error){report.status='failed';report.error=String(error);report.chrome_log=logs;throw error;}
finally{
    if(memoryEnabled){
        memoryActive=false;
        clearInterval(memoryTimer);
        let processMemory=null,observerOutcome=null;
        if(memoryObserver){
            if(memoryObserver.exitCode===null&&memoryObserver.signalCode===null&&!memoryObserverError)memoryObserver.kill('SIGTERM');
            observerOutcome=await Promise.race([memoryObserverClosed,delay(3000).then(()=>({timeout:true}))]);
            if(observerOutcome?.timeout){memoryObserver.kill('SIGKILL');recordMemoryError('Process memory observer did not stop within 3 seconds');}
            try{processMemory=JSON.parse(await readFile(memoryOutput,'utf8'));}
            catch(error){recordMemoryError('Process memory report unavailable: '+String(error));}
        }
        const requiredBrowserMetrics=['js_used_bytes','wasm_linear_capacity_bytes','wasm_allocator_used_bytes'];
        const missingBrowserMetrics=requiredBrowserMetrics.filter(key=>!memoryPeaks[key]);
        const observerSucceeded=observerOutcome?.code===0&&!observerOutcome?.error&&!observerOutcome?.timeout;
        const memoryStatus=Object.keys(memoryPeaks).length===0&&!Object.keys(processMemory?.sampled_peaks||{}).length
            ? 'unavailable'
            : !missingBrowserMetrics.length&&!memoryErrorCount&&observerSucceeded&&processMemory?.status==='complete'
            ? 'complete':'partial';
        report.memory={status:memoryStatus,
            status_scope:'Observed JS/WASM allocator/capacity, RSS and DRM requested/resident metrics; startup status is independent',
            missing_required_browser_metrics:missingBrowserMetrics,
            kind:'sampled observations; domains must not be added together',
            interval_ms:memoryIntervalMs,sample_count:memorySampleCount,
            retained_sample_count:memorySamples.length,dropped_history_samples:memorySampleCount-memorySamples.length,
            sampled_peaks:memoryPeaks,samples:memorySamples,error_count:memoryErrorCount,errors:memoryErrors,
            process_tree:processMemory,observer_outcome:observerOutcome,observer_stderr:memoryObserverStderr,
            staging_bytes:null,pending_retirement_bytes:null,
            limitations:[
                'Memory instrumentation changes timing; run visible performance separately without VOXY_SMOKE_MEMORY.',
                'CDP heap usage is isolate-wide; ArrayBuffer backing storage may overlap the separately reported WASM capacity.',
                'WASM allocator usage is mallinfo().uordblks; fixed linear-memory capacity is not allocator live usage.',
                'Peaks are sampled. Synchronous WASM startup can delay CDP observations and hide allocator transients.',
                'GPU values are DRM client observations, not WebGPU resource-category accounting or unique physical totals.',
                'Staging and pending retirement are unavailable, not zero. Hidden driver allocations may not be attributed.',
            ],sources:['https://chromedevtools.github.io/devtools-protocol/tot/Runtime/#method-getHeapUsage',
                'https://dri.freedesktop.org/docs/drm/gpu/drm-usage-stats.html']};
    }
    report.console=consoleMessages;
    report.browserErrors=browserErrors;
    report.chrome_log=logs;
    if(socket?.readyState===WebSocket.OPEN){
        // Preserve diagnostics even when initialization never produced telemetry.
        const n=900000;
        report.startup=await Promise.race([new Promise(resolve=>{
            const listener=e=>{const m=JSON.parse(e.data);if(m.id===n){socket.removeEventListener('message',listener);resolve(m.result);}};
            socket.addEventListener('message',listener);
            socket.send(JSON.stringify({id:n,method:'Runtime.evaluate',params:{returnByValue:true,expression:`({diagnostics:globalThis.voxyStartupDiagnostics,profile:window.voxyDeviceProfile,lost:globalThis.voxyDeviceLost,memory:performance.memory?{used:performance.memory.usedJSHeapSize,total:performance.memory.totalJSHeapSize}:null})`}}));
        }),delay(3000).then(()=>({timeout:true}))]);
    }
    clearTimeout(timer);
    report.browserShutdown={retainedProfile,directory,requestedClose:false,requestedTerminate:false,forced:false,completed:chromeClosed};
    if(!chromeClosed&&!spawnError){
        let stopped=false;const closed=new Promise(resolve=>chrome.once('close',()=>{stopped=true;resolve();}));
        const waitClosed=async milliseconds=>{let timeout;await Promise.race([closed,new Promise(resolve=>{timeout=setTimeout(resolve,milliseconds);})]);clearTimeout(timeout);};
        if(retainedProfile){
            // Ask the owned browser to close normally so optional profile data
            // can flush. Sending Close is not itself proof of persisted bytes.
            if(chrome.exitCode===null&&chrome.signalCode===null&&socket?.readyState===WebSocket.OPEN){
                try{socket.send(JSON.stringify({id:900001,method:'Browser.close'}));report.browserShutdown.requestedClose=true;}
                catch(error){report.browserShutdown.closeError=String(error);}
            }
            await waitClosed(3000);
            if(!stopped&&chrome.exitCode===null&&chrome.signalCode===null){report.browserShutdown.requestedTerminate=chrome.kill('SIGTERM');await waitClosed(3000);}
        }
        if(!stopped){report.browserShutdown.forced=true;report.browserShutdown.forceSignalSent=chrome.kill('SIGKILL');await waitClosed(3000);}
        report.browserShutdown.completed=stopped;
        if(!stopped){for(const stream of chrome.stdio)stream?.destroy();chrome.unref();}
    }
    report.browserShutdown.exitCode=chrome.exitCode;report.browserShutdown.signal=chrome.signalCode;
    const normalBrowserExit=chrome.exitCode===0||(report.browserShutdown.requestedTerminate&&chrome.signalCode==='SIGTERM');
    if((!report.browserShutdown.completed||(retainedProfile&&(!normalBrowserExit||report.browserShutdown.forced||(!report.browserShutdown.requestedClose&&!report.browserShutdown.requestedTerminate))))&&report.status==='passed'){
        report.status='failed';report.error='Retained browser profile did not receive a bounded normal shutdown';
    }
    socket?.close();
    await writeFile(process.env.VOXY_SMOKE_REPORT||'integrated-startup-report.json',JSON.stringify(report,null,2)+'\n');
    await new Promise(r=>server.close(r));
    if(!retainedProfile)await rm(directory,{recursive:true,force:true,maxRetries:8,retryDelay:100});
}
console.log(JSON.stringify(report,null,2));
if(report.status!=='passed')process.exitCode=1;
