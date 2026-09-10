#!/usr/bin/env node
// Launch a fresh hardware browser and preserve real C++ GPU test readbacks.
import assert from 'node:assert/strict';
import {readFile,writeFile,mkdir,mkdtemp,rm} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {spawn} from 'node:child_process';
import http from 'node:http';
import path from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';

const [packageArg,outputArg]=process.argv.slice(2);
assert(packageArg&&outputArg,'Usage: node scripts/run_shape_diagnostics.mjs BUILD_DIRECTORY NEW_OUTPUT_DIRECTORY');
const root=path.resolve(packageArg),output=path.resolve(outputArg);
const suite=process.env.VOXY_SHAPE_SUITE||'all';
assert(['all','live','lego','queries','contacts','terrain','consumers'].includes(suite),'Unknown shape test suite');
await mkdir(output,{recursive:false});
const profile=await mkdtemp(path.join(tmpdir(),'voxys-shape-browser-'));
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const report={passed:false,kind:'Shared C++ authored-shape compute and lifetime tests on hardware browser WebGPU',
    runner_sha256:sha(await readFile(fileURLToPath(import.meta.url))),package:root,artifacts:{}};
for(const name of ['index.html','shape_diagnostics.js','shape_diagnostics.wasm']){
    const bytes=await readFile(path.join(root,name));
    report.artifacts[name]={sha256:sha(bytes),bytes:bytes.length};
}
const server=http.createServer(async(req,res)=>{
    try{
        const pathname=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
        const file=path.resolve(root,'.'+(pathname==='/'?'/index.html':pathname));
        if(!file.startsWith(root+path.sep)){res.writeHead(403);res.end();return;}
        res.writeHead(200,{'Content-Type':{'.html':'text/html','.js':'text/javascript','.wasm':'application/wasm'}[path.extname(file)]||'application/octet-stream'});
        res.end(await readFile(file));
    }catch{res.end();}
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const flags=['--headless=new','--no-sandbox','--no-first-run','--no-default-browser-check','--disable-background-networking',
    '--enable-unsafe-webgpu','--disable-gpu-watchdog','--disable-background-timer-throttling',
    '--disable-renderer-backgrounding','--ozone-platform=x11','--enable-features=Vulkan',
    '--use-angle=vulkan','--use-vulkan=native','--disable-vulkan-surface',
    '--remote-debugging-port=0',`--user-data-dir=${profile}`,'about:blank'];
report.chrome_argv=[process.env.VOXY_TEST_CHROME||'google-chrome',...flags];
const chrome=spawn(report.chrome_argv[0],flags,{stdio:['ignore','ignore','pipe']});
let stderr='',spawnError,socket;
chrome.stderr.on('data',data=>stderr+=data);chrome.on('error',error=>spawnError=error);
const closed=new Promise(resolve=>chrome.once('close',(code,signal)=>{report.chrome_exit={code,signal};resolve();}));
const timer=setTimeout(()=>chrome.kill('SIGKILL'),180000);
const errors=[],consoleMessages=[];
try{
    let port;
    for(let i=0;i<600&&!port;i++){
        if(spawnError)throw spawnError;
        if(chrome.exitCode!==null)throw Error('Chrome exited: '+stderr);
        try{port=Number((await readFile(path.join(profile,'DevToolsActivePort'),'utf8')).split('\n')[0]);}
        catch{await delay(50);}
    }
    assert(port,'Chrome startup timed out');
    const target=await(await fetch(`http://127.0.0.1:${port}/json/new?about:blank`,{method:'PUT'})).json();
    socket=new WebSocket(target.webSocketDebuggerUrl);
    const pending=new Map();let sequence=0;
    socket.addEventListener('message',event=>{
        const message=JSON.parse(event.data),waiting=pending.get(message.id);
        if(message.method==='Runtime.exceptionThrown')errors.push(message.params.exceptionDetails);
        if(message.method==='Runtime.consoleAPICalled')consoleMessages.push(message.params);
        if(waiting){pending.delete(message.id);message.error?waiting.reject(Error(JSON.stringify(message.error))):waiting.resolve(message.result);}
    });
    socket.addEventListener('close',()=>{for(const p of pending.values())p.reject(Error('Chrome closed'));pending.clear();});
    await new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',reject,{once:true});});
    const call=(method,params={})=>new Promise((resolve,reject)=>{
        const id=++sequence;pending.set(id,{resolve,reject});socket.send(JSON.stringify({id,method,params}));
    });
    await call('Page.enable');await call('Runtime.enable');
    report.browser=await call('Browser.getVersion');
    await call('Emulation.setDeviceMetricsOverride',{width:1400,height:1840,deviceScaleFactor:1,mobile:false});
    await call('Page.navigate',{url:`http://127.0.0.1:${server.address().port}/?suite=${suite}`});
    let result;
    for(let i=0;i<1200;i++){
        const response=await call('Runtime.evaluate',{expression:'globalThis.diagnostic?.done ? globalThis.diagnostic : null',returnByValue:true});
        if(response.exceptionDetails)throw Error(JSON.stringify(response.exceptionDetails));
        result=response.result?.value;
        if(result)break;
        await delay(100);
    }
    assert(result,'C++ shape tests timed out');
    await writeFile(path.join(output,'tests.log'),result.output.join('\n')+'\n');
    report.result={...result,captures:[]};
    for(const capture of result.captures){
        assert(/^[0-9]+-[A-Za-z0-9_]+\.u32$/.test(capture.name),'Invalid readback name');
        const bytes=Buffer.from(capture.bytes);
        assert.equal(bytes.length,256,'Unexpected compute readback size');
        await writeFile(path.join(output,capture.name),bytes);
        report.result.captures.push({name:capture.name,sha256:sha(bytes),bytes:bytes.length});
    }
    report.unexpected_console_errors=consoleMessages.filter(m=>m.type==='error');
    assert.equal(report.unexpected_console_errors.length,0,'Unexpected console errors');
    assert.equal(errors.length,0,'Browser exceptions');
    assert.equal(result.passed,true,'C++/GPU diagnostics failed');
    report.passed=true;
}catch(error){report.failure=String(error);process.exitCode=1;}
finally{
    report.exceptions=errors;report.console=consoleMessages;
    await writeFile(path.join(output,'report.json'),JSON.stringify(report,null,2)+'\n');
    await writeFile(path.join(output,'chrome.log'),stderr);
    socket?.close();chrome.kill('SIGTERM');
    await Promise.race([closed,delay(5000).then(()=>chrome.kill('SIGKILL'))]);
    clearTimeout(timer);await new Promise(resolve=>server.close(resolve));
    await rm(profile,{recursive:true,force:true});
}
console.log(JSON.stringify({passed:report.passed,output,failure:report.failure}));
