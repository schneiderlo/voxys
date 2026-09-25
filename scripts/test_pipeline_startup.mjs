// Compile fixtures/startup_pipeline_probe.cpp as documented there, then run:
// node scripts/test_pipeline_startup.mjs --chrome=PATH --build=OUTPUT_DIRECTORY
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import http from 'node:http';
import {spawn} from 'node:child_process';

const args=Object.fromEntries(process.argv.slice(2).map(arg=>{
    const at=arg.indexOf('=');return [arg.slice(2,at),arg.slice(at+1)];
}));
assert(args.chrome&&args.build,'--chrome and --build are required');
const build=path.resolve(args.build);
for(const name of ['pipeline-probe.js','pipeline-probe.wasm'])assert(fs.existsSync(path.join(build,name)),name+' is missing');
const profile=fs.mkdtempSync(path.join(os.tmpdir(),'voxys-pipeline-test-'));
const html=`<!doctype html><script src="pipeline-probe.js"></script><script>
(async()=>{
    const adapter=await navigator.gpu.requestAdapter();
    const device=await adapter.requestDevice();
    const renderDescriptors=[];
    let pendingRenders=0,maxPendingRenders=0;
    const createRender=device.createRenderPipelineAsync.bind(device);
    device.createRenderPipelineAsync=descriptor=>{
        renderDescriptors.push({entry:descriptor.vertex.entryPoint,
            format:descriptor.fragment.targets[0].format});
        maxPendingRenders=Math.max(maxPendingRenders,++pendingRenders);
        return createRender(descriptor).finally(()=>{pendingRenders--;});
    };
    const timeout=window.setTimeout;
    globalThis.progress=[];globalThis.blockedTimers=0;
    globalThis.setTimeout=(f,ms,...args)=>{
        if(globalThis.pauseTimers&&ms>0){blockedTimers++;return -1;}
        return timeout(f,ms,...args);
    };
    await PipelineProbe({preinitializedWebGPUDevice:device,setStatus(text){
        progress.push(text);
        if(text.startsWith('Preparing graphics'))globalThis.pauseTimers=true;
    }});
    // Only the test's deadline uses the original timer. Production code must
    // complete through WebGPU callbacks while its timers remain paused.
    for(let n=0;n<200&&!globalThis.pipelineProbePassed;n++)await new Promise(r=>timeout(r,50));
    globalThis.probe={passed:globalThis.pipelineProbePassed===true,blockedTimers,progress,
        renderDescriptors,maxPendingRenders,
        adapter:{vendor:adapter.info.vendor,architecture:adapter.info.architecture}};
})().catch(e=>{globalThis.probe={error:String(e)};});
</script>`;
const server=http.createServer((req,res)=>{
    const name=new URL(req.url,'http://localhost').pathname;
    if(name==='/'){res.setHeader('Content-Type','text/html');res.end(html);return;}
    if(!['/pipeline-probe.js','/pipeline-probe.wasm'].includes(name)){res.writeHead(404);res.end();return;}
    res.setHeader('Content-Type',name.endsWith('.wasm')?'application/wasm':'text/javascript');
    res.end(fs.readFileSync(path.join(build,name.slice(1))));
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const softwareFlags=args.software==='1'?['--enable-unsafe-webgpu','--enable-unsafe-swiftshader',
    '--use-webgpu-adapter=swiftshader','--use-angle=swiftshader',
    '--enable-features=Vulkan','--use-vulkan=swiftshader','--disable-vulkan-surface']:[];
const chrome=spawn(args.chrome,['--headless=new','--no-sandbox','--remote-debugging-port=0','--user-data-dir='+profile,
    '--no-first-run',...softwareFlags,'about:blank']);
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
let stderr='',spawnError,socket,call;const pending=new Map();let id=0;
chrome.stderr.on('data',data=>{stderr+=data;});chrome.on('error',error=>{spawnError=error;});
try{
    let port;
    for(let n=0;n<200;n++){
        if(spawnError)throw spawnError;
        port=stderr.match(/DevTools listening on ws:\/\/127\.0\.0\.1:(\d+)/)?.[1];
        if(port)break;await delay(100);
    }
    assert(port,stderr);
    const target=await(await fetch(`http://127.0.0.1:${port}/json/new?about:blank`,{method:'PUT'})).json();
    socket=new WebSocket(target.webSocketDebuggerUrl);
    await new Promise((resolve,reject)=>{socket.onopen=resolve;socket.onerror=reject;});
    socket.onmessage=event=>{const m=JSON.parse(event.data),p=pending.get(m.id);if(p){pending.delete(m.id);clearTimeout(p.timer);m.error?p.reject(Error(JSON.stringify(m.error))):p.resolve(m.result);}};
    call=(method,params={})=>new Promise((resolve,reject)=>{
        const n=++id,timer=setTimeout(()=>{pending.delete(n);reject(Error('Timeout: '+method));},30000);
        pending.set(n,{resolve,reject,timer});socket.send(JSON.stringify({id:n,method,params}));
    });
    await call('Page.navigate',{url:`http://127.0.0.1:${server.address().port}/`});
    let result;
    for(let n=0;n<150;n++){
        await delay(200);
        const r=await call('Runtime.evaluate',{expression:'globalThis.probe',returnByValue:true});
        result=r.result?.value;if(result)break;
    }
    console.log(JSON.stringify(result));
    assert.equal(result?.passed,true);
    assert.equal(result.blockedTimers,0);
    assert(result.progress.includes('Preparing graphics... 51 steps complete'));
    assert.equal(result.maxPendingRenders,25,'24 valid variants and a failure must be submitted before waiting');
    assert.deepEqual(result.renderDescriptors.slice(2,26),Array.from({length:24},(_,i)=>({
        entry:'vs',format:i%2===0?'rgba8unorm':'rgba16float'
    })),'the WASM bridge must consume each nested descriptor before its storage is reused');
}finally{
    if(call&&socket?.readyState===WebSocket.OPEN){try{await call('Browser.close');}catch{chrome.kill();}}
    else chrome.kill();
    for(const p of pending.values())clearTimeout(p.timer);
    socket?.close();server.close();
    // Browser subprocesses may briefly retain Windows profile files on exit.
    await delay(1000);
    try{fs.rmSync(profile,{recursive:true,force:true,maxRetries:5,retryDelay:200});}catch{console.error('Test profile retained at '+profile);}
}
