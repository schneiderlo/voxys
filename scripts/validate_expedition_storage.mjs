#!/usr/bin/env node
// Real browser storage only. Private browser/profile, no renderer or screenshots.
import assert from 'node:assert/strict';
import {readFile,writeFile,mkdir,mkdtemp,rm} from 'node:fs/promises';
import {spawn,execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import http from 'node:http';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const output=path.resolve(process.argv[2]);await mkdir(output,{recursive:false});
const profile=await mkdtemp(path.join(root,'.expedition-store-browser-'));
const files={'/expedition_store.js':'web/expedition_store.js','/cases.js':'scripts/expedition_storage_cases.js'};
const sources={};for(const name of [...Object.values(files),'scripts/validate_expedition_storage.mjs','tests/fixtures/save_generation_v1.json'])sources[name]=createHash('sha256').update(await readFile(path.join(root,name))).digest('hex');
const report={status:'running',kind:'Isolated browser world storage, no GameSession mutation',sourceHashes:sources,checks:[],screenshots:0,profileFilesystem:execFileSync('findmnt',['--target',profile,'--noheadings','--output','FSTYPE,TARGET'],{encoding:'utf8'}).trim(),browserRuns:[]};
const persist=()=>writeFile(path.join(output,'report.json'),JSON.stringify(report,null,2)+'\n');
const html='<!doctype html><meta charset="utf-8"><title>Expedition storage checks</title><script src="/expedition_store.js"></script><script src="/cases.js"></script>';
const server=http.createServer(async(req,res)=>{
    const route=new URL(req.url,'http://localhost').pathname;
    if(route==='/'){res.writeHead(200,{'Content-Type':'text/html'});res.end(html);return;}
    if(files[route]){res.writeHead(200,{'Content-Type':'text/javascript'});res.end(await readFile(path.join(root,files[route])));return;}
    res.writeHead(404);res.end();
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const url=`http://127.0.0.1:${server.address().port}/`;
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
let chrome,closed,port,sockets=[],runLog='',runs=0;
async function start(){
    runs++;runLog='';port=0;
    // The preceding process is terminal before this is removed; no live handle is guessed stale.
    await rm(path.join(profile,'DevToolsActivePort'),{force:true});
    chrome=spawn(process.env.VOXY_TEST_CHROME||'/usr/bin/google-chrome',[
        '--headless=new','--no-sandbox','--disable-gpu','--no-first-run','--no-default-browser-check','--disable-background-networking',
        '--remote-debugging-port=0',`--user-data-dir=${profile}`,'about:blank'
    ],{detached:true,stdio:['ignore','ignore','pipe']});
    let spawnError;chrome.on('error',e=>{spawnError=e;});chrome.stderr.on('data',chunk=>{runLog+=chunk;});
    closed=new Promise(resolve=>chrome.once('close',(code,signal)=>resolve({code,signal})));
    for(let tries=0;tries<200&&!port;tries++){
        if(spawnError)throw spawnError;if(chrome.exitCode!==null)throw Error('Chrome exited: '+runLog);
        try{port=Number((await readFile(path.join(profile,'DevToolsActivePort'),'utf8')).split('\n')[0]);}catch{await delay(50);}
    }
    assert(port,'debugging port unavailable');
    const version=await(await fetch(`http://127.0.0.1:${port}/json/version`)).json();report.browserRuns.push({run:runs,version});
}
async function stop(signal='SIGTERM'){
    if(!chrome)return;
    const processHandle=chrome;chrome=null;
    try{process.kill(-processHandle.pid,signal);}catch(e){if(e.code!=='ESRCH')throw e;}
    report.browserRuns.at(-1).termination=await closed;
    for(const socket of sockets)socket.close();sockets=[];
    await writeFile(path.join(output,`chrome-${runs}.log`),runLog);
}
async function page(){
    const target=await(await fetch(`http://127.0.0.1:${port}/json/new?about:blank`,{method:'PUT'})).json();
    const socket=new WebSocket(target.webSocketDebuggerUrl);sockets.push(socket);let serial=0;const pending=new Map(),listeners=new Map();
    socket.addEventListener('message',event=>{
        const message=JSON.parse(event.data),request=pending.get(message.id);
        if(request){pending.delete(message.id);clearTimeout(request.timer);message.error?request.reject(Error(JSON.stringify(message.error))):request.resolve(message.result);}
        for(const listener of listeners.get(message.method)||[])listener(message.params);
    });
    socket.addEventListener('close',()=>{for(const request of pending.values()){clearTimeout(request.timer);request.reject(Error('Test browser closed'));}pending.clear();});
    await new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',reject,{once:true});});
    const call=(method,params={})=>new Promise((resolve,reject)=>{
        const id=++serial,timer=setTimeout(()=>{pending.delete(id);reject(Error('Timed out: '+method));},30000);
        pending.set(id,{resolve,reject,timer});socket.send(JSON.stringify({id,method,params}));
    });
    const evaluate=async expression=>{const value=await call('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});if(value.exceptionDetails)throw Error(JSON.stringify(value.exceptionDetails));return value.result.value;};
    const once=method=>new Promise((resolve,reject)=>{
        const handler=value=>{clearTimeout(timer);listeners.set(method,(listeners.get(method)||[]).filter(f=>f!==handler));resolve(value);};
        const timer=setTimeout(()=>reject(Error('No browser event: '+method)),15000);listeners.set(method,[...(listeners.get(method)||[]),handler]);
    });
    await call('Page.enable');await call('Runtime.enable');
    const ready=async()=>{for(let i=0;i<200;i++){try{if(await evaluate('typeof openJourneyStore==="function"'))return;}catch{}await delay(25);}throw Error('Storage fixture did not load');};
    const loaded=once('Page.loadEventFired');await call('Page.navigate',{url});await loaded;await ready();
    return {target,call,evaluate,once,ready};
}
try{
    await start();let first=await page();
    const fixture=JSON.parse(await readFile(path.join(root,'tests/fixtures/save_generation_v1.json'),'utf8'));
    const interop=await first.evaluate(`(async()=>{
        const fixture=${JSON.stringify(fixture)};
        const payload=Uint8Array.from(fixture.payloadHex.match(/../g),v=>parseInt(v,16));
        const bytes=await VoxyExpeditionStore.encodeGeneration(fixture.world,fixture.generation,payload);
        const decoded=await VoxyExpeditionStore.decodeGeneration(bytes,fixture.world);
        return {hex:Array.from(bytes,v=>v.toString(16).padStart(2,'0')).join(''),generation:String(decoded.generation),payload:Array.from(decoded.payload)};
    })()`);
    assert.equal(interop.hex,fixture.bytesHex);assert.equal(interop.generation,fixture.generation);assert.deepEqual(interop.payload,[0,127,255]);
    report.nativeEnvelopeInterop={status:'passed',bytes:interop.hex.length/2,sha256:fixture.sha256,generation:interop.generation};
    const database='voxys-storage-test-'+crypto.randomUUID();
    report.isolated=await first.evaluate(`runExpeditionStorageCases(${JSON.stringify(database)})`);assert.equal(report.isolated.status,'passed');await persist();
    const journey='voxys-world-journey-'+crypto.randomUUID();
    assert.equal((await first.evaluate(`openJourneyStore(${JSON.stringify(journey)})`)).generation,'0');
    assert.equal(await first.evaluate('journeyStore.publish(0n,new Uint8Array([1,77])).then(value=>String(value.generation))'),'1');
    const second=await page();
    assert.equal(await second.evaluate(`openJourneyStore(${JSON.stringify(journey)}).then(()=>"unexpected",error=>error.code)`),'Busy');
    report.checks.push('second actual tab refuses a live world owner');
    const reloaded=first.once('Page.loadEventFired');await first.call('Page.reload');await reloaded;await first.ready();
    const afterReload=await second.evaluate(`openJourneyStore(${JSON.stringify(journey)})`);
    assert.deepEqual(afterReload,{generation:'1',payload:[1,77],needsRepair:false});report.checks.push('actual page reload releases ownership and preserves the acknowledged generation');
    await first.call('Target.closeTarget',{targetId:second.target.id});
    assert.deepEqual(await first.evaluate(`openJourneyStore(${JSON.stringify(journey)})`),afterReload);report.checks.push('actual tab close releases world ownership');
    // Kill the private browser while paused inside this test page's write path.
    // Before the second put: transaction cannot publish a partial two-copy save.
    // After transaction completion: acknowledgment was not yet delivered to the caller.
    for(const [point,expected]of [['first-put','1'],['committed','2']]){
        await first.call('Debugger.enable');await first.evaluate(`installSaveCrashCut(${JSON.stringify(journey)},${JSON.stringify(point)})`);
        const paused=first.once('Debugger.paused');
        const pending=first.evaluate('journeyStore.publish(1n,new Uint8Array([1,88])).then(value=>String(value.generation))').catch(error=>({closed:String(error)}));
        const location=await paused;assert(location.callFrames.length>0);
        const frame=location.callFrames[0];
        const script=await first.call('Debugger.getScriptSource',{scriptId:frame.location.scriptId});
        assert.equal(script.scriptSource,await readFile(path.join(root,'scripts/expedition_storage_cases.js'),'utf8'));
        const pausedLine=script.scriptSource.split('\n')[frame.location.lineNumber];
        report.crashCuts??=[];report.crashCuts.push({point,function:frame.functionName,location:frame.location,pausedLine});
        assert(pausedLine.includes('debugger;')&&(point==='first-put'?pausedLine.includes('seen===1'):pausedLine.includes("'complete'")),'expected fixture pause source');
        await stop('SIGKILL');assert.equal(report.browserRuns.at(-1).termination.signal,'SIGKILL');
        const completion=await pending;assert(completion?.closed,'save acknowledgment must not have reached caller before the crash');
        report.crashCuts.at(-1).acknowledgmentObserved=false;
        await start();first=await page();const recovered=await first.evaluate(`openJourneyStore(${JSON.stringify(journey)})`);
        assert.equal(recovered.generation,expected);assert.deepEqual(recovered.payload,expected==='1'?[1,77]:[1,88]);assert.equal(recovered.needsRepair,false);
        report.checks.push(`browser process kill at ${point} recovers complete generation ${expected}`);await persist();
    }
    await first.evaluate('journeyStore.close()');
    report.sourcesUnchanged=true;
    for(const [name,hash]of Object.entries(sources))assert.equal(createHash('sha256').update(await readFile(path.join(root,name))).digest('hex'),hash,name);
    report.status='passed';await persist();
}catch(error){report.status='failed';report.failure=String(error.stack||error);await persist();process.exitCode=1;}
finally{await stop();await new Promise(resolve=>server.close(resolve));await rm(profile,{recursive:true,force:true});report.profileRemoved=true;await persist();}
console.log(JSON.stringify({status:report.status,isolatedChecks:report.isolated?.checks?.length,journeyChecks:report.checks.length,failure:report.failure},null,2));
