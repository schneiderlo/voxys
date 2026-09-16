#!/usr/bin/env node
/** Prepared ordinary-input G-B journey. See docs/validation/adventure/G-B/browser-driver.md.
 * Requires explicit owner permission before execution outside CUA. No screenshots,
 * injected events, exported game commands, state setters, save edits or retries.
 */
import assert from 'node:assert/strict';
import {createReadStream} from 'node:fs';
import {readFile,writeFile,mkdir,realpath} from 'node:fs/promises';
import {resolve,relative,isAbsolute} from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';

const argv=process.argv.slice(2),options=new Map();
for(let i=0;i<argv.length;++i){
    const key=argv[i];assert(!options.has(key),`Duplicate argument: ${key}`);
    if(key==='--authorized-ui-driver'){options.set(key,true);continue;}
    assert(['--playwright','--browser','--package','--url','--output','--seconds'].includes(key),`Unknown argument: ${key}`);
    assert(argv[i+1]&&!argv[i+1].startsWith('--'),`Missing value: ${key}`);options.set(key,argv[++i]);
}
assert(options.get('--authorized-ui-driver'),
    'Obtain explicit owner permission for this ordinary-input driver, then pass --authorized-ui-driver. The flag alone is not permission.');
for(const key of ['--playwright','--browser','--package','--url','--output'])assert(options.has(key),`Missing ${key}`);
const seconds=Number(options.get('--seconds')??900);assert(Number.isInteger(seconds)&&seconds>=180&&seconds<=1200);
const packagePath=await realpath(resolve(options.get('--package'))),output=resolve(options.get('--output'));
const inside=(base,path)=>{const rel=relative(base,path);return rel===''||(!rel.startsWith('..')&&!isAbsolute(rel));};
assert(!inside(packagePath,output)&&!inside(output,packagePath),'Output and immutable package must be separate directories');
const entry=new URL(options.get('--url'));
assert(['http:','https:'].includes(entry.protocol)&&['127.0.0.1','localhost','[::1]'].includes(entry.hostname),'Use the isolated local package server');
assert(!entry.username&&!entry.password&&!entry.searchParams.has('world'),'A fresh run must not target an existing world');
assert(!entry.searchParams.has('experience')||entry.searchParams.get('experience')==='adventure');
entry.searchParams.set('experience','adventure');entry.searchParams.set('new','1');entry.searchParams.set('adventureObserve','1');
await mkdir(output,{recursive:false});
const profile=resolve(output,'browser-profile');await mkdir(profile,{recursive:false});
const source=await readFile(fileURLToPath(import.meta.url)),digest=bytes=>createHash('sha256').update(bytes).digest('hex');
await writeFile(resolve(output,'executed-driver.mjs'),source);
const began=Date.now(),report={status:'running',startedUtc:new Date().toISOString(),packagePath,output,profile,entry:entry.href,
    browserPath:resolve(options.get('--browser')),playwrightPath:resolve(options.get('--playwright')),
    driverSha256:digest(source),executionAuthorizationAcknowledged:true,
    scope:'Fresh ordinary-input quest journey. Exact observable ownership, not complete archive-byte equality or visual/performance acceptance.',
    stages:[],controls:[],walks:[],console:[],pageErrors:[],checkpoints:[]};
let context,page,last=null,currentStage='package verification',held=new Set(),manifest;
const sleep=ms=>new Promise(done=>setTimeout(done,ms));
const serial=s=>BigInt(s?.observation??0);
function budget(){assert(Date.now()-began<seconds*1000,`Overall ${seconds}s limit at ${currentStage}`);}
const persist=async()=>{report.currentStage=currentStage;report.elapsedSeconds=(Date.now()-began)/1000;
    await writeFile(resolve(output,'summary.json'),JSON.stringify(report,null,2)+'\n');};
function control(value){assert(report.controls.length<12000,'Control trace limit');report.controls.push(value);}
async function hashFile(path){const hash=createHash('sha256');for await(const bytes of createReadStream(path))hash.update(bytes);return hash.digest('hex');}
async function verifyPackage(){
    const installed=JSON.parse(await readFile(resolve(packagePath,'package.json'),'utf8'));
    assert(installed&&typeof installed.buildId==='string'&&installed.files&&typeof installed.files==='object');
    assert(Object.keys(installed.files).length>0&&Object.keys(installed.files).length<=256);
    for(const [name,hash]of Object.entries(installed.files)){
        const path=await realpath(resolve(packagePath,name));assert(inside(packagePath,path),`Package path escape: ${name}`);
        assert.match(hash,/^[0-9a-f]{64}$/);assert.equal(await hashFile(path),hash,`Immutable package hash: ${name}`);
    }
    const response=await fetch(new URL('package.json',entry),{signal:AbortSignal.timeout(15000)});assert(response.ok);
    assert.deepEqual(await response.json(),installed,'Server must serve the exact supplied package');
    if(manifest)assert.deepEqual(installed,manifest,'Package changed during journey');else manifest=installed;
    return installed;
}
async function read(){
    const result=await page.evaluate(()=>{
        const node=document.getElementById('adventure-observation');
        if(!node)return {snapshot:null,reason:'Local opt-in observation node missing'};
        if(node.getAttribute('type')!=='application/json')throw Error('Observation node is not inert JSON');
        const raw=node.textContent;if(raw.length>512*1024)throw Error('Observation exceeds 512 Ki characters');
        return {snapshot:JSON.parse(raw),reason:raw==='null'?'UI snapshot unavailable':null};
    });
    if(result.snapshot?.player)last=result.snapshot;
    report.lastReadReason=result.reason;return result.snapshot;
}
async function wait(predicate,label,limit=12000){
    const end=Date.now()+limit;
    while(Date.now()<end){budget();const state=await read();if(state&&predicate(state))return state;await sleep(100);}
    throw Error(`${label}; last read: ${report.lastReadReason??'valid'}; observation: ${JSON.stringify(last)}`);
}
async function fresh(){const before=serial(await read());return wait(s=>serial(s)>before,'Fresh released-input observation');}
async function key(name,modifiers=[]){return hold([...modifiers,name],90);}
async function hold(keys,milliseconds){
    budget();assert(milliseconds>=30&&milliseconds<=220);assert(keys.length>0&&keys.length<=3);
    const before=last?.player;
    try{for(const key of keys){await page.keyboard.down(key);held.add(key);}await sleep(milliseconds);}
    finally{for(const key of [...keys].reverse()){await page.keyboard.up(key);held.delete(key);}}
    const state=await fresh();control({keys,milliseconds,before,after:state.player,observation:state.observation});return state;
}
async function click(selector){
    budget();await page.locator(selector).click();await fresh();control({click:selector,observation:last.observation});return last;
}
async function focusWorld(){
    assert(['explore','build'].includes((await read()).mode),'Close the active sheet before world input');
    // Shift+Tab is ordinary browser focus navigation. Clicking a canvas while
    // building could place a part, so it is never used as a focus shortcut.
    for(let step=0;step<28;++step){
        if(await page.evaluate(()=>document.activeElement?.id==='voxy-canvas'))return;
        await key('Tab',['Shift']);
    }
    throw Error('World focus was not reachable through ordinary Shift+Tab');
}
async function closeSheet(){
    if(!['explore','build'].includes((await read()).mode)){
        await click('#adventure-close');await wait(s=>['explore','build'].includes(s.mode),'Sheet closed');
    }
    await focusWorld();
}
async function choose(label,{prefix=false,double=false}={}){
    const state=await read();assert(!['explore','build'].includes(state.mode));
    const choices=state.rows.filter(r=>prefix?r.label.startsWith(label):r.label===label);
    assert.equal(choices.length,1,`Unique named choice ${label}: ${JSON.stringify(state.rows)}`);
    const row=choices[0];assert.equal(row.enabled,true,`Disabled choice ${label}`);
    assert(Number.isSafeInteger(row.intent)&&row.intent>0);
    // The DOM button, not an exported command, activates the captured intent.
    const selector=`#adventure-menu-rows button[data-intent="${row.intent}"]`;
    if(double)await page.locator(selector).dblclick({delay:70});else await page.locator(selector).click();
    await fresh();control({choice:row.label,intent:row.intent,double,observation:last.observation});return last;
}
function cameraBasis(camera){
    assert(camera.viewProjection.length===16&&camera.viewProjection.every(Number.isFinite));
    const normalize=v=>{const length=Math.hypot(...v);assert(length>.1);return v.map(x=>x/length);};
    const m=camera.viewProjection,right=normalize([m[0],m[8]]);
    const forward=normalize([camera.viewTarget[0]-camera.eye[0],camera.viewTarget[2]-camera.eye[2]]);
    assert(Math.abs(right[0]*forward[0]+right[1]*forward[1])<.03,'Camera horizontal basis must be orthogonal');
    return {right,forward};
}
async function walk(target,tolerance=.2){
    currentStage=`walk ${target.join(',')}`;await focusWorld();const trace=[];let previous=Infinity,stalled=0;
    for(let step=0;step<240;++step){
        budget();const state=await read();assert(state.mode==='explore'&&!state.build,'World movement must own input');
        const dx=target[0]-state.player.x,dz=target[1]-state.player.z,distance=Math.hypot(dx,dz);
        if(distance<=tolerance){report.walks.push({target,tolerance,trace});await persist();return state;}
        stalled=Math.abs(previous-distance)<.012?stalled+1:0;assert(stalled<9,`Blocked route to ${target}; ${JSON.stringify(trace.slice(-9))}`);
        const {right,forward}=cameraBasis(state.camera),f=(dx*forward[0]+dz*forward[1])/distance,r=(dx*right[0]+dz*right[1])/distance;
        const keys=[];if(Math.abs(f)>.4)keys.push(f>0?'w':'s');if(Math.abs(r)>.4)keys.push(r>0?'d':'a');
        const milliseconds=Math.min(180,Math.max(35,(distance-tolerance*.5)/3.6*.7*1000));
        trace.push({player:state.player,right,forward,keys,milliseconds});report.activeWalk={target,tolerance,trace};previous=distance;
        await hold(keys,milliseconds);
    }
    throw Error(`240-step walk limit: ${target}`);
}
function project(camera,point){
    const local=point.map((v,i)=>v-camera.origin[i]).concat(1),m=camera.viewProjection;
    const clip=[0,1,2,3].map(row=>local.reduce((sum,v,col)=>sum+v*m[col*4+row],0));
    if(clip[3]<=0)return null;
    return [(clip[0]/clip[3]+1)*camera.width/2,(1-clip[1]/clip[3])*camera.height/2];
}
async function screenPoint(camera,pixel){
    if(!pixel||pixel[0]<24||pixel[0]>camera.width-24||pixel[1]<24||pixel[1]>camera.height-24)return null;
    const box=await page.locator('#voxy-canvas').boundingBox();assert(box);
    const p={x:box.x+pixel[0]*box.width/camera.width,y:box.y+pixel[1]*box.height/camera.height};
    return await page.evaluate(p=>document.elementFromPoint(p.x,p.y)?.id==='voxy-canvas',p)?p:null;
}
async function aim(point){
    await focusWorld();
    for(let attempt=0;attempt<6;++attempt){
        const state=await read(),pixel=project(state.camera,point),p=await screenPoint(state.camera,pixel);
        if(p){await page.mouse.move(p.x,p.y);await fresh();control({aim:point,pixel,observation:last.observation});return last;}
        const {forward}=cameraBasis(state.camera),current=Math.atan2(-forward[0],-forward[1]);
        const desired=Math.atan2(-(point[0]-state.player.x),-(point[2]-state.player.z));
        const error=Math.atan2(Math.sin(desired-current),Math.cos(desired-current));
        const dx=Math.max(-350,Math.min(350,error/.004)),dy=35;
        const start=await screenPoint(state.camera,[state.camera.width*.5,state.camera.height*.34]);assert(start,'Orbit drag start is covered by UI');
        await page.mouse.move(start.x,start.y);await page.mouse.down({button:'right'});
        try{await page.mouse.move(start.x+dx,start.y+dy,{steps:4});}finally{await page.mouse.up({button:'right'});}
        await fresh();control({rightDrag:[dx,dy],observation:last.observation});
    }
    throw Error(`Cannot frame unobstructed placement target ${point}; no screenshot retry`);
}
const materials=s=>({wood:s.wood,stone:s.stone,scrap:s.scrap});
const spent=(before,after)=>Object.fromEntries(Object.keys(before).map(k=>[k,before[k]-after[k]]));
function ownership(s){
    return {world:s.world,structures:s.structures,components:s.components,...materials(s),registeredBed:s.registeredBed,
        hammerEquipped:s.hammerEquipped,equippedUtility:s.equippedUtility,metNpcMask:s.metNpcMask,saveSchema:s.saveSchema,
        quest:{phase:s.quest.phase,recipeUnlocked:s.quest.recipeUnlocked,rewardRevision:s.quest.rewardRevision}};
}
async function record(name,extra={}){currentStage=name;const state=await fresh();assert.match(state.world,/^[0-9a-f]{32}$/);
    report.stages.push({name,state,...extra});await persist();process.stdout.write(name+'\n');return state;}
async function talkMoss(){
    const moss=(await read()).residents.find(n=>n.id===1);assert(moss?.available,'Moss must have an admitted pose');
    await walk([moss.x,moss.z+1.1],.15);await wait(s=>/Moss/.test(s.interaction),'Moss reachable prompt');
    await key('e');await wait(s=>s.mode==='dialogue'&&s.dialogue?.npcId===1,'Moss dialogue');
}
async function save(){
    await closeSheet();assert.equal(last.mode,'explore');await click('#adventure-menu');await wait(s=>s.mode==='pause','Pause');
    await choose('Save adventure');const saved=await wait(s=>s.saveStatus==='Saved adventure'&&!s.dirty,'Confirmed manual save',30000);
    report.checkpoints.push({state:saved,url:page.url()});await persist();return saved;
}
function verifyBearing(state,target){
    const c=state.compass;assert(c.equipped&&c.available&&Number.isFinite(c.bearing)&&Number.isFinite(c.distance));
    const dx=target[0]-state.player.x,dz=target[1]-state.player.z;
    assert(Math.abs(c.distance-Math.hypot(dx,dz))<.05,'Compass horizontal distance matches actual positions');
    const expected=(Math.atan2(dx,-dz)*180/Math.PI+360)%360;
    const error=Math.abs(((c.bearing-expected+540)%360)-180);assert(error<.05,'Compass bearing is clockwise from north');
}
let watchdog;
try{
    report.package=await verifyPackage();report.buildId=manifest.buildId;
    await writeFile(resolve(output,'package-sha256.json'),JSON.stringify(manifest,null,2)+'\n');await persist();
    const imported=await import(pathToFileURL(resolve(options.get('--playwright'))).href),playwright=imported.chromium?imported:imported.default;
    watchdog=setTimeout(()=>{report.timedOut=true;void context?.close();},seconds*1000);
    currentStage='launch fresh isolated browser';
    context=await playwright.chromium.launchPersistentContext(profile,{headless:false,executablePath:resolve(options.get('--browser')),
        viewport:{width:1280,height:800},deviceScaleFactor:1,timeout:60000,
        args:['--ozone-platform=x11','--enable-features=Vulkan','--enable-unsafe-webgpu','--disable-gpu-watchdog',
            '--disable-background-timer-throttling','--disable-renderer-backgrounding','--disable-dev-shm-usage','--no-first-run','--no-default-browser-check']});
    page=context.pages()[0]??await context.newPage();page.setDefaultTimeout(12000);
    report.browserVersion=context.browser()?.version()??'unavailable';
    page.on('console',m=>{if(report.console.length<1000)report.console.push({type:m.type(),text:m.text().slice(0,8000)});else report.consoleTruncated=true;});
    page.on('pageerror',e=>report.pageErrors.push(String(e)));
    page.on('dialog',dialog=>{report.unexpectedDialog={type:dialog.type(),message:dialog.message()};void dialog.dismiss();});
    await page.goto(entry.href,{waitUntil:'domcontentloaded',timeout:30000});
    await wait(s=>s.ready!==false&&s.player&&s.camera?.viewProjection?.length===16,'Initialized local observation',90000);
    await page.locator('#adventure-ui').waitFor({state:'visible'});
    assert.equal(last.parts,0);assert.equal(last.quest.phase,'not-accepted');assert.equal(last.registeredBed,'0');
    assert.equal(last.equippedUtility.kind,0);assert.equal(last.hammerEquipped,false);
    await focusWorld();const first=await read(),basis=cameraBasis(first.camera);
    await hold(['d'],120);const movement=[last.player.x-first.player.x,last.player.z-first.player.z];
    assert(movement[0]*basis.right[0]+movement[1]*basis.right[1]>.05,'D must move toward actual rendered screen-right');
    await record('fresh-world-and-rendered-movement-basis',{basis,movement});
    await talkMoss();await choose('Accept: A Place to Return');await wait(s=>s.quest.phase==='active','Home quest accepted');
    assert.equal(last.quest.recipeUnlocked,false);await record('moss-home-quest-accepted');await closeSheet();
    await walk([-68.5,-892]);await walk([-76,-892]);await walk([-76,-895]);
    currentStage='visible Starter room discovery';await key('b');await wait(s=>s.mode==='build','B opens build tray');
    assert(await page.locator('#adventure-starter').isVisible(),'Starter room must be visible without Pause');
    await click('#adventure-individual');await wait(s=>s.mode==='catalog','Building picker');
    for(let category=0;category<3;++category){
        await click(`#adventure-category-${category}`);await wait(s=>s.catalogCategory===category,'Selected build category');
        assert.equal(last.rows[0].label,'Starter room');assert.equal(last.rows[0].pieceKind,0);
        assert(last.rows[0].detail.includes('70')&&last.rows[0].detail.includes('16')&&last.rows[0].detail.includes('8'));
    }
    await choose('Starter room');await wait(s=>s.mode==='build'&&s.piece===0,'Paid room preview selected');
    await click('#adventure-starter');await focusWorld();await aim([-85,-146.24,-895]);
    for(let raise=0;raise<4&&!last.valid;++raise){assert(/Raise the foundation/i.test(last.previewReason),last.previewReason);await key('PageUp');}
    const valid=await wait(s=>s.valid&&s.piece===0,'Valid west home preview');assert.equal(valid.preview.yaw,0);
    const origin=[valid.preview.x,valid.preview.y,valid.preview.z],before=materials(valid);
    assert(Math.hypot(origin[0]+85,origin[2]+895)<1.5,'House stays in the known open west site');
    await key('e');await wait(s=>s.parts===19&&s.mode==='explore','Room placement accepted');
    assert.deepEqual(spent(before,last),{wood:70,stone:16,scrap:8});assert.equal(last.structures.length,1);
    await record('starter-room-built-and-paid',{origin,cost:spent(before,last)});
    const [ox,oy,oz]=origin;
    const enter=async()=>{await walk([ox+7,oz+5]);await walk([ox-1,oz+5]);await walk([ox-1,oz+1]);};
    const leave=async()=>{await walk([ox-1,oz+1]);await walk([ox-1,oz+5]);await walk([ox+7,oz+5]);};
    await enter();assert(last.player.y>oy+.3);await record('walked-through-doorway');
    await walk([ox-1,oz+.45]);await key('e');await wait(s=>s.mode==='chest','Usable chest');
    const chest=last.components.find(c=>c.kind===2);assert(chest);const wood=last.wood;
    await choose('Store Wood',{prefix:true});await wait(s=>s.wood===wood-10&&s.components.find(c=>c.id===chest.id)?.wood===10,'Store ten wood');
    await choose('Take Wood',{prefix:true});await wait(s=>s.wood===wood&&s.components.find(c=>c.id===chest.id)?.wood===0,'Take ten wood');
    await choose('Store Wood',{prefix:true});await wait(s=>s.wood===wood-10&&s.components.find(c=>c.id===chest.id)?.wood===10,'Keep nonempty chest');
    await closeSheet();await walk([ox-.58,oz+1.25],.08);await key('e');await wait(s=>s.mode==='workbench','Workbench recipe menu');
    assert.equal(last.rows.find(r=>r.label==='Trail compass: help Moss first')?.enabled,false);
    const hammerCost=materials(last);await choose('Craft field hammer');await wait(s=>s.hammerEquipped,'Field hammer crafted and equipped');
    assert.deepEqual(spent(hammerCost,last),{wood:4,stone:0,scrap:2});await closeSheet();
    await walk([ox-.1,oz-.5],.06);await key('e');await wait(s=>BigInt(s.registeredBed)>0n&&s.quest.ready,'Sheltered bed registered; home ready');
    await record('real-home-bed-chest-workbench-ready');await leave();await walk([-69,-891]);await talkMoss();
    const beforeReward=materials(last);await choose('Complete quest: learn compass',{double:true});
    await wait(s=>s.quest.phase==='completed'&&s.quest.recipeUnlocked&&BigInt(s.quest.rewardRevision)>0n,'Permanent quest recipe receipt');
    const receipt=last.quest.rewardRevision;assert.deepEqual(materials(last),beforeReward,'Quest grants a recipe, not free materials or a compass');
    assert.equal(last.equippedUtility.kind,0);assert.equal(last.compass.backpackSlot,null);
    await closeSheet();await talkMoss();assert(!last.rows.some(r=>/Complete quest/.test(r.label)),'Completed reward cannot be claimed again');
    await choose('See you soon');assert.equal(last.quest.rewardRevision,receipt);assert.deepEqual(materials(last),beforeReward);
    await record('moss-recipe-earned-once-and-repeat-unavailable');await closeSheet();
    await walk([-69,-891]);await enter();await walk([ox-.58,oz+1.25],.08);await key('e');await wait(s=>s.mode==='workbench','Home workbench after quest');
    const compassCost=materials(last);await choose('Craft trail compass');
    await wait(s=>s.compass.backpackSlot!==null&&s.equippedUtility.kind===0,'Crafted compass owned in backpack');
    assert.deepEqual(spent(compassCost,last),{wood:2,stone:0,scrap:4});assert.equal(last.compass.available,false,'Backpack possession does not activate compass');
    await choose('Equip trail compass');await wait(s=>s.equippedUtility.kind===5&&s.equippedUtility.quantity===1&&s.compass.backpackSlot===null,'Atomic compass equip');
    await closeSheet();await leave();assert.equal(last.compass.target,'relay');verifyBearing(last,[-63,-975]);
    assert(await page.locator('#adventure-compass').isVisible());const distance=last.compass.distance;
    await walk([ox+7,oz+2]);verifyBearing(last,[-63,-975]);assert(Math.abs(last.compass.distance-distance)>1,'Compass follows real movement');
    await click('#adventure-compass-target');await wait(s=>s.compass.target==='home'&&s.compass.available,'Usable home compass target');
    assert(last.compass.distance>1&&last.compass.distance<15);assert(Number.isFinite(last.compass.bearing));
    await click('#adventure-compass-target');await wait(s=>s.compass.target==='relay','Beacon target restored');verifyBearing(last,[-63,-975]);
    await record('paid-compass-owned-equipped-and-tracks-real-targets');
    const saved=await save(),expected=ownership(saved),savedUrl=new URL(page.url());
    assert.equal(savedUrl.searchParams.get('world'),saved.world);assert(!savedUrl.searchParams.has('new'));
    assert.equal(savedUrl.searchParams.get('adventureObserve'),'1');
    currentStage='ordinary same-world reload';await page.reload({waitUntil:'domcontentloaded',timeout:30000});
    await wait(s=>s.world===saved.world&&s.saveStatus==='Saved adventure loaded','Exact saved namespace restored',90000);
    assert.deepEqual(ownership(last),expected,'All observed parts, nonempty chest, materials, equipment and permanent quest receipt restored exactly');
    assert(Math.hypot(...['x','y','z'].map(k=>last.player[k]-saved.player[k]))<.03,'Player position restored');
    verifyBearing(last,[-63,-975]);await record('saved-ownership-and-progression-reloaded-exactly');
    await closeSheet();await walk([-69,-891]);await talkMoss();
    assert(!last.rows.some(r=>/Complete quest/.test(r.label)));await choose('See you soon');
    assert.deepEqual(ownership(last),expected,'Repeating completed dialogue after reload grants nothing');
    await record('completed-quest-stays-completed-after-reload');
    await verifyPackage();report.finalObservation=await read();report.status='passed';
}catch(error){report.status='failed';report.error=String(error.stack??error);report.lastObservation=last;
    if(page&&!page.isClosed())report.lastDom=await page.evaluate(()=>({url:location.href,focus:document.activeElement?.id,
        pointerLocked:!!document.pointerLockElement,uiMode:document.getElementById('adventure-ui')?.dataset.mode})).catch(()=>null);
    process.stderr.write(report.error+'\n');
}finally{
    clearTimeout(watchdog);
    for(const key of held)await page?.keyboard.up(key).catch(()=>{});
    await page?.mouse.up({button:'right'}).catch(()=>{});
    await context?.close().catch(error=>{report.cleanupError=String(error);report.status='failed';});
    report.gpuErrors=report.console.filter(c=>/uncaptured.*(?:gpu|error)|WebGPU validation error|Abandoning undrained|\[(?:error|fatal)\s*\]/i.test(c.text));
    if(report.gpuErrors.length||report.pageErrors.length||report.unexpectedDialog||report.timedOut||report.consoleTruncated)report.status='failed';
    await persist();
}
process.stdout.write(`${report.status}: ${output}\n`);process.exitCode=report.status==='passed'?0:1;
