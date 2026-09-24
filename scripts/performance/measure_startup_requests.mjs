// Isolate the page's request ordering from the engine and GPU workload.
// Real Chromium parses the real index and runs the real data-pack loader;
// the engine/data bytes are small fixtures. Candidate stylesheets remain
// blocked until all critical requests start, with a ten-second failure bound.
// --baseline=FILE optionally compares a saved index from before a change.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';

const args=Object.fromEntries(process.argv.slice(2).map(arg=>{
    const split=arg.indexOf('=');
    if(!arg.startsWith('--')||split<3)throw Error('Use --name=value');
    return [arg.slice(2,split),arg.slice(split+1)];
}));
if(!args.playwright||!args.browser||!args.output)
    throw Error('--playwright=MODULE --browser=CHROME --output=FILE are required');
const {chromium}=await import(pathToFileURL(path.resolve(args.playwright)));
const web=fileURLToPath(new URL('../../web/',import.meta.url));
const dataLoader=await fs.readFile(path.join(web,'data_packs.js'),'utf8');
const manifest={
    'voxy_wasm.data':{sha256:'a'.repeat(64),size:32},
    'voxy_wasm.wasm':{sha256:'b'.repeat(64),size:8},
};
const browser=await chromium.launch({executablePath:args.browser,headless:true,
    args:['--no-sandbox','--disable-gpu']});
const report={fixture:'Real document/parser and data loader; synthetic engine/data; no GPU workload',
    latency_comparison:false,browser:browser.version(),rounds:[]};
try{
    const inputs=[...(args.baseline?[['baseline',path.resolve(args.baseline)]]:[]),
        ['candidate',path.join(web,'index.html')]];
    for(const [name,indexPath] of inputs){
        const index=await fs.readFile(indexPath,'utf8');
        const runtimeScript=index.match(/<script\s+src="(voxy_wasm\.js\?[^\"]+)"[^>]*><\/script>/);
        assert(runtimeScript,'The document must have its ordinary runtime script');
        // Retain the actual head, DOM and script ordering through the runtime.
        // Exclude application bootstrap, which would start the engine/GPU.
        const html=(index.slice(0,runtimeScript.index+runtimeScript[0].length)+'\n</body></html>')
            .replaceAll('__VOXY_BUILD_ID__','startup-request-fixture')
            .replace('__VOXY_RELEASE_FILES__',JSON.stringify(manifest));
        const requests=[];
        const criticalPaths=new Set(['/voxy_wasm.js','/voxy_wasm.wasm','/voxy_wasm.data']);
        let releaseStyles,gateTimer,gateTimedOut=false;
        const stylesGate=new Promise(resolve=>{releaseStyles=resolve;});
        const waitForStyles=()=>name==='candidate'?stylesGate:new Promise(resolve=>setTimeout(resolve,750));
        const server=http.createServer((req,res)=>{
            const url=new URL(req.url,'http://localhost'),start=performance.now();
            const request={url:url.pathname+url.search,start_ms:start};requests.push(request);
            if(name==='candidate'&&url.pathname==='/index.html'){
                gateTimer=setTimeout(()=>{gateTimedOut=true;releaseStyles();},10000);
            }
            criticalPaths.delete(url.pathname);
            if(criticalPaths.size===0){clearTimeout(gateTimer);releaseStyles();}
            res.setHeader('Cache-Control','public, max-age=600');
            const send=(type,body)=>{
                request.response_ms=performance.now();
                res.setHeader('Content-Type',type);res.end(body);
            };
            if(url.pathname==='/index.html')send('text/html',html);
            else if(url.pathname.endsWith('.css'))waitForStyles().then(()=>send('text/css',''));
            else if(url.pathname==='/data_packs.js')send('text/javascript',dataLoader);
            else if(url.pathname==='/voxy_wasm.js')send('text/javascript',
                'globalThis.runtimeScriptExecutions=(globalThis.runtimeScriptExecutions||0)+1;');
            else if(url.pathname==='/voxy_wasm.wasm')send('application/wasm',Buffer.from([0,97,115,109,1,0,0,0]));
            else if(url.pathname==='/voxy_wasm.data')send('application/octet-stream',Buffer.alloc(32));
            else send('text/javascript','');
        });
        await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
        const context=await browser.newContext(),page=await context.newPage(),errors=[];
        try{
            await context.route('https://www.googletagmanager.com/**',route=>route.fulfill({body:''}));
            // Pages serves HTTP/2. Hold CSS in the browser instead of tying up
            // all six HTTP/1 localhost sockets with the artificial delay.
            await context.route(/\.css(?:\?|$)/,async route=>{
                const url=new URL(route.request().url());
                const request={url:url.pathname+url.search,start_ms:performance.now()};requests.push(request);
                await waitForStyles();
                request.response_ms=performance.now();
                await route.fulfill({contentType:'text/css',body:''});
            });
            await page.addInitScript(()=>{
                // Capability flag only: do not acquire an adapter or device.
                Object.defineProperty(navigator,'gpu',{value:{},configurable:true});
            });
            page.on('pageerror',error=>errors.push(String(error)));
            await page.goto(`http://127.0.0.1:${server.address().port}/index.html`,{waitUntil:'commit'});
            await page.waitForLoadState('load');
            const state=await page.evaluate(async()=>{
                const downloads=await window.voxyStartupDownloads.ready;
                return {runtime_executions:globalThis.runtimeScriptExecutions,
                    data_bytes:downloads.take('voxy_wasm.data',32)?.byteLength,
                    wasm_compiled:downloads.wasmModule instanceof WebAssembly.Module};
            });
            const first=requests[0].start_ms;
            for(const request of requests){request.start_ms-=first;request.response_ms-=first;}
            const css=requests.filter(request=>request.url.endsWith('.css'));
            const files={
                data:requests.find(request=>request.url.startsWith('/voxy_wasm.data?')),
                wasm:requests.find(request=>request.url.startsWith('/voxy_wasm.wasm?')),
                runtime:requests.find(request=>request.url.startsWith('/voxy_wasm.js?')),
            };
            assert(css.length&&Object.values(files).every(Boolean),'Every critical fixture file was requested');
            const firstCssResponse=Math.min(...css.map(request=>request.response_ms));
            const runtimeRequests=requests.filter(request=>request.url.startsWith('/voxy_wasm.js?'));
            assert.equal(runtimeRequests.length,1,'The runtime preload must be reused by the script tag');
            assert.equal(state.runtime_executions,1,'The runtime must execute once');
            assert.equal(state.data_bytes,32);assert.equal(state.wasm_compiled,true);
            assert.deepEqual(errors,[]);
            const early=Object.fromEntries(Object.entries(files).map(([key,request])=>[key,request.start_ms<firstCssResponse]));
            const round={name,first_css_response_ms:firstCssResponse,critical_requests:files,
                stylesheet_fixture:name==='candidate'?'Held until all three critical requests start (10-second failure bound)':'Fixed 750 ms delay',
                stylesheet_gate_timed_out:gateTimedOut,
                started_before_styles:early,runtime_request_count:runtimeRequests.length,...state,errors,requests};
            report.rounds.push(round);
            console.log(JSON.stringify({name,first_css_response_ms:firstCssResponse,
                critical_request_start_ms:Object.fromEntries(Object.entries(files).map(([key,request])=>[key,request.start_ms])),
                started_before_styles:early,runtime_request_count:runtimeRequests.length}));
            if(name==='candidate'){
                assert.equal(gateTimedOut,false,'Critical requests must start without releasing any stylesheet');
                assert(Object.values(early).every(Boolean),'Stylesheets must be released only after all critical requests start');
            }
        }finally{
            clearTimeout(gateTimer);releaseStyles();
            await context.close();
            await new Promise(resolve=>server.close(resolve));
        }
    }
}finally{
    await browser.close();
    await fs.mkdir(path.dirname(path.resolve(args.output)),{recursive:true});
    await fs.writeFile(args.output,JSON.stringify(report,null,2)+'\n');
}
