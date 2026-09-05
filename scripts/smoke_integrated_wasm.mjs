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
    for(const experience of ['terrain','ridgebreak']){
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
assert(['terrain','ridgebreak'].includes(selected),'unknown scene');
const directory=await mkdtemp(path.join(tmpdir(),'voxys-startup-'));
const delay=ms=>new Promise(r=>setTimeout(r,ms));
const mime={'.html':'text/html','.js':'text/javascript','.css':'text/css','.wasm':'application/wasm','.data':'application/octet-stream'};
const server=http.createServer(async(req,res)=>{
    try{
        const requested=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
        const filename=path.resolve(root,'.'+(requested==='/'?'/index.html':requested));
        if(!filename.startsWith(root+path.sep)){res.writeHead(403);res.end();return;}
        const data=await readFile(filename);
        res.writeHead(200,{'Content-Type':mime[path.extname(filename)]||'application/octet-stream'});res.end(data);
    }catch{res.writeHead(404);res.end();}
});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const chrome=spawn(process.env.VOXY_TEST_CHROME||'google-chrome',[
    '--headless=new','--no-sandbox','--no-first-run','--no-default-browser-check',
    '--disable-background-networking','--enable-unsafe-webgpu','--enable-unsafe-swiftshader',
    '--use-angle=vulkan','--enable-features=Vulkan','--use-vulkan=native',
    '--disable-vulkan-surface','--remote-debugging-port=0',`--user-data-dir=${directory}`,'about:blank'
],{stdio:['ignore','ignore','pipe']});
let logs='',spawnError,socket;
chrome.stderr.on('data',d=>logs=(logs+d).slice(-12000));chrome.on('error',e=>spawnError=e);
const report={kind:'application startup, no FPS acceptance',experience:selected};
const timer=setTimeout(()=>chrome.kill('SIGKILL'),240000);
try{
    let port;
    for(let i=0;i<1200&&!port;++i){
        if(spawnError)throw spawnError;
        if(chrome.exitCode!==null)throw new Error(logs);
        try{port=Number((await readFile(path.join(directory,'DevToolsActivePort'),'utf8')).split('\n')[0]);}
        catch{await delay(50);}
    }
    assert(port,'Chrome debugging unavailable');
    const target=await(await fetch(`http://127.0.0.1:${port}/json/new?about:blank`,{method:'PUT'})).json();
    socket=new WebSocket(target.webSocketDebuggerUrl);
    const pending=new Map();let id=0;
    socket.addEventListener('message',e=>{const m=JSON.parse(e.data),p=pending.get(m.id);
        if(p){pending.delete(m.id);m.error?p.reject(new Error(JSON.stringify(m.error))):p.resolve(m.result);}});
    socket.addEventListener('close',()=>{for(const p of pending.values())p.reject(new Error('Chrome closed'));pending.clear();});
    await new Promise((r,j)=>{socket.addEventListener('open',r,{once:true});socket.addEventListener('error',j,{once:true});});
    const call=(method,params={})=>new Promise((resolve,reject)=>{const n=++id;pending.set(n,{resolve,reject});socket.send(JSON.stringify({id:n,method,params}));});
    await call('Runtime.enable');await call('Network.enable');
    await call('Network.setBlockedURLs',{urls:['*googletagmanager.com*','*google-analytics.com*']});
    await call('Emulation.setDeviceMetricsOverride',{width:320,height:240,deviceScaleFactor:1,mobile:false});
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
    const url=`http://127.0.0.1:${server.address().port}/index.html?browserBenchmarkRun=1&physicsProfile=1&renderProfile=1&telemetry=0&physicsMaxBodies=1024${selected==='ridgebreak'?'&experience=ridgebreak':''}`;
    await call('Page.navigate',{url});
    let sample;const started=Date.now();
    while(Date.now()-started<180000){
        const r=await call('Runtime.evaluate',{returnByValue:true,expression:`(() => {
            if(typeof voxyModule==='undefined'||!voxyModule?._voxy_is_initialized?.())return null;
            const pointer=voxyModule._voxy_get_telemetry_json();const moto=voxyModule._voxy_get_moto_hud_json?.();
            return {telemetry:JSON.parse(voxyModule.UTF8ToString(pointer)),
                moto:moto?JSON.parse(voxyModule.UTF8ToString(moto)):null,
                errors:globalThis.voxyUncapturedGpuErrors||[],lost:globalThis.voxyDeviceLost,
                adapter:window.voxyDeviceProfile?.adapter,title:document.title};
        })()`});
        if(r.exceptionDetails)throw new Error(JSON.stringify(r.exceptionDetails));
        sample=r.result?.value;report.sample=sample;
        if(sample?.errors?.length||sample?.lost)throw new Error('GPU device error: '+JSON.stringify(sample.lost||sample.errors));
        // Passing the eight-frame queue limit requires a completion callback.
        // A timestamp sample additionally proves that GPU work/readback retired.
        if(sample?.telemetry?.frame?.count>=12&&sample.telemetry.render_gpu?.available)break;
        await delay(500);
    }
    assert(sample?.telemetry?.frame?.count>=12&&sample.telemetry.render_gpu?.available,'GPU did not retire startup frames');
    assert.equal(sample.telemetry.physics.backend,'webgpu_soft');
    assert.equal(Boolean(sample.moto?.active),selected==='ridgebreak','experience activation mismatch');
    report.status='passed';
}catch(error){report.status='failed';report.error=String(error);report.chrome_log=logs;throw error;}
finally{
    await writeFile(process.env.VOXY_SMOKE_REPORT||'integrated-startup-report.json',JSON.stringify(report,null,2)+'\n');
    clearTimeout(timer);socket?.close();
    if(chrome.exitCode===null&&chrome.signalCode===null&&!spawnError){const closed=new Promise(r=>chrome.once('close',r));chrome.kill('SIGKILL');await closed;}
    await new Promise(r=>server.close(r));await rm(directory,{recursive:true,force:true,maxRetries:8,retryDelay:100});
}
console.log(JSON.stringify(report,null,2));
