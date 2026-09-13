#!/usr/bin/env node
/** One bounded browser adventure journey using ordinary DOM/keyboard/mouse input.
 * Runtime JSON and browser information are read only. No exported game action,
 * snapshot export, state setter, injected event, camera setter, or save edit.
 * A failed run stops once. Continuation requires its confirmed saved checkpoint.
 */
import assert from 'node:assert/strict';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {resolve} from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';

const args=process.argv.slice(2),get=name=>args[args.indexOf(name)+1];
for(const name of ['--playwright','--browser','--package','--output','--profile','--url'])assert(args.includes(name),`Missing ${name}`);
const seconds=Number(args.includes('--seconds')?get('--seconds'):600);
assert(seconds>=120&&seconds<=900,'--seconds must be 120..900');
const output=resolve(get('--output')),profile=resolve(get('--profile')),packagePath=resolve(get('--package'));
const url=new URL(get('--url'));assert(['127.0.0.1','localhost'].includes(url.hostname),'Use the isolated local package server');
await mkdir(output,{recursive:false});
const prior=args.includes('--continue-from')?JSON.parse(await readFile(resolve(get('--continue-from'),'summary.json'),'utf8')):null;
const checkpoint=prior?.checkpoints?.at(-1);
if(prior){assert(checkpoint&&prior.profile===profile&&prior.origin===url.origin,'Continuation needs this profile and a confirmed checkpoint');}
else{await mkdir(profile,{recursive:false});await writeFile(resolve(profile,'voxys-journey-profile'),'adventure-browser-v1\n');}
assert.equal(await readFile(resolve(profile,'voxys-journey-profile'),'utf8'),'adventure-browser-v1\n');
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const manifest=JSON.parse(await readFile(resolve(packagePath,'package.json'),'utf8'));
for(const [name,hash]of Object.entries(manifest.files))assert.equal(sha(await readFile(resolve(packagePath,name))),hash,`Package hash: ${name}`);
assert.deepEqual(await(await fetch(new URL('package.json',url))).json(),manifest,'Server package identity');
const source=await readFile(fileURLToPath(import.meta.url));await writeFile(resolve(output,'executed-driver.mjs'),source);
const began=Date.now(),report={status:'running',startedUtc:new Date().toISOString(),profile,origin:url.origin,
    packagePath,buildId:manifest.buildId,package:manifest,driverSha256:sha(source),stages:prior?prior.stages.slice(0,checkpoint.stageCount):[],
    checkpoints:prior?.checkpoints??[],controls:[],console:[],pageErrors:[],walks:[]};
if(prior)report.continuedFrom=resolve(get('--continue-from'));
let context,page,last={},currentStage='startup',held=new Set();
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const persist=async()=>{report.elapsedSeconds=(Date.now()-began)/1000;report.currentStage=currentStage;await writeFile(resolve(output,'summary.json'),JSON.stringify(report,null,2)+'\n');};
const serial=state=>BigInt(state?.observation??0);
const read=async()=>{try{const next=await page.evaluate(()=>{
    if(typeof voxyModule==='undefined'||!voxyModule?._get_adventure_state_json)return{};
    const pointer=voxyModule._get_adventure_state_json();if(!pointer)return{};
    const raw=voxyModule.UTF8ToString(pointer);if(raw.length>512*1024)throw Error('Observation exceeds 512 Ki characters');
    return JSON.parse(raw);
});if(next.player)last=next;return next;}catch(error){if(page?.isClosed())throw error;return{};}};
async function wait(predicate,label,limit=12000){
    const end=Date.now()+limit;
    while(Date.now()<end){assert(Date.now()-began<seconds*1000,`Journey time limit: ${label}`);const state=await read();if(predicate(state))return state;await sleep(35);}
    throw Error(`${label}: ${JSON.stringify(last)}`);
}
async function fresh(){const before=serial(await read());return wait(s=>serial(s)>before,'fresh observation after released input');}
async function controlState(){const state=await read();return{observation:state.observation,build:state.build,menu:state.menu,saveStatus:state.saveStatus,player:state.player,
    aimInput:state.aimInput,dom:await page.evaluate(()=>({active:document.activeElement.id||document.activeElement.tagName,pointerLock:document.pointerLockElement?.id??'',url:location.href}))};}
async function key(name){const before=await controlState();await page.keyboard.press(name,{delay:100});const state=await fresh();report.controls.push({key:name,before,after:await controlState()});return state;}
async function hold(names,milliseconds){
    for(const name of names){await page.keyboard.down(name);held.add(name);}
    try{await sleep(milliseconds);}finally{for(const name of names){await page.keyboard.up(name);held.delete(name);}}
    const state=await fresh();report.controls.push({keys:names,heldMilliseconds:Math.round(milliseconds),observation:state.observation});return state;
}
async function showControls(){if(await page.locator('#adventure-collapse').getAttribute('aria-expanded')==='false')await page.locator('#adventure-collapse').click();}
async function releaseWorld(){
    assert.equal((await read()).menu,'');
    // A real, harmless button obtains DOM focus, then Escape releases it. This
    // also leaves the panel collapsed so it cannot cover a projected target.
    await page.locator('#adventure-collapse').click();
    if(await page.locator('#adventure-collapse').getAttribute('aria-expanded')==='true')await page.locator('#adventure-collapse').click();
    await key('Escape');assert.equal(await page.evaluate(()=>document.activeElement.tagName),'BODY');
}
async function button(id){await showControls();const before=await controlState();await page.locator(id).click();await fresh();report.controls.push({button:id,before,after:await controlState()});}
async function choose(prefix){
    await showControls();const state=await fresh();const choices=(state.rows??[]).map((row,index)=>({row,index})).filter(({row})=>row.label.toLowerCase().startsWith(prefix.toLowerCase()));
    assert.equal(choices.length,1,`${prefix}: ${JSON.stringify(state.rows)}`);assert.notEqual(choices[0].row.enabled,false);
    await page.locator(`#adventure-menu-rows button[data-row="${choices[0].index}"]`).click();await fresh();
    report.controls.push({menuChoice:prefix,observation:last.observation});
    if(!last.menu)await releaseWorld();
}
async function closeMenu(){
    if((await read()).menu){await showControls();await page.locator('#adventure-menu').click();await wait(s=>s.menu==='','closed interaction menu');}
    await releaseWorld();
}
async function openMenu(){if(!(await read()).menu){await button('#adventure-menu');await wait(s=>s.menu==='Adventure paused','adventure pause menu');}}
async function setBuild(enabled){if((await read()).build!==enabled){await button('#adventure-build');await wait(s=>s.build===enabled,'building toggle button');}await releaseWorld();}
function project(camera,point){const local=point.map((v,i)=>v-camera.origin[i]).concat(1),m=camera.viewProjection;
    const c=[0,1,2,3].map(row=>local.reduce((sum,v,col)=>sum+v*m[col*4+row],0));
    if(c[3]<=0)throw Error('Target behind camera');return[(c[0]/c[3]+1)*camera.width/2,(1-c[1]/c[3])*camera.height/2];}
async function pointer(x,y){
    const box=await page.locator('#voxy-canvas').boundingBox(),camera=(await read()).camera;
    assert(x>=5&&x<camera.width-5&&y>=5&&y<camera.height-5,'Aim inside canvas');
    await page.mouse.move(box.x+x*box.width/camera.width,box.y+y*box.height/camera.height);return fresh();
}
async function drag(dx,dy){const box=await page.locator('#voxy-canvas').boundingBox(),x=box.x+box.width/2,y=box.y+box.height/2;
    await page.mouse.move(x,y);await page.mouse.down({button:'right'});await sleep(60);
    try{await page.mouse.move(x+Math.max(-450,Math.min(450,dx)),y+Math.max(-300,Math.min(300,dy)),{steps:3});}
    finally{await page.mouse.up({button:'right'});}await fresh();report.controls.push({rightDrag:[dx,dy],observation:last.observation});}
async function aim(point){for(let attempt=0;attempt<3;attempt++){
    const state=await read(),camera=state.camera;
    try{const[x,y]=project(camera,point);if(x>=30&&x<camera.width-30&&y>=30&&y<camera.height-30){await pointer(x,y);report.controls.push({pointerWorld:point,pixel:[x,y],observation:last.observation});return last;}}catch{}
    const desired=Math.atan2(-(point[0]-state.player.x),-(point[2]-state.player.z));
    const error=Math.atan2(Math.sin(desired-camera.yaw),Math.cos(desired-camera.yaw));await drag(error/.004,30);
}throw Error(`Cannot aim at ${point}`);}
async function walk(target,tolerance=.24){const end=Date.now()+60000,trace=[];let previous,stalled=0;
    while(Date.now()<end){const state=await read();assert(!state.menu&&!state.build,'World movement must own input');
        const p=state.player,dx=target[0]-p.x,dz=target[1]-p.z,distance=Math.hypot(dx,dz);
        if(distance<=tolerance){report.walks.push({target,tolerance,trace});await persist();return state;}
        stalled=previous!==undefined&&Math.abs(previous-distance)<.015?stalled+1:0;
        if(stalled>=8){report.failedWalk=trace;throw Error(`Actual route blocked: ${target}`);}
        const yaw=state.camera.yaw,f=(-dx*Math.sin(yaw)-dz*Math.cos(yaw))/distance,r=(dx*Math.cos(yaw)-dz*Math.sin(yaw))/distance;
        const keys=[];if(Math.abs(f)>.4)keys.push(f>0?'w':'s');if(Math.abs(r)>.4)keys.push(r>0?'d':'a');
        const duration=Math.min(180,Math.max(35,(distance-tolerance/2)/3.6*.7*1000));trace.push({player:p,keys,duration});previous=distance;await hold(keys,duration);
    }report.failedWalk=trace;throw Error(`Walk timed out: ${target}`);
}
async function record(name,extra={}){currentStage=name;const state=await fresh();assert.match(state.world,/^[0-9a-f]{32}$/);report.stages.push({name,state,...extra});await persist();process.stdout.write(name+'\n');return state;}
const done=name=>report.stages.some(s=>s.name===name);
const inventory=state=>Object.fromEntries(['world','structures','components','wood','stone','scrap','registeredBed','hammerEquipped'].map(k=>[k,state[k]]));
async function save(label,resume=true){await openMenu();await button('#adventure-save');const state=await wait(s=>s.saveStatus==='Saved adventure'&&!s.dirty,'confirmed saved '+label,30000);
    report.checkpoints.push({name:label,world:state.world,stageCount:report.stages.length,state});await persist();if(resume)await closeMenu();return state;}

try{
    const imported=await import(pathToFileURL(resolve(get('--playwright'))));const playwright=imported.chromium?imported:imported.default;
    context=await playwright.chromium.launchPersistentContext(profile,{headless:false,executablePath:resolve(get('--browser')),viewport:{width:1280,height:800},deviceScaleFactor:1,
        args:['--ozone-platform=x11','--enable-features=Vulkan','--enable-unsafe-webgpu','--disable-gpu-watchdog','--disable-background-timer-throttling','--disable-renderer-backgrounding','--disable-dev-shm-usage','--no-first-run','--no-default-browser-check','--no-sandbox']});
    page=context.pages()[0]??await context.newPage();page.setDefaultTimeout(10000);
    page.on('console',message=>report.console.push({type:message.type(),text:message.text()}));
    page.on('pageerror',error=>report.pageErrors.push(String(error)));
    page.on('dialog',dialog=>{report.dialogs??=[];report.dialogs.push({type:dialog.type(),message:dialog.message()});void dialog.dismiss();});
    if(checkpoint){url.searchParams.delete('new');url.searchParams.set('world',checkpoint.world);}
    await page.goto(url.href,{waitUntil:'domcontentloaded',timeout:30000});
    await wait(s=>s.player&&s.camera?.viewProjection?.length===16,'initialized adventure observation',90000);
    await page.locator('#adventure-ui').waitFor({state:'visible',timeout:30000});
    report.browser=await page.evaluate(async()=>{const adapter=await navigator.gpu?.requestAdapter();const i=adapter?.info;return{userAgent:navigator.userAgent,adapter:i?{vendor:i.vendor,architecture:i.architecture,device:i.device,description:i.description,isFallbackAdapter:i.isFallbackAdapter}:null,heapBytes:voxyModule.HEAPU8?.buffer.byteLength};});
    if(checkpoint)assert.deepEqual(inventory(last),inventory(checkpoint.state),'Confirmed checkpoint restored');
    await releaseWorld();
    if(!done('starter-room-built-and-paid')){
        await record('full-landscape-spawn');currentStage='walk to open home site';await walk([-76,-895]);
        // Installed node 10 is the ordinary wood pile at town + (-12, 0).
        // Observe its real prompt and normal gather result, without granting items.
        await wait(s=>s.interaction==='Gather Wood','nearby wood pile prompt');const woodBefore=last.wood;
        await key('e');await wait(s=>s.wood===woodBefore+12,'real wood pile gathered');await record('gathered-installed-wood-pile',{woodBefore,woodGained:12});
        await button('#adventure-starter');await wait(s=>s.build&&s.piece===0,'starter room tool');await releaseWorld();
        await aim([-85,-146.24,-895]);
        for(let i=0;i<3&&!last.valid;i++){assert(last.previewReason.includes('Raise the foundation'),last.previewReason);await key('PageUp');}
        const state=await wait(s=>s.valid,'valid paid starter preview'),origin=['x','y','z'].map(k=>state.preview[k]);
        assert.equal(state.preview.yaw,0);assert(Math.hypot(origin[0]+85,origin[2]+895)<1.5);
        const before={wood:state.wood,stone:state.stone,scrap:state.scrap};await key('e');await wait(s=>s.parts===19,'paid 19-part room');
        assert.deepEqual(Object.fromEntries(Object.keys(before).map(k=>[k,before[k]-last[k]])),{wood:70,stone:16,scrap:8});
        await record('starter-room-built-and-paid',{origin});await setBuild(false);await save('starter-room');
    }
    const[ox,oy,oz]=report.stages.find(s=>s.name==='starter-room-built-and-paid').origin;
    if(!done('manual-floor-place-remove-undo')){
        currentStage='manual building controls';await walk([ox+7,oz+4]);await setBuild(true);await key('Tab');await wait(s=>s.menu==='Building pieces','piece catalogue');await choose('Foundation  |');
        await aim([ox+5,oy+.18,oz+4]);await wait(s=>s.valid,'manual foundation');const base={...last.preview},count=last.parts,stock={wood:last.wood,stone:last.stone,scrap:last.scrap};
        await key('e');await wait(s=>s.parts===count+1,'foundation placed');await key('Tab');await choose('Floor  |');
        const point=[base.x,base.y+.32,base.z];await aim(point);await wait(s=>s.valid,'floor support');await key('e');await wait(s=>s.parts===count+2,'floor placed');
        await aim([base.x,base.y+.64,base.z]);await key('Delete');await wait(s=>s.parts===count+1,'floor removed');
        await aim(point);await wait(s=>s.valid,'floor affordable again');await key('e');await wait(s=>s.parts===count+2,'floor replaced');await key('Control+z');await wait(s=>s.parts===count+1,'floor undone');
        await aim(point);await key('Delete');await wait(s=>s.parts===count,'temporary foundation removed');assert.deepEqual({wood:last.wood,stone:last.stone,scrap:last.scrap},stock);
        await setBuild(false);await record('manual-floor-place-remove-undo');await save('manual-building');
    }
    if(!done('home-functions-and-nonempty-storage')){
        currentStage='walk through the doorway';await walk([ox+7,oz+5]);await walk([ox-1,oz+5]);await walk([ox-1,oz+1]);await record('walked-through-doorway');
        await walk([ox-1,oz+.45]);await key('e');await wait(s=>s.menu==='Chest','reachable chest');const before=last.wood;
        await choose('Store Wood');await wait(s=>s.wood===before-10&&s.components.some(c=>c.kind===2&&c.wood===10),'wood stored');
        await choose('Take Wood');await wait(s=>s.wood===before&&s.components.every(c=>c.wood===0),'wood retrieved');
        await choose('Store Wood');await wait(s=>s.wood===before-10&&s.components.some(c=>c.kind===2&&c.wood===10),'nonempty saved chest');
        await closeMenu();await walk([ox-.58,oz+1.25],.08);const cost={wood:last.wood,scrap:last.scrap};await key('e');
        await wait(s=>s.wood===cost.wood-4&&s.scrap===cost.scrap-2&&s.hammerEquipped,'field hammer crafted and equipped');
        await walk([ox-.1,oz-.5],.06);await key('e');await wait(s=>BigInt(s.registeredBed??0)>0n,'sheltered bed registered');
        await record('home-functions-and-nonempty-storage');await save('usable-home');
    }
    currentStage='confirmed save and ordinary Continue navigation';const saved=await save('final-home',false),expected=inventory(saved);
    await openMenu();await showControls();await page.locator('#adventure-more summary').click();
    await Promise.all([page.waitForURL(candidate=>candidate.searchParams.get('experience')==='adventure'&&!candidate.searchParams.has('new')&&!candidate.searchParams.has('world'),{timeout:30000}),page.locator('#adventure-continue').click()]);
    await wait(s=>s.world===saved.world&&s.saveStatus==='Saved adventure loaded','Continue restored saved namespace',90000);
    assert.deepEqual(inventory(last),expected,'All parts, nonempty chest, inventory, bed and hammer restored');
    assert(Math.hypot(...['x','y','z'].map(k=>last.player[k]-saved.player[k]))<.03,'Saved player location restored');
    await record('continue-reloaded-home-nonempty-chest-bed-and-tool');
    await releaseWorld();await walk([ox-.58,oz+1.25],.08);await walk([ox-1,oz+1]);await walk([ox-1,oz+5]);await walk([ox+7,oz+5]);
    await walk([-76,-895]);const restoredWood=last.wood;assert.notEqual(last.interaction,'Gather Wood');
    await key('e');assert.equal(last.wood,restoredWood,'Saved depleted resource cannot grant wood again');await record('gathered-pile-stayed-depleted-after-reload');
    await walk([ox+7,oz+5]);
    await aim([ox,oy+1.7,oz]);await fresh();await page.screenshot({path:resolve(output,'home-exterior.png')});
    report.exteriorView={file:'home-exterior.png',state:await read()};
    report.finalBrowser=await page.evaluate(()=>({heapBytes:voxyModule.HEAPU8?.buffer.byteLength,canvas:[document.querySelector('#voxy-canvas').width,document.querySelector('#voxy-canvas').height],pointerLocked:!!document.pointerLockElement}));
    report.status='passed';
}catch(error){report.status='failed';report.error=String(error.stack??error);report.lastObservation=last;
    report.lastControlState=await controlState().catch(()=>null);process.stderr.write(report.error+'\n');}
finally{
    for(const name of held)await page?.keyboard.up(name).catch(()=>{});
    await context?.close().catch(error=>{report.cleanupError=String(error);report.status='failed';});
    report.gpuErrors=report.console.filter(c=>/uncaptured.*(?:gpu|error)|WebGPU validation error|Abandoning undrained|\[(?:error|fatal)\s*\]/i.test(c.text));
    if(report.gpuErrors.length||report.pageErrors.length)report.status='failed';
    await persist();
}
process.stdout.write(`${report.status}: ${output}\n`);process.exitCode=report.status==='passed'?0:1;
