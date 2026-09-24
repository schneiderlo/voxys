// Diagnose GPU hangs locally. Extra completion fences perturb scheduling.
// --profiling=0|1 controls existing timestamp queries; --skip=REGEX omits selected
// draw/dispatch operations for isolation only, never as a production fix.
// Example: node diagnose_gpu_hang.mjs --chrome=PATH --site=DIR --output=DIR
// Each invocation runs one instrumented launch through at least 300 frames.
// This controls browser caches, but does not clear the driver's system cache.
import {installGpuHangTrace,splitGpuPasses} from './gpu_hang_trace.mjs';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import http from 'node:http';
import {spawn} from 'node:child_process';

const args=Object.fromEntries(process.argv.slice(2).map(arg=>{
    const split=arg.indexOf('=');
    if(!arg.startsWith('--')||split<3)throw Error('Use --name=value');
    return [arg.slice(2,split),arg.slice(split+1)];
}));
if(!args.chrome||!args.site||!args.output)throw Error('--chrome, --site and --output are required');
const site=path.resolve(args.site),output=path.resolve(args.output);
fs.mkdirSync(output,{recursive:true});
const profile=args.profile?path.resolve(args.profile):fs.mkdtempSync(path.join(os.tmpdir(),'voxys-startup-comparison-'));
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const report={site,profile,headless:true,viewport:{width:Number(args.width||960),height:Number(args.height||540)},rounds:[],complete:false};
const save=()=>fs.writeFileSync(path.join(output,'report.json'),JSON.stringify(report,null,2));
const mime={'.html':'text/html','.js':'text/javascript','.wasm':'application/wasm','.css':'text/css','.svg':'image/svg+xml','.png':'image/png','.jpg':'image/jpeg'};
const server=http.createServer((req,res)=>{
    const name=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
    const file=path.resolve(site,'.'+(name==='/'?'/index.html':name));
    if(!file.startsWith(site+path.sep)){res.writeHead(403);res.end();return;}
    fs.readFile(file,(error,data)=>{
        if(error){res.writeHead(404);res.end();return;}
        res.setHeader('Content-Type',mime[path.extname(file)]||'application/octet-stream');res.end(data);
    });
});
await new Promise(resolve=>server.listen(Number(args.port||0),'127.0.0.1',resolve));
const chrome=spawn(args.chrome,['--headless=new','--remote-debugging-port=0',
    '--user-data-dir='+profile,'--no-first-run','--no-default-browser-check',
    '--disable-background-timer-throttling','--disable-renderer-backgrounding','about:blank']);
let chromeLog='',socket,call,current;
chrome.stderr.on('data',data=>{chromeLog+=data;});
let spawnError;chrome.on('error',error=>{spawnError=error;});
const pending=new Map();let id=0;
try{
    let port;
    for(let n=0;n<200;n++){
        if(spawnError)throw spawnError;
        port=chromeLog.match(/DevTools listening on ws:\/\/127\.0\.0\.1:(\d+)/)?.[1];
        if(port)break;
        await delay(100);
    }
    if(!port)throw Error('Chrome debugging unavailable: '+chromeLog);
    const target=await(await fetch(`http://127.0.0.1:${port}/json/new?about:blank`,{method:'PUT'})).json();
    socket=new WebSocket(target.webSocketDebuggerUrl);
    await new Promise((resolve,reject)=>{socket.onopen=resolve;socket.onerror=reject;});
    socket.onmessage=event=>{
        const message=JSON.parse(event.data),request=pending.get(message.id);
        if(request){pending.delete(message.id);clearTimeout(request.timer);
            message.error?request.reject(Error(JSON.stringify(message.error))):request.resolve(message.result);}
        if(message.method==='Runtime.consoleAPICalled'&&current){
            current.console.push({type:message.params.type,text:message.params.args.map(a=>a.value??a.description??'').join(' ')});
        }
        if(message.method==='Runtime.exceptionThrown'&&current)current.exceptions.push(message.params.exceptionDetails);
    };
    socket.onclose=()=>{for(const p of pending.values()){clearTimeout(p.timer);p.reject(Error('Chrome closed'));}pending.clear();};
    call=(method,params={},timeout=660000)=>new Promise((resolve,reject)=>{
        const n=++id;
        const timer=setTimeout(()=>{pending.delete(n);reject(Error('Timeout: '+method));},timeout);
        pending.set(n,{resolve,reject,timer});socket.send(JSON.stringify({id:n,method,params}));
    });
    report.browser=await call('Browser.getVersion');
    await call('Page.enable');await call('Runtime.enable');await call('Network.enable');
    await call('Network.setBlockedURLs',{urls:['*googletagmanager.com*','*google-analytics.com*']});
    await call('Emulation.setDeviceMetricsOverride',{...report.viewport,deviceScaleFactor:1,mobile:false});
    await call('Emulation.setFocusEmulationEnabled',{enabled:true});
    await call('Page.addScriptToEvaluateOnNewDocument',{source:`(() => {
        globalThis.startupPipelineTimings=[];
        for(const name of ['createComputePipeline','createRenderPipeline','createComputePipelineAsync','createRenderPipelineAsync']){
            const original=GPUDevice.prototype[name];
            GPUDevice.prototype[name]=function(descriptor){
                const record={name,label:descriptor.label,entry:descriptor.compute?.entryPoint,
                    constants:descriptor.compute?.constants,start_ms:performance.now()};
                startupPipelineTimings.push(record);
                const result=Reflect.apply(original,this,arguments);
                if(name.endsWith('Async'))return result.then(p=>{record.end_ms=performance.now();return p;},e=>{record.error=String(e);throw e;});
                record.return_ms=performance.now();return result;
            };
        }
    })();`});
    await call('Page.addScriptToEvaluateOnNewDocument',{source:`(${installGpuHangTrace.toString()})(${JSON.stringify({skip:args.skip||null})})`});
    if(args.split==='1')await call('Page.addScriptToEvaluateOnNewDocument',{source:`(${splitGpuPasses.toString()})()`});
    report.splitPasses=args.split==='1';
    const url=`http://127.0.0.1:${server.address().port}/?new=1&telemetry=0&physicsProfile=${args.profiling||'0'}&renderProfile=${args.profiling||'0'}${args.query?'&'+args.query:''}`;
    report.url=url;
    for(const cache of [args.profile?'retained-profile':'fresh-profile']){
        current={cache,console:[],exceptions:[],status:'running'};report.rounds.push(current);save();
        const started=Date.now();
        await call('Page.navigate',{url});await call('Page.bringToFront');
        while(Date.now()-started<600000){
            await delay(1000);
            const result=await call('Runtime.evaluate',{returnByValue:true,expression:`(() => {
                const module=typeof voxyModule==='undefined'?null:voxyModule;
                const ready=module?._voxy_is_initialized?.()===1;
                const pointer=ready?module._voxy_get_telemetry_json?.():0;
                const telemetry=pointer?JSON.parse(module.UTF8ToString(pointer)):null;
                return {build:window.voxyBuildId,ready,telemetry,device:window.voxyDeviceProfile,
                    lost:window.voxyDeviceLost,errors:window.voxyUncapturedGpuErrors,
                    loading:document.getElementById('loading')?getComputedStyle(document.getElementById('loading')).display!=='none':true,
                    message:document.getElementById('loading-technical')?.textContent,
                    pageError:document.getElementById('error')&&getComputedStyle(document.getElementById('error')).display!=='none'?document.getElementById('error').innerText:null,
                    trace:globalThis.voxyGpuTrace,pipelines:globalThis.startupPipelineTimings||[]};
            })()`},Math.min(30000,Math.max(1000,600000-(Date.now()-started))));
            if(result.exceptionDetails)throw Error(JSON.stringify(result.exceptionDetails));
            current.sample=result.result.value;current.elapsed_ms=Date.now()-started;save();
            const s=current.sample;
            if(s.ready&&current.initialized_ms===undefined)current.initialized_ms=current.elapsed_ms;
            if(s.lost||s.errors?.length||s.pageError||current.exceptions.length)throw Error('Application error: '+JSON.stringify({lost:s.lost,errors:s.errors,pageError:s.pageError,exceptions:current.exceptions}));
            if(s.ready&&!s.loading&&s.telemetry?.frame?.count>=300){
                current.status='passed';
                const shot=await call('Page.captureScreenshot',{format:'png'});
                fs.writeFileSync(path.join(output,cache+'.png'),Buffer.from(shot.data,'base64'));
                break;
            }
            save();
        }
        if(current.status!=='passed')throw Error('Startup exceeded ten minutes');
        save();console.log(JSON.stringify({cache,elapsed_ms:current.elapsed_ms,initialized_ms:current.initialized_ms,adapter:current.sample.device.adapter}));
    }
    report.complete=true;
}catch(error){report.error=String(error);if(current)current.status='failed';process.exitCode=1;}
finally{
    save();fs.writeFileSync(path.join(output,'chrome.log'),chromeLog);
    if(call&&socket?.readyState===WebSocket.OPEN){try{await call('Browser.close',{},5000);}catch{chrome.kill();}}
    else chrome.kill();
    for(const p of pending.values())clearTimeout(p.timer);
    socket?.close();server.close();
    // Leave this isolated test profile with the report for cache inspection.
}
