#!/usr/bin/env node
// Node 22+, no packages. Real shared-HUD clicks and live game captures.
// Never hides DOM, injects HUD artwork, calls gameplay actions, or changes game state directly.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import http from 'node:http';
import {spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';

const args=Object.fromEntries(process.argv.slice(2).map(arg=>{
    const split=arg.indexOf('=');
    if(!arg.startsWith('--')||split<3)throw Error('Use --name=value');
    return [arg.slice(2,split),arg.slice(split+1)];
}));
if(!args.chrome||(!args.site&&!args.url)||!args.output)
    throw Error('Required: --chrome=PATH --site=DIR (or --url=URL) --output=DIR');
const output=path.resolve(args.output);fs.mkdirSync(output,{recursive:true});
const width=Number(args.width||1600),height=Number(args.height||900);
const dpr=Number(args.dpr||1);assert(Number.isFinite(dpr)&&dpr>=.25&&dpr<=4);
assert(Number.isInteger(width)&&width>=320&&Number.isInteger(height)&&height>=240);
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const report={schema:1,status:'running',method:'Live game; real CDP pointer clicks on runtime-published shared GPU HUD hits; all DOM left intact',
    started:new Date().toISOString(),viewport:{width,height,dpr},captures:[],actions:[],consoleExceptions:[],siteFiles:{}};
const writeReport=()=>fs.writeFileSync(path.join(output,'capture.json'),JSON.stringify(report,null,2)+'\n');
const sockets=[];
let server,browser,child,page;
const profile=args.profile?path.resolve(args.profile):fs.mkdtempSync(path.join(os.tmpdir(),'voxys-shared-hud-'));
report.profile=profile;

async function connect(url){
    const ws=new WebSocket(url);sockets.push(ws);
    await new Promise((resolve,reject)=>{ws.onopen=resolve;ws.onerror=reject;});
    let nextId=0;const pending=new Map();
    ws.onmessage=event=>{
        const message=JSON.parse(event.data),request=pending.get(message.id);
        if(request){
            pending.delete(message.id);clearTimeout(request.timer);
            message.error?request.reject(Error(JSON.stringify(message.error))):request.resolve(message.result);
        }else if(message.method==='Runtime.consoleAPICalled'){
            fs.appendFileSync(path.join(output,'console.log'),message.params.args.map(arg=>arg.value??arg.description??'').join(' ')+'\n');
        }else if(message.method==='Runtime.exceptionThrown'){
            report.consoleExceptions.push(message.params.exceptionDetails);writeReport();
        }
    };
    ws.onclose=()=>{for(const request of pending.values()){clearTimeout(request.timer);request.reject(Error('CDP connection closed'));}pending.clear();};
    return {call:(method,params={})=>new Promise((resolve,reject)=>{
        const id=++nextId;
        const timer=setTimeout(()=>{pending.delete(id);reject(Error('CDP timeout: '+method));},Number(args['cdp-timeout']||180)*1000);
        pending.set(id,{resolve,reject,timer});ws.send(JSON.stringify({id,method,params}));
    })};
}
async function hashFile(file){
    const hash=createHash('sha256');
    for await(const chunk of fs.createReadStream(file))hash.update(chunk);
    return hash.digest('hex');
}
const evaluate=async expression=>{
    const result=await page.call('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});
    if(result.exceptionDetails)throw Error(JSON.stringify(result.exceptionDetails));
    return result.result.value;
};
async function read(){
    return evaluate(`(()=>{
        const engine=typeof voxyModule==='undefined'?null:voxyModule;
        const ready=!!engine?._voxy_is_initialized?.();
        const pointer=ready?engine?._get_adventure_state_json?.():0;
        let state=null;if(pointer)state=JSON.parse(engine.UTF8ToString(pointer));
        const canvas=document.getElementById('voxy-canvas'),rect=canvas?.getBoundingClientRect();
        const host=document.getElementById('shared-hud-accessibility'),loading=document.getElementById('loading');
        const presentationReady=!!canvas&&(!loading||getComputedStyle(loading).display==='none')
            &&document.elementFromPoint(rect.x+rect.width/2,rect.y+rect.height/2)===canvas;
        return {ready,presentationReady,frame:Number(engine?._voxy_get_frame_count?.()||0),state,
            lost:globalThis.voxyDeviceLost||null,gpuErrors:globalThis.voxyUncapturedGpuErrors||[],
            canvas:canvas?{width:canvas.width,height:canvas.height,x:rect.x,y:rect.y,cssWidth:rect.width,cssHeight:rect.height,dpr:devicePixelRatio}:null,
            semantics:{present:!!host,mode:host?.dataset.mode,role:host?.getAttribute('role'),
                peers:[...(host?.querySelectorAll('button')||[])].map(b=>({label:b.getAttribute('aria-label'),disabled:b.disabled,
                    action:Number(b.dataset.action),value:Number(b.dataset.value),row:b.dataset.row===undefined?-1:Number(b.dataset.row),
                    pointerEvents:getComputedStyle(b).pointerEvents})),legacyBuildUi:!!document.getElementById('build-ui')},
            buildId:globalThis.voxyBuildId||null};
    })()`);
}
function checkHealth(snapshot){
    report.last={frame:snapshot.frame,ready:snapshot.ready,mode:snapshot.state?.mode,hits:snapshot.state?.hud?.controls?.length||0,lost:snapshot.lost};
    if(snapshot.lost){report.failureSnapshot=snapshot;throw Error('WebGPU device lost: '+JSON.stringify(snapshot.lost));}
    if(snapshot.gpuErrors.length)throw Error('Uncaptured GPU errors: '+JSON.stringify(snapshot.gpuErrors));
}
async function until(label,predicate,timeout=Number(args['step-timeout']||90)*1000){
    const deadline=Date.now()+timeout;let snapshot,lastProgress=0;
    while(Date.now()<deadline){
        snapshot=await read();checkHealth(snapshot);
        if(predicate(snapshot))return snapshot;
        if(Date.now()-lastProgress>=15000){console.log(label,JSON.stringify(report.last));writeReport();lastProgress=Date.now();}
        await sleep(250);
    }
    report.failureSnapshot=snapshot;
    throw Error(label+' timed out: '+JSON.stringify(report.last));
}
function peersMatch(snapshot){
    const controls=snapshot.state?.hud?.controls||[],peers=snapshot.semantics.peers;
    if(!controls.length||controls.length!==peers.length)return false;
    const key=item=>JSON.stringify([item.action,item.value,item.row,item.label,item.enabled===false||item.disabled===true]);
    return controls.map(key).sort().join('\n')===peers.map(key).sort().join('\n');
}
async function settled(mode,afterFrame=-1){
    return until('Waiting for '+mode,snapshot=>snapshot.ready&&snapshot.presentationReady&&snapshot.state?.ready!==false&&snapshot.state?.creative
        &&snapshot.state.mode===mode&&snapshot.frame>=afterFrame+2&&peersMatch(snapshot));
}
function verify(snapshot){
    const {state,canvas,semantics}=snapshot,hud=state.hud;
    assert(snapshot.presentationReady);assert(state.creative&&state.ready!==false);assert(semantics.present);assert.equal(semantics.legacyBuildUi,false,'The old visual DOM HUD must not be installed');
    assert(Math.abs(canvas.dpr-dpr)<.00001,'Requested browser pixel density was not applied');
    assert.equal(semantics.mode,state.mode);assert(peersMatch(snapshot),'Semantic peers must mirror current shared hits');
    assert.equal(hud.width,canvas.width);assert.equal(hud.height,canvas.height);
    for(const control of hud.controls){
        assert(control.width*canvas.cssWidth/hud.width>=44-.01&&control.height*canvas.cssHeight/hud.height>=44-.01,'Small CSS input target: '+control.label);
        assert(control.x>=0&&control.y>=0&&control.x+control.width<=hud.width+.01&&control.y+control.height<=hud.height+.01);
        assert.equal(Math.floor((control.intent-1)/64),state.menuToken,'Stale input intent');
    }
    for(const peer of semantics.peers)assert.equal(peer.pointerEvents,'none','DOM peers must not intercept native pointer input');
}
async function clickControl(label,match,expectedMode,check=()=>true){
    let before=await until('Finding '+label,snapshot=>snapshot.ready&&snapshot.state?.hud?.controls?.some(c=>c.enabled!==false&&match(c))&&peersMatch(snapshot));
    const mode=before.state.mode;
    const choose=snapshot=>snapshot.state.hud.controls.find(c=>c.enabled!==false&&match(c));
    let control=choose(before);
    const point=(snapshot,hit)=>({x:snapshot.canvas.x+(hit.x+hit.width/2)/snapshot.state.hud.width*snapshot.canvas.cssWidth,
        y:snapshot.canvas.y+(hit.y+hit.height/2)/snapshot.state.hud.height*snapshot.canvas.cssHeight});
    let position=point(before,control);
    await page.call('Input.dispatchMouseEvent',{type:'mouseMoved',...position,button:'none',buttons:0,pointerType:'mouse'});
    before=await until('Hover '+label,snapshot=>snapshot.frame>before.frame&&snapshot.state?.mode===mode&&!!choose(snapshot));
    control=choose(before);position=point(before,control);
    assert(await evaluate(`document.elementFromPoint(${position.x},${position.y})===document.getElementById('voxy-canvas')`),'Canvas input is blocked at '+label);
    await page.call('Input.dispatchMouseEvent',{type:'mousePressed',...position,button:'left',buttons:1,clickCount:1,pointerType:'mouse'});
    try{await until('Input frame '+label,snapshot=>snapshot.frame>before.frame);}finally{
        await page.call('Input.dispatchMouseEvent',{type:'mouseReleased',...position,button:'left',buttons:0,clickCount:1,pointerType:'mouse'});
    }
    const after=await until('Accepted '+label,snapshot=>snapshot.frame>=before.frame+2&&snapshot.state?.mode===expectedMode
        &&snapshot.state?.hud?.controls?.length>0&&peersMatch(snapshot)&&check(snapshot.state));
    assert.equal(after.state.parts,before.state.parts,'A HUD click placed or removed a piece');
    report.actions.push({label,control,position,before:{frame:before.frame,mode:before.state.mode,token:before.state.menuToken},
        after:{frame:after.frame,mode:after.state.mode,token:after.state.menuToken,piece:after.state.piece,paint:after.state.paint,parts:after.state.parts}});
    writeReport();return after;
}
async function capture(name,mode){
    let snapshot=await settled(mode);
    await page.call('Input.dispatchMouseEvent',{type:'mouseMoved',x:24,y:110,button:'none',buttons:0,pointerType:'mouse'});
    snapshot=await settled(mode,snapshot.frame);verify(snapshot);
    const shot=await page.call('Page.captureScreenshot',{format:'png'});
    const file=path.join(output,name+'.png');fs.writeFileSync(file,Buffer.from(shot.data,'base64'));
    fs.writeFileSync(path.join(output,name+'.json'),JSON.stringify(snapshot,null,2)+'\n');
    report.captures.push({name,file:name+'.png',state:name+'.json',sha256:await hashFile(file),frame:snapshot.frame,
        mode:snapshot.state.mode,hits:snapshot.state.hud.controls.length,semanticButtons:snapshot.semantics.peers.length,
        piece:snapshot.state.piece,paint:snapshot.state.paint,parts:snapshot.state.parts});
    console.log('Captured',name,snapshot.frame);writeReport();return snapshot;
}

try{
    let url=args.url;
    if(args.site){
        const site=path.resolve(args.site);
        for(const name of ['index.html','voxy_wasm.js','voxy_wasm.wasm','build_ui.js','build_ui.css']){
            const file=path.join(site,name);
            if(fs.existsSync(file))report.siteFiles[name]={bytes:fs.statSync(file).size,sha256:await hashFile(file)};
        }
        server=http.createServer((request,response)=>{
            let pathname;try{pathname=decodeURIComponent(new URL(request.url,'http://localhost').pathname);}catch{response.writeHead(400);response.end();return;}
            const file=path.resolve(site,pathname==='/'?'index.html':pathname.slice(1));
            if(!file.startsWith(site+path.sep)){response.writeHead(403);response.end();return;}
            fs.readFile(file,(error,data)=>{
                if(error){response.writeHead(404);response.end();return;}
                response.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.mjs':'text/javascript','.css':'text/css',
                    '.wasm':'application/wasm','.svg':'image/svg+xml','.png':'image/png','.json':'application/json','.woff2':'font/woff2'})[path.extname(file)]||'application/octet-stream');
                response.end(data);
            });
        });
        await new Promise(resolve=>server.listen(Number(args.port||0),'127.0.0.1',resolve));
        url='http://127.0.0.1:'+server.address().port+'/index.html';
    }
    const targetUrl=new URL(url);targetUrl.searchParams.set('experience','build');targetUrl.searchParams.set('new','1');targetUrl.searchParams.set('telemetry','0');
    report.url=targetUrl.href;writeReport();
    let stderr='';
    child=spawn(args.chrome,['--headless=new',`--window-size=${width},${height}`,'--remote-debugging-port=0','--user-data-dir='+profile,
        '--no-first-run','--no-default-browser-check','about:blank']);
    child.on('error',error=>{stderr+=String(error);});
    child.stderr.on('data',data=>{stderr+=data;fs.appendFileSync(path.join(output,'chrome.log'),data);});
    let endpoint;
    for(let attempt=0;attempt<200;++attempt){endpoint=stderr.match(/DevTools listening on (ws:\/\/\S+)/)?.[1];if(endpoint)break;await sleep(100);}
    if(!endpoint)throw Error('Chrome did not start: '+stderr);
    browser=await connect(endpoint);
    const target=await(await fetch('http://127.0.0.1:'+new URL(endpoint).port+'/json/new?about:blank',{method:'PUT'})).json();
    page=await connect(target.webSocketDebuggerUrl);await page.call('Runtime.enable');await page.call('Page.enable');
    await page.call('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:dpr,mobile:false});
    await page.call('Emulation.setFocusEmulationEnabled',{enabled:true});
    await page.call('Page.navigate',{url:targetUrl.href});
    let initial=await until('Waiting for game',snapshot=>snapshot.ready&&snapshot.presentationReady&&snapshot.frame>=3&&snapshot.state?.creative&&snapshot.state.ready!==false
        &&snapshot.state.hud?.controls?.length>0&&peersMatch(snapshot),Number(args.timeout||900)*1000);
    if(initial.state.mode==='explore')initial=await clickControl('Enter building',c=>c.action===21,'build');
    assert.equal(initial.state.mode,'build');report.initialParts=initial.state.parts;
    await capture('build','build');
    await clickControl('Open pieces',c=>c.action===23,'catalog');
    await clickControl('Structure category',c=>c.action===13&&c.value===1,'catalog',state=>state.catalogCategory===0);
    await capture('catalog','catalog');
    await clickControl('Choose wall',c=>c.action===10&&c.label==='Wall','build',state=>state.piece===3);
    await clickControl('Open colours',c=>c.action===38,'colours');
    await clickControl('Choose coral',c=>c.action===10&&c.value===0xe57b66,'build',state=>state.paint===0xe57b66);
    await clickControl('Reopen colours',c=>c.action===38,'colours');
    await capture('palette','colours');
    await clickControl('Close colours',c=>c.action===20,'build');
    await clickControl('Walk',c=>c.action===22,'explore');
    const final=await capture('explore','explore');
    assert.equal(final.state.parts,report.initialParts);
    assert(final.state.hud.controls.every(c=>c.action!==2),'Walking still exposes build-piece controls');
    report.status='passed';report.finished=new Date().toISOString();writeReport();
    console.log('PASS: live shared GPU HUD, pointer input, semantic peers, four screenshots.');
}catch(error){
    report.status='failed';report.error=String(error.stack||error);report.finished=new Date().toISOString();writeReport();
    console.error(report.error);process.exitCode=1;
}finally{
    if(browser)try{await browser.call('Browser.close');}catch{}
    for(const socket of sockets)socket.close();server?.close();child?.kill();
}
