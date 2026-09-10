#!/usr/bin/env node
// Node 22+, local Chrome and a hardware WebGPU adapter. No image capture.
// Usage: node scripts/validate_authored_terrain_browser.mjs REPORT.json [SHADER.wgsl]
// VOXY_SMOKE_GPU=gaming-x11 uses a normal X11 window; default is headless Vulkan.
import assert from 'node:assert/strict';
import {readFile,writeFile,mkdtemp,rm,mkdir} from 'node:fs/promises';
import {spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import http from 'node:http';
import path from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
assert(process.argv[2],'An output report path is required');
const output=path.resolve(process.argv[2]),shaderPath=path.resolve(process.argv[3]||path.join(root,'shaders/physics_ballistic.wgsl'));
const shader=await readFile(shaderPath),test=await readFile(path.join(root,'tests/browser/authored_terrain.mjs'));
const page=`<!doctype html><script type="module">import {runAuthoredTerrainRegression} from '/test.mjs';
try { globalThis.result=await runAuthoredTerrainRegression(await (await fetch('/shader.wgsl')).text()); }
catch(error){globalThis.result={status:'failed',error:String(error)};}</script>`;
const server=http.createServer((request,response)=>{
    const item={'/':[page,'text/html'],'/test.mjs':[test,'text/javascript'],'/shader.wgsl':[shader,'text/plain']}[request.url];
    response.writeHead(item?200:404,{'Content-Type':item?.[1]||'text/plain'});response.end(item?.[0]||'Not found');
});
await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});
const profile=await mkdtemp(path.join(tmpdir(),'voxy-contact-'));
const windowed=process.env.VOXY_SMOKE_GPU==='gaming-x11';
const flags=windowed?['--ozone-platform=x11','--enable-features=Vulkan']:
    ['--headless=new','--use-angle=vulkan','--enable-features=Vulkan','--use-vulkan=native','--disable-vulkan-surface'];
const chrome=spawn(process.env.VOXY_TEST_CHROME||'google-chrome',[
    '--no-sandbox','--no-first-run','--no-default-browser-check','--disable-background-networking',
    '--enable-unsafe-webgpu','--disable-gpu-watchdog','--disable-background-timer-throttling',
    '--disable-renderer-backgrounding',...flags,'--remote-debugging-port=0',`--user-data-dir=${profile}`,'about:blank'
],{stdio:['ignore','ignore','pipe']});
let logs='',socket,spawnError;
chrome.stderr.on('data',data=>{logs=(logs+data).slice(-12000);});chrome.on('error',error=>{spawnError=error;});
const closed=new Promise(resolve=>chrome.once('close',resolve));
const timer=setTimeout(()=>chrome.kill('SIGKILL'),120000),pending=new Map();
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const report={kind:'shipping static-contact browser regression; no screenshots',shader:shaderPath,
    sha256:createHash('sha256').update(shader).digest('hex'),flags,status:'failed'};
try{
    let port;
    for(let attempt=0;attempt<300&&!port;attempt++){
        if(spawnError)throw spawnError;assert(chrome.exitCode===null,logs);
        try{port=Number((await readFile(path.join(profile,'DevToolsActivePort'),'utf8')).split('\n')[0]);}catch{await delay(100);}
    }
    assert(port,'Chrome debugging unavailable');
    const target=await(await fetch(`http://127.0.0.1:${port}/json/new?about:blank`,{method:'PUT'})).json();
    socket=new WebSocket(target.webSocketDebuggerUrl);let id=0;
    socket.addEventListener('message',event=>{
        const message=JSON.parse(event.data),item=pending.get(message.id);
        if(item){pending.delete(message.id);message.error?item.reject(Error(JSON.stringify(message.error))):item.resolve(message.result);}
    });
    socket.addEventListener('close',()=>{for(const item of pending.values())item.reject(Error('Chrome closed'));pending.clear();});
    await new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',reject,{once:true});});
    const call=(method,params={})=>new Promise((resolve,reject)=>{pending.set(++id,{resolve,reject});socket.send(JSON.stringify({id,method,params}));});
    report.browser=await call('Browser.getVersion');
    await call('Page.enable');await call('Page.navigate',{url:`http://127.0.0.1:${server.address().port}/`});
    await call('Page.bringToFront');
    for(let attempt=0;attempt<900;attempt++){
        const result=await call('Runtime.evaluate',{expression:'globalThis.result',returnByValue:true});
        if(result.result?.value){report.result=result.result.value;break;}await delay(100);
    }
    assert.equal(report.result?.status,'passed',JSON.stringify(report.result));report.status='passed';
}catch(error){report.error=String(error);report.browserLog=logs;process.exitCode=1;}
finally{
    clearTimeout(timer);socket?.close();chrome.kill('SIGTERM');
    await Promise.race([closed,delay(2000)]);
    if(chrome.exitCode===null&&chrome.signalCode===null){chrome.kill('SIGKILL');await closed;}
    await new Promise(resolve=>server.close(resolve));await rm(profile,{recursive:true,force:true});
    await mkdir(path.dirname(output),{recursive:true});await writeFile(output,JSON.stringify(report,null,2)+'\n');
    console.log(JSON.stringify({status:report.status,cases:report.result?.cases?.length||0,report:output}));
}
