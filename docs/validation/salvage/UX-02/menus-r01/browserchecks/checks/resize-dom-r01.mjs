import {readFile,writeFile,mkdtemp,rm} from 'node:fs/promises';
import {spawn} from 'node:child_process';
const files=await Promise.all(['web/index.html','web/salvage_preview.css','web/controller_menu.js','web/cove_preferences.js','build-cove-ux-r01/browser-ui-r03/summary.json'].map(p=>readFile(p,'utf8')));
const original=files[0],panel=original.slice(original.indexOf('<section id="salvage-preview"'),original.indexOf('<script src="salvage_preview.js"'));
const prefs=JSON.parse(files[4]).preferences;
const html=`<!doctype html><html><head><style>body{margin:0;overflow:hidden} ${files[1]}</style></head><body><canvas id="voxy-canvas" tabindex="0"></canvas>${panel}<script>${files[2]}</script><script>${files[3]}</script><script>
window.trace=[];const panel=document.getElementById('salvage-preview');panel.hidden=false;panel.dataset.scene='cove';panel.dataset.workshop='false';
for(const e of panel.querySelectorAll('[data-cove-section]'))e.hidden=e.dataset.coveSection!=='settings';
for(const id of ['salvage-workshop','salvage-field-tools','salvage-camera','cove-practice','salvage-objective','salvage-help','salvage-scope'])if(document.getElementById(id))document.getElementById(id).hidden=true;
const more=document.getElementById('salvage-more-controls');more.open=true;
window.snap=label=>{const e=document.activeElement,r=e.getBoundingClientRect(),p=panel.getBoundingClientRect();trace.push({label,at:performance.now(),width:innerWidth,height:innerHeight,focus:e.id,scrollTop:panel.scrollTop,font:getComputedStyle(panel).fontSize,top:r.top,bottom:r.bottom,panelBottom:p.bottom,owner:window.voxyControllerMenuActive?.()});};
window.addEventListener('resize',()=>snap('resize-before-controller'));
const menu=VoxyControllerMenu.install(window,panel);const state={active:true,ready:true,world:'test',gamepad:{connected:true},workshop:{open:false},session:{admissionOpen:true},ui:${JSON.stringify(prefs)}};
const settings=VoxyCovePreferences.install({ccall:()=>JSON.stringify(${JSON.stringify(prefs)})},window,document.getElementById('cove-preferences'),menu);
settings.tick(state);menu.tick(state);menu.openSection(more);
const apply=document.getElementById('cove-preferences-apply');apply.focus();apply.scrollIntoView({block:'nearest'});
window.addEventListener('resize',()=>snap('resize-after-controller'));window.addEventListener('scroll',()=>snap('scroll'),true);snap('ready');
</script></body></html>`;
const profile=await mkdtemp('/tmp/voxys-dom-resize-');const child=spawn('/opt/google/chrome/chrome',['--headless=new','--disable-gpu','--no-sandbox','--no-first-run','--no-default-browser-check','--remote-debugging-port=0',`--user-data-dir=${profile}`,'about:blank'],{stdio:['ignore','ignore','pipe']});let logs='',ws;
child.stderr.on('data',x=>logs+=x);const delay=ms=>new Promise(r=>setTimeout(r,ms));
try{let port;for(let i=0;i<200;i++){port=logs.match(/DevTools listening on ws:\/\/127\.0\.0\.1:(\d+)\//)?.[1];if(port)break;await delay(50);}if(!port)throw Error(logs);
const target=await(await fetch(`http://127.0.0.1:${port}/json/new?about:blank`,{method:'PUT'})).json();ws=new WebSocket(target.webSocketDebuggerUrl);await new Promise(r=>ws.addEventListener('open',r,{once:true}));let serial=0;const pending=new Map();
ws.addEventListener('message',e=>{const m=JSON.parse(e.data),p=pending.get(m.id);if(p){pending.delete(m.id);m.error?p.reject(Error(JSON.stringify(m.error))):p.resolve(m.result);}});
const call=(method,params={})=>new Promise((resolve,reject)=>{const id=++serial;pending.set(id,{resolve,reject});ws.send(JSON.stringify({id,method,params}));});
const evaluate=async expression=>{const x=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(x.exceptionDetails)throw Error(JSON.stringify(x.exceptionDetails));return x.result.value;};
await call('Emulation.setFocusEmulationEnabled',{enabled:true});await call('Emulation.setDeviceMetricsOverride',{width:1280,height:720,deviceScaleFactor:1,mobile:false});await call('Page.navigate',{url:'data:text/html,'+encodeURIComponent(html)});
await delay(250);await evaluate("snap('before-resize')");await call('Emulation.setDeviceMetricsOverride',{width:640,height:480,deviceScaleFactor:1,mobile:false});await evaluate("snap('resize-call-returned')");await delay(150);await evaluate("snap('150ms')");await evaluate("new Promise(resolve=>requestAnimationFrame(()=>{snap('animation-frame');resolve()}))");const report=await evaluate('trace');await writeFile('build-cove-ux-r01/checks/resize-dom-r01.json',JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}finally{ws?.close();child.kill('SIGTERM');await new Promise(r=>child.once('close',r));await rm(profile,{recursive:true,force:true});}
