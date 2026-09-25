#!/usr/bin/env node
// Headless hardware-WebGPU gameplay probe. Node 22+; no browser dependencies.
// Timings describe headless gameplay, not display/input-to-photon latency.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import http from 'node:http';
import {spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import {installStartupGraphicsProbe} from './performance/startup_graphics_probe.mjs';
import {installGameplayCounters} from './performance/gameplay_counters.mjs';
import {gameplayWork,checkGameplayWork} from './performance/gameplay_work.mjs';
const args = Object.fromEntries(process.argv.slice(2).map(a => {
    const i=a.indexOf('='); if(i<3)throw Error('Use --name=value');
    return [a.slice(2,i),a.slice(i+1)];
}));
if(args['check-work']==='1'&&args.counts!=='1'&&args['diagnose-first']!=='1')throw Error('--check-work=1 requires --counts=1 or --diagnose-first=1');
if(!args.chrome||(!args.site&&!args.url)||!args.output)throw Error('Required: --chrome=PATH --site=DIR (or --url=URL) --output=DIR');
if(Boolean(args['narrow-shader'])!==Boolean(args['expected-narrow-shader']))throw Error('--narrow-shader and --expected-narrow-shader must be supplied together');
const replacement=args['narrow-shader']?{label:args['shader-label']||'physics_narrow_phase.wgsl',code:fs.readFileSync(args['narrow-shader'],'utf8'),expected:fs.readFileSync(args['expected-narrow-shader'],'utf8')}:null;
const settleSeconds=Number(args['settle-seconds']??3);
if(!Number.isFinite(settleSeconds)||settleSeconds<0)throw Error('--settle-seconds must be nonnegative');
const dataDelayMs=Number(args['data-delay-ms']??0);
if(!Number.isFinite(dataDelayMs)||dataDelayMs<0)throw Error('--data-delay-ms must be nonnegative');
const output=path.resolve(args.output); fs.mkdirSync(output,{recursive:true});
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const connections=[];
async function connect(url){
    const ws=new WebSocket(url); await new Promise((r,j)=>{ws.onopen=r;ws.onerror=j;});
    connections.push(ws); let id=0;const pending=new Map();
    ws.onmessage=e=>{const m=JSON.parse(e.data),p=pending.get(m.id);if(p){pending.delete(m.id);clearTimeout(p.timer);m.error?p.reject(Error(JSON.stringify(m.error))):p.resolve(m.result);}
        else if(m.method==='Runtime.consoleAPICalled')fs.appendFileSync(path.join(output,'console.log'),m.params.args.map(a=>a.value??a.description??'').join(' ')+'\n');};
    return {call:(method,params={})=>new Promise((resolve,reject)=>{const n=++id;const timer=setTimeout(()=>{pending.delete(n);reject(Error('Timeout: '+method));},900000);pending.set(n,{resolve,reject,timer});ws.send(JSON.stringify({id:n,method,params}));})};
}
let server,browser,child,report;
const profile=args.profile?path.resolve(args.profile):fs.mkdtempSync(path.join(os.tmpdir(),'voxys-gameplay-'));
try{
    let url=args.url;
    if(args.site){
        const site=path.resolve(args.site);
        server=http.createServer((q,s)=>{
            const name=decodeURIComponent(new URL(q.url,'http://localhost').pathname);
            const file=path.resolve(site,name==='/'?'index.html':name.slice(1));
            if(!file.startsWith(site+path.sep)){s.writeHead(403);s.end();return;}
            fs.readFile(file,async(e,data)=>{if(e){s.writeHead(404);s.end();return;}if(path.extname(file)==='.data'&&dataDelayMs)await sleep(dataDelayMs);s.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.mjs':'text/javascript','.css':'text/css','.wasm':'application/wasm','.svg':'image/svg+xml','.png':'image/png'})[path.extname(file)]||'application/octet-stream');s.end(data);});
        });
        await new Promise(r=>server.listen(Number(args.port||0),'127.0.0.1',r));url='http://127.0.0.1:'+server.address().port+'/index.html';
    }
    const targetUrl=new URL(url);targetUrl.searchParams.set('telemetry','0');targetUrl.searchParams.set('new','1');
    if(args.profiling!==undefined)for(const key of ['renderProfile','physicsProfile'])targetUrl.searchParams.set(key,args.profiling);
    let stderr='';child=spawn(args.chrome,['--headless=new','--window-size=1280,800','--remote-debugging-port=0','--user-data-dir='+profile,'--no-first-run','--no-default-browser-check','about:blank']);
    child.on('error',e=>{stderr+=String(e);});child.stderr.on('data',d=>{stderr+=d;fs.appendFileSync(path.join(output,'chrome.log'),d);});
    let endpoint;for(let i=0;i<200;i++){endpoint=stderr.match(/DevTools listening on (ws:\/\/\S+)/)?.[1];if(endpoint)break;await sleep(100);}if(!endpoint)throw Error('Chrome failed: '+stderr);
    browser=await connect(endpoint);
    const target=await(await fetch('http://127.0.0.1:'+new URL(endpoint).port+'/json/new?about:blank',{method:'PUT'})).json();
    const page=await connect(target.webSocketDebuggerUrl);await page.call('Runtime.enable');await page.call('Page.enable');
    await page.call('Emulation.setDeviceMetricsOverride',{width:Number(args.width||1280),height:Number(args.height||720),deviceScaleFactor:1,mobile:false});
    await page.call('Emulation.setFocusEmulationEnabled',{enabled:true});
    const evaluate=async expression=>{const r=await page.call('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));return r.result.value;};
    if(args.counts==='1')await page.call('Page.addScriptToEvaluateOnNewDocument',{source:`(${installGameplayCounters.toString()})()`});
    if(args.startup==='1'||replacement)await page.call('Page.addScriptToEvaluateOnNewDocument',{source:`(${installStartupGraphicsProbe.toString()})(${JSON.stringify(replacement)})`});
    await page.call('Page.navigate',{url:targetUrl.href});
    let state;
    for(let i=0;i<180;i++){
        await sleep(5000);state=await evaluate(`({ready:typeof voxyModule!=='undefined'&&voxyModule?._voxy_is_initialized?.(),frames:typeof voxyModule!=='undefined'&&voxyModule?._voxy_get_frame_count?.(),lost:globalThis.voxyDeviceLost,errors:globalThis.voxyUncapturedGpuErrors,loading:getComputedStyle(document.getElementById('loading')).display!=='none',device:globalThis.voxyDeviceProfile})`);
        if(state.lost||state.errors?.length)throw Error(JSON.stringify(state));
        if(state.ready&&state.frames>60&&!state.loading)break;
        if(i%6===0)console.log('Waiting for game',state.frames);
    }
    if(!state.ready||state.loading||state.frames<=60)throw Error('Game did not start');
    const graphicsCache=await evaluate('globalThis.voxyGpuStartup?.stats ?? null');
    const downloads=await evaluate('globalThis.voxyStartupDownloads ? {startedMs:voxyStartupDownloads.startedMs,readyMs:voxyStartupDownloads.readyMs,cachedFiles:voxyStartupDownloads.cachedFiles} : null');
    const graphicsRecipe=await evaluate('globalThis.voxyGpuStartup?.recipe ?? null');
    if(graphicsRecipe)fs.writeFileSync(path.join(output,'graphics-recipe.json'),JSON.stringify(graphicsRecipe));
    const startup=(args.startup==='1'||replacement)?await evaluate('voxyFinishGraphicsProbe()'):null;
    if(startup){
        startup.build=await evaluate('window.voxyBuildId');
        startup.browser=await browser.call('Browser.getVersion');
        startup.shader_sha256=replacement?createHash('sha256').update(replacement.code).digest('hex'):null;
        startup.expected_shader_sha256=replacement?createHash('sha256').update(replacement.expected).digest('hex'):null;
        fs.writeFileSync(path.join(output,'startup.json'),JSON.stringify(startup,null,2));
        console.log('Graphics startup',JSON.stringify({span_ms:startup.graphics_span_ms,playable_ms:startup.playable_observed_ms,pipelines:startup.pipelines.length}));
    }
    if(state.device?.adapter?.fallback||/swiftshader|llvmpipe/i.test(JSON.stringify(state.device)))throw Error('Software GPU is not valid performance evidence');
    await evaluate(`const canvas=document.getElementById('voxy-canvas');canvas.focus();canvas.width=${Number(args.width||1280)};canvas.height=${Number(args.height||720)};voxyModule._voxy_resize(canvas.width,canvas.height);voxyModule._adventure_action(15,0);`);
    await sleep(settleSeconds*1000);
    if(args['wait-for-file']){console.log('Waiting for clean timing gate');const deadline=Date.now()+1800000;while(!fs.existsSync(args['wait-for-file'])){if(Date.now()>deadline)throw Error('Timing gate timed out');await sleep(1000);}await sleep(3000);}
    report={schema:'voxys.gameplay.v1',url:targetUrl.href,device:state.device,width:Number(args.width||1280),height:Number(args.height||720),headless:true,instrumented:args.cpu==='1'||args.counts==='1',settleSeconds,startup,graphicsCache,downloads,dataDelayMs,scenarios:[]};
    report.complete=false;
    const saveReport=()=>fs.writeFileSync(path.join(output,'report.json'),JSON.stringify(report,null,2));
    if(args['diagnose-first']==='1'){
        if(args.counts==='1'||args.cpu==='1')throw Error('--diagnose-first requires otherwise clean timing');
        await evaluate(`(${installGameplayCounters.toString()})()`);
        await page.call('Profiler.enable');await page.call('Profiler.start');
        report.diagnostic=await evaluate(`(async()=>{
            const read=()=>JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_telemetry_json()));
            const before=read(),workBefore={...globalThis.voxyWorkCounts};
            await new Promise(resolve=>setTimeout(resolve,5000));
            return {name:'idle',before,after:read(),workBefore,workAfter:{...globalThis.voxyWorkCounts}};
        })()`);
        const {profile:cpuProfile}=await page.call('Profiler.stop');await page.call('Profiler.disable');
        fs.writeFileSync(path.join(output,'diagnostic-idle.cpuprofile'),JSON.stringify(cpuProfile));
        await evaluate('globalThis.voxyStopWorkCounts()');
        report.diagnostic.work=args['check-work']==='1'?checkGameplayWork(report.diagnostic):gameplayWork(report.diagnostic);
        console.log('Diagnostic work',JSON.stringify(report.diagnostic.work));
        saveReport();
        await sleep(2000);
    }
    for(const scenario of (args.scenarios||'idle,orbit,walk,throw').split(',')){
        await page.call('Input.dispatchMouseEvent',{type:'mouseMoved',x:640,y:360});
        if(scenario==='orbit')await page.call('Input.dispatchMouseEvent',{type:'mousePressed',button:'right',buttons:2,x:640,y:360,clickCount:1});
        if(scenario==='walk')await evaluate(`voxyModule._voxy_key_event(87,1)`);
        if(scenario==='throw'){
            await evaluate(`if(JSON.parse(voxyModule.UTF8ToString(voxyModule._get_adventure_state_json())).build)voxyModule._adventure_action(1,0);`);
            await sleep(200);
            const bodies=await evaluate(`voxyModule._voxy_get_physics_resident_bodies()`);
            await page.call('Input.dispatchMouseEvent',{type:'mousePressed',button:'right',buttons:2,x:640,y:360,clickCount:1});
            await sleep(100);
            await page.call('Input.dispatchMouseEvent',{type:'mouseReleased',button:'right',buttons:0,x:640,y:360,clickCount:1});
            await sleep(300);
            const thrown=await evaluate(`voxyModule._voxy_get_physics_resident_bodies()`);
            if(thrown-bodies!==100)throw Error('Expected exactly 100 thrown bricks, got '+(thrown-bodies));
        }
        if(args.cpu==='1'){await page.call('Profiler.enable');await page.call('Profiler.start');}
        const result=await evaluate(`(async()=>{
            const telemetry=()=>JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_telemetry_json()));
            const workBefore={...globalThis.voxyWorkCounts};const before=telemetry(),frames=[],cpu=[],samples=[];let last=null,start=performance.now(),n=0;
            await new Promise(resolve=>{function tick(now){
                if(last!==null)frames.push(now-last);last=now;
                if(${JSON.stringify(scenario)}==='orbit'){
                    document.getElementById('voxy-canvas').dispatchEvent(new MouseEvent('mousemove',{bubbles:true,clientX:640+120*Math.sin((now-start)*Math.PI*2/5000),clientY:360,buttons:2}));
                }
                if(n++%15===0){const t=telemetry();samples.push(t);cpu.push(t.frame.cpu_ms);}
                if(now-start<${Number(args.seconds||10)*1000})requestAnimationFrame(tick);else resolve();
            }requestAnimationFrame(tick);});
            return {name:${JSON.stringify(scenario)},elapsedMs:performance.now()-start,before,after:telemetry(),canvas:{width:document.getElementById('voxy-canvas').width,height:document.getElementById('voxy-canvas').height},health:{lost:globalThis.voxyDeviceLost,errors:globalThis.voxyUncapturedGpuErrors},workBefore,workAfter:{...globalThis.voxyWorkCounts},frames,cpu,samples,adventure:JSON.parse(voxyModule.UTF8ToString(voxyModule._get_adventure_state_json()))};
        })()`);
        if(args.cpu==='1'){const {profile:cpuProfile}=await page.call('Profiler.stop');fs.writeFileSync(path.join(output,scenario+'.cpuprofile'),JSON.stringify(cpuProfile));}
        if(scenario==='orbit')await page.call('Input.dispatchMouseEvent',{type:'mouseReleased',button:'right',buttons:0,x:640,y:360,clickCount:1});
        if(scenario==='walk')await evaluate(`voxyModule._voxy_key_event(87,0)`);
        const percentile=(values,p)=>{const sorted=[...values].sort((a,b)=>a-b);return sorted[Math.min(sorted.length-1,Math.floor(sorted.length*p))];};
        result.summary={frames:result.after.frame.count-result.before.frame.count,renderedFrames:result.after.render.surface_acquired_frames-result.before.render.surface_acquired_frames,p50:percentile(result.frames,.5),p95:percentile(result.frames,.95),p99:percentile(result.frames,.99),cpuP50:percentile(result.cpu,.5),cpuP95:percentile(result.cpu,.95),terrainRefreshes:result.after.render.terrain_cache_refreshes-result.before.render.terrain_cache_refreshes};
        // Retain the failing scenario too; an incomplete report is not a pass.
        report.scenarios.push(result);saveReport();
        if(result.health.lost||result.health.errors?.length)throw Error('GPU failure: '+JSON.stringify(result.health));
        if(result.canvas.width!==report.width||result.canvas.height!==report.height)throw Error('Render resolution changed');
        if(result.summary.frames<30||result.adventure.cannon?.wallFailed||result.after.physics.tick<=result.before.physics.tick)throw Error('Gameplay did not advance: '+JSON.stringify(result.summary));
        if(args.counts==='1')result.work=args['check-work']==='1'?checkGameplayWork(result):gameplayWork(result);
        console.log(scenario,JSON.stringify(result.summary));
        saveReport();
        const shot=await page.call('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(output,scenario+'.png'),Buffer.from(shot.data,'base64'));
    }
    report.complete=true;
    saveReport();
}catch(error){
    report||={complete:false,scenarios:[]};report.error=String(error);
    fs.writeFileSync(path.join(output,'report.json'),JSON.stringify(report,null,2));
    throw error;
}finally{
    if(browser)try{await browser.call('Browser.close');}catch{}
    for(const ws of connections)ws.close();
    if(child&&!child.killed)child.kill();
    if(server)server.close();
    // Chrome may briefly retain profile files after exit on Windows.
    if(!args.profile)try{fs.rmSync(profile,{recursive:true,force:true,maxRetries:5,retryDelay:200});}catch{}
}
