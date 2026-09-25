import fs from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';
import http from 'node:http';
import {createHash} from 'node:crypto';
// Compare the production collision shader variants with identical layouts.
// node scripts/performance/compare_pipeline_startup.mjs --chrome=PATH --mode=batch --output=DIR
const args=Object.fromEntries(process.argv.slice(2).map(arg=>{
    const at=arg.indexOf('=');return [arg.slice(2,at),arg.slice(at+1)];
}));
const mode=args.mode;
if(!args.chrome||!args.output||!['sync','serial','batch'].includes(mode))throw Error('--chrome, --output and --mode=sync|serial|batch are required');
if(args.recipe&&(!args.shader||!args.label))throw Error('--recipe also requires --shader and --label');
const dir=path.resolve(args.output);fs.mkdirSync(dir,{recursive:true});
// --shader permits an archived baseline without changing the working tree.
const shader=fs.readFileSync(args.shader||new URL('../../shaders/physics_narrow_phase.wgsl',import.meta.url),'utf8');
const shaderHash=createHash('sha256').update(shader).digest('hex');
const inputs=[];
let resources;
if(args.recipe){
    const recipe=JSON.parse(fs.readFileSync(args.recipe,'utf8'));
    resources=recipe.resources;
    const label=args.label||'physics_ballistic.wgsl';
    const selected=new Set();
    for(const [index,row] of resources.entries())if(row.kind==='createShaderModule'&&row.descriptor.label===label){
        row.descriptor.code=shader;selected.add(index);
    }
    for(const row of recipe.pipelines)if(row.kind==='createComputePipelineAsync'&&selected.has(row.descriptor.compute.module.$gpu))inputs.push(row);
    if(!inputs.length)throw Error('No recipe pipelines for '+label);
}else{
const names=['sphere_sphere','sphere_capsule','capsule_capsule','sphere_box','capsule_box','box_box','sphere_cylinder','capsule_cylinder','box_cylinder','cylinder_cylinder'];
for(const [index,name] of names.entries()){
    const authored=[3,4,5,8].includes(index);
    for(const pass of authored?[0,1]:[-1]){
        inputs.push({descriptor:{label:pass===1?'narrow_phase_authored_pair_class':'narrow_phase_pair_class',layout:'auto',
            compute:{module:{label:'physics_narrow_phase.wgsl',code:shader},entryPoint:'narrow_'+name+'_128',
                ...(pass<0?{}:{constants:{AUTHORED_PAIR_PASS:pass}})}}});
    }
}
}
const profile=fs.mkdtempSync(path.join(dir,mode+'-profile-'));
const server=http.createServer((req,res)=>{
    res.setHeader('Content-Type',req.url==='/pipelines.json'?'application/json':'text/html');
    res.end(req.url==='/pipelines.json'?JSON.stringify({inputs,resources}):'<!doctype html><title>Startup compilation comparison</title>');
});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const chrome=spawn(args.chrome,[
    '--headless=new','--remote-debugging-port=0','--user-data-dir='+profile,
    '--no-first-run','--no-default-browser-check','--disable-background-timer-throttling',
    '--disable-renderer-backgrounding','about:blank']);
let stderr='',socket,spawnError;
chrome.on('error',error=>{spawnError=error;});
chrome.stderr.on('data',d=>{stderr+=d;});
const delay=ms=>new Promise(r=>setTimeout(r,ms));
const pending=new Map();let id=0;
async function run(mode){
    const {inputs,resources}=await(await fetch('/pipelines.json')).json();
    const adapter=await navigator.gpu.requestAdapter({powerPreference:'high-performance'});
    const device=await adapter.requestDevice({requiredLimits:{maxStorageBuffersPerShaderStage:8}});
    const report=globalThis.report={mode,adapter:{vendor:adapter.info.vendor,architecture:adapter.info.architecture},rounds:[],lost:null,errors:[]};
    device.lost.then(info=>{report.lost={reason:info.reason,message:info.message};});
    device.addEventListener('uncapturederror',e=>report.errors.push(e.error.message));
    const modules=new Map();
    globalThis.retainedPipelines=[];
    if(resources){
        const objects=[];
        function decode(value){
            if(!value||typeof value!=='object')return value;
            if(Array.isArray(value))return value.map(decode);
            if('$gpu' in value)return objects[value.$gpu]||build(value.$gpu);
            return Object.fromEntries(Object.entries(value).map(([key,item])=>[key,decode(item)]));
        }
        function build(index){const row=resources[index];return objects[index]=device[row.kind](decode(row.descriptor));}
        for(const row of inputs)row.descriptor=decode(row.descriptor);
        report.layout='captured production descriptors';
    }else{
    const entries=[[0,true],[1,true],[4,false],[6,true],[7,false],[8,true],[9,false],[15,true]].map(([binding,readOnly])=>({binding,visibility:GPUShaderStage.COMPUTE,buffer:{type:readOnly?'read-only-storage':'storage'}}));
    entries.push({binding:10,visibility:GPUShaderStage.COMPUTE,buffer:{type:'uniform',minBindingSize:48}},{binding:16,visibility:GPUShaderStage.COMPUTE,buffer:{type:'uniform',minBindingSize:128}});
    const layout=device.createPipelineLayout({bindGroupLayouts:[device.createBindGroupLayout({entries})]});
    report.layout='production explicit narrow-phase layout';
    for(const p of inputs){
        const source=p.descriptor.compute.module;
        if(!modules.has(source.code))modules.set(source.code,device.createShaderModule(source));
        p.descriptor.compute.module=modules.get(source.code);p.descriptor.layout=layout;
    }
    }
    for(const cache of ['fresh-profile','same-device-repeat']){
        const round={cache,pipelines:[]};report.rounds.push(round);
        const started=performance.now();device.pushErrorScope('validation');
        async function create(p){
            const d=p.descriptor;
            const result={entry:d.compute.entryPoint,constants:d.compute.constants||{},label:d.label,start_ms:performance.now()-started};
            round.pipelines.push(result);
            const pipeline=mode==='sync'?device.createComputePipeline(d):await device.createComputePipelineAsync(d);
            result.end_ms=performance.now()-started;
            if(!pipeline)throw Error('No pipeline');globalThis.retainedPipelines.push(pipeline);
        }
        if(mode==='batch')await Promise.all(inputs.map(create));
        else for(const p of inputs)await create(p);
        const error=await device.popErrorScope();
        if(error)report.errors.push(error.message);
        await device.queue.onSubmittedWorkDone();
        round.elapsed_ms=performance.now()-started;
        console.log(JSON.stringify({mode,cache,elapsed_ms:round.elapsed_ms}));
        if(report.errors.length||report.lost)break;
    }
    report.complete=true;
}
try{
    let port;for(let n=0;n<200;n++){if(spawnError)throw spawnError;port=stderr.match(/DevTools listening on ws:\/\/127.0.0.1:(\d+)/)?.[1];if(port)break;await delay(100);}
    if(!port)throw Error(stderr);
    const target=await(await fetch(`http://127.0.0.1:${port}/json/new?about:blank`,{method:'PUT'})).json();
    socket=new WebSocket(target.webSocketDebuggerUrl);
    await new Promise((r,j)=>{socket.onopen=r;socket.onerror=j;});
    socket.onmessage=e=>{const m=JSON.parse(e.data),p=pending.get(m.id);if(p){pending.delete(m.id);clearTimeout(p.timer);m.error?p.reject(Error(JSON.stringify(m.error))):p.resolve(m.result);}};
    const call=(method,params={})=>new Promise((resolve,reject)=>{const n=++id;const timer=setTimeout(()=>{pending.delete(n);reject(Error('Timeout '+method));},900000);pending.set(n,{resolve,reject,timer});socket.send(JSON.stringify({id:n,method,params}));});
    await call('Page.enable');await call('Page.navigate',{url:`http://127.0.0.1:${server.address().port}/`});await delay(500);
    await call('Runtime.evaluate',{expression:`(${run.toString()})(${JSON.stringify(mode)}).catch(e=>{globalThis.report||={};report.error=String(e);report.complete=true;});true`});
    let result;
    for(let n=0;n<450;n++){
        await delay(2000);
        const r=await call('Runtime.evaluate',{expression:'globalThis.report',returnByValue:true});result=r.result?.value;
        if(n%10===0)console.log(mode,JSON.stringify(result?.rounds?.map(r=>({cache:r.cache,completed:r.pipelines.filter(p=>p.end_ms!==undefined).length,total:r.pipelines.length}))));
        if(result?.complete)break;
    }
    if(result){result.shader_sha256=shaderHash;result.browser=await call('Browser.getVersion');}
    fs.writeFileSync(path.join(dir,mode+'-report.json'),JSON.stringify(result,null,2));
    console.log(JSON.stringify({mode,rounds:result?.rounds?.map(r=>({cache:r.cache,elapsed_ms:r.elapsed_ms})),lost:result?.lost,error:result?.error,errors:result?.errors}));
    if(!result?.complete||result.error||result.lost||result.errors?.length)process.exitCode=1;
    await call('Browser.close');
}finally{
    for(const p of pending.values())clearTimeout(p.timer);
    socket?.close();chrome.kill();server.close();
    fs.writeFileSync(path.join(dir,mode+'-chrome.log'),stderr);
}
