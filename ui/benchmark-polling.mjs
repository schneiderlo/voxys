// Isolates browser UI polling from the engine and GPU. Both variants use the
// same production UI, dependency versions and synthetic, moving observations.
// Example: node benchmark-polling.mjs --playwright=/path/to/index.mjs
//   --browser=/path/to/chrome --baseline=/tmp/bridge.baseline.mjs --output=/tmp/ui-polling.json
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {build} from 'esbuild';

const args=Object.fromEntries(process.argv.slice(2).map(arg=>{const i=arg.indexOf('=');return [arg.slice(2,i),arg.slice(i+1)];}));
if(!args.playwright||!args.browser||!args.baseline||!args.output)throw Error('Required: --playwright=MODULE --browser=CHROME --baseline=BRIDGE --output=JSON');
const directory=fileURLToPath(new URL('.',import.meta.url)),web=path.resolve(directory,'../web');
const startedUtc=new Date().toISOString();
const {chromium}=await import(pathToFileURL(path.resolve(args.playwright)));
const baseline=await fs.readFile(path.resolve(args.baseline),'utf8'),bundles=new Map();
const sources=Object.fromEntries(await Promise.all(['main.jsx','bridge.js'].map(async name=>[name,await fs.readFile(path.join(directory,'src',name),'utf8')])));
try{sources['shared.js']=await fs.readFile(path.join(directory,'src/shared.js'),'utf8');}catch(error){if(error.code!=='ENOENT')throw error;}
const hasShared=Boolean(sources['shared.js']&&/\binstallShared\b/.test(sources['main.jsx']));
const availableModes=hasShared?['preact','shared']:['preact'];
const modes=args.modes?args.modes.split(','):availableModes;
if(!modes.length||modes.some(mode=>!availableModes.includes(mode)))throw Error(`Available --modes: ${availableModes.join(',')}`);
for(const variant of ['baseline','candidate']){
    const result=await build({absWorkingDir:directory,stdin:{resolveDir:directory,contents:`
        import {options} from 'preact';
        export {${hasShared?'install,installShared':'install'}} from './src/main.jsx';
        const previous=options.diffed;
        options.diffed=vnode=>{
            if(vnode.props?.engine?._get_adventure_state_json)globalThis.uiBenchmarkRenders=(globalThis.uiBenchmarkRenders||0)+1;
            previous?.(vnode);
        };`},bundle:true,minify:true,write:false,outfile:'benchmark.js',format:'iife',globalName:'VoxyBuildUI',
        jsx:'automatic',jsxImportSource:'preact',target:['es2020'],external:['dm-sans-latin.woff2'],
        plugins:[{name:'snapshot-ui-sources',setup(b){b.onLoad({filter:/[/\\]src[/\\](main\.jsx|shared\.js|bridge\.js)$/},args=>{
            const name=path.basename(args.path);
            return {contents:variant==='baseline'&&name==='bridge.js'?baseline:sources[name],loader:name.endsWith('.jsx')?'jsx':'js'};
        });}}]});
    bundles.set('/'+variant+'.js',result.outputFiles.find(f=>f.path.endsWith('.js')).contents);
}
const server=http.createServer(async(request,response)=>{
    const name=new URL(request.url,'http://localhost').pathname;
    if(bundles.has(name)){response.setHeader('Content-Type','text/javascript');response.end(bundles.get(name));return;}
    if(name==='/'){
        response.setHeader('Content-Type','text/html');response.end('<!doctype html><link rel="stylesheet" href="/build_ui.css"><canvas id="voxy-canvas" tabindex="0" style="width:1280px;height:720px"></canvas>');return;
    }
    const file=path.resolve(web,name.slice(1));if(!file.startsWith(web+path.sep)){response.writeHead(403);response.end();return;}
    try{response.setHeader('Content-Type',({'.css':'text/css','.svg':'image/svg+xml','.woff2':'font/woff2'})[path.extname(file)]||'application/octet-stream');response.end(await fs.readFile(file));}
    catch{response.writeHead(404);response.end();}
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser;
try{
    browser=await chromium.launch({executablePath:args.browser,headless:true,args:['--no-sandbox']});
    const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
    const report={scope:'Real Chromium UI with synthetic accepted engine snapshots. Measures JSON parsing, publication and DOM/Preact work; excludes fixture/WASM serialization, GPU work and frame rate.',
        workload:'Sequential polls, each flushed through the microtask queue. DOM writes counted in a separate untimed pass. Three alternating-order runs per variant.',
        source_sha256:{baseline_bridge:hash(baseline),...Object.fromEntries(Object.entries(sources).map(([name,bytes])=>[name,hash(bytes)]))},
        browser:await browser.version(),started_utc:startedUtc,modes,shared_hud_fixture:hasShared,polls_per_run:1000,warmup_polls:100,runs:[]};
    for(const mode of modes)for(let run=0;run<3;run++)for(const variant of run%2?['candidate','baseline']:['baseline','candidate']){
        const page=await browser.newPage({viewport:{width:1280,height:720}}),errors=[];
        page.on('pageerror',error=>errors.push(String(error)));
        await page.goto(`http://127.0.0.1:${server.address().port}/`);
        await page.addScriptTag({url:`/${variant}.js`});
        const measurement=await page.evaluate(async({mode,polls,warmup,includeHud})=>{
            let poll,raw,snapshot={creative:true,ready:true,mode:'build',piece:10,menuToken:1,observation:'1',rows:[],parts:256,
                paint:0,colourAvailable:true,canUndo:true,canRemove:true,valid:true,status:'Ready',dirty:true,saveStatus:'Unsaved build',
                preferencesRevision:'1',cannon:{available:true,nearby:false,active:false,ready:true,wallBusy:false,awaitingHit:false},
                hud:{width:1280,height:720,controls:Array.from({length:15},(_,i)=>({label:'Piece '+(i+1),action:2,value:i+1,intent:65+i,row:-1,enabled:true,x:i*70,y:600,width:60,height:60}))},
                structures:[{id:'1',parts:Array.from({length:256},(_,i)=>({id:String(i+1),kind:10,x:i%16*2,y:0,z:Math.floor(i/16)*4,yaw:0,paint:0}))}]};
            if(!includeHud)delete snapshot.hud;
            raw=JSON.stringify(snapshot);
            const environment={document,addEventListener:window.addEventListener.bind(window),removeEventListener:window.removeEventListener.bind(window),
                setInterval:fn=>{poll=fn;return 1;},clearInterval(){},setTimeout:window.setTimeout.bind(window),clearTimeout:window.clearTimeout.bind(window)};
            const engine={_get_adventure_state_json:()=>1,UTF8ToString:()=>raw,_adventure_action(){}};
            const app=mode==='preact'?VoxyBuildUI.install(engine,environment):VoxyBuildUI.installShared(engine,environment);
            while(!poll)await new Promise(resolve=>setTimeout(resolve,10));
            await new Promise(resolve=>setTimeout(resolve,150));
            const advance=tick=>{
                snapshot={...snapshot,observation:String(tick),player:{tick:String(tick),x:tick/10,y:2,z:1,yaw:tick/100},
                    camera:{yaw:tick/100,width:1280,height:720,eye:[tick/10,3,8],viewProjection:Array(16).fill(tick/100)},
                    forest:{selectionMs:tick/10000,visibleTrees:75,instanceUploadBytes:4096},
                    cannon:{...snapshot.cannon,yaw:tick/100,contacts:tick,shots:tick}};
                raw=JSON.stringify(snapshot);
            };
            for(let i=0;i<warmup;i++){advance(i+2);poll();await Promise.resolve();}
            const host=document.querySelector(mode==='preact'?'#build-ui':'#shared-hud-accessibility');
            const initialRenders=globalThis.uiBenchmarkRenders||0,times=[];
            for(let i=0;i<polls;i++){
                advance(i+warmup+2);
                const start=performance.now();poll();await Promise.resolve();times.push(performance.now()-start);
            }
            const renders=(globalThis.uiBenchmarkRenders||0)-initialRenders;
            // Count writes in a separate pass so MutationObserver delivery does
            // not inflate the baseline timing when it does redundant DOM work.
            let mutations=0;
            const observer=new MutationObserver(records=>{mutations+=records.length;});observer.observe(host,{attributes:true,childList:true,characterData:true,subtree:true});
            for(let i=0;i<polls;i++){
                advance(i+warmup+polls+2);poll();await Promise.resolve();mutations+=observer.takeRecords().length;
            }
            observer.disconnect();
            // Keep a visible update as an oracle: suppression may never freeze controls.
            snapshot={...snapshot,piece:8};raw=JSON.stringify(snapshot);poll();await Promise.resolve();
            const selection=host.querySelector(mode==='preact'?'.bb-piece[aria-pressed="true"]':'.shared-hud-peer[aria-pressed="true"]');
            const changed=mode==='preact'?selection?.getAttribute('aria-label')==='Brick 1 × 2':selection?.dataset.value==='8';
            app.cleanup();
            times.sort((a,b)=>a-b);
            return {snapshot_bytes:raw.length,app_renders:renders,dom_mutations:mutations,visible_selection_updated:changed,
                total_poll_ms:times.reduce((a,b)=>a+b,0),p50_poll_ms:times[Math.ceil(times.length*.50)-1],p95_poll_ms:times[Math.ceil(times.length*.95)-1]};
        },{mode,polls:report.polls_per_run,warmup:report.warmup_polls,includeHud:hasShared});
        assert.deepEqual(errors,[]);assert(measurement.visible_selection_updated,'Visible selection must still update');
        if(variant==='candidate'){assert.equal(measurement.app_renders,0);assert.equal(measurement.dom_mutations,0);}
        report.runs.push({mode,run,variant,...measurement});await page.close();
    }
    const median=values=>[...values].sort((a,b)=>a-b)[Math.floor(values.length/2)];
    report.summary=modes.map(mode=>{
        const values=variant=>report.runs.filter(r=>r.mode===mode&&r.variant===variant);
        const before=median(values('baseline').map(r=>r.total_poll_ms)),after=median(values('candidate').map(r=>r.total_poll_ms));
        return {mode,baseline_median_total_ms:before,candidate_median_total_ms:after,reduction_percent:(1-after/before)*100,
            baseline_renders:values('baseline').map(r=>r.app_renders),candidate_renders:values('candidate').map(r=>r.app_renders),
            baseline_mutations:values('baseline').map(r=>r.dom_mutations),candidate_mutations:values('candidate').map(r=>r.dom_mutations)};
    });
    report.completed_utc=new Date().toISOString();
    await fs.writeFile(path.resolve(args.output),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report.summary,null,2));
}finally{await browser?.close();server.close();}
