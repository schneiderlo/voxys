import fs from 'node:fs';import path from 'node:path';import os from 'node:os';
import http from 'node:http';import {spawn} from 'node:child_process';import {createHash} from 'node:crypto';
const root=path.dirname(new URL(import.meta.url).pathname).replace(/^\/(\w:)/,'$1');
const sha256=bytes=>createHash('sha256').update(bytes).digest('hex');
const source=new URL('.',import.meta.url);const profile=fs.mkdtempSync(path.join(os.tmpdir(),'voxys-combined-daytime-oracle-'));
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));let server,chrome,browser;const sockets=[];
const report={purpose:'Independent Windows Intel linked combined daytime shader oracle; not a gameplay timing',startedAt:new Date().toISOString(),inputs:{export:JSON.parse(fs.readFileSync(new URL('export-result.json',source))),provenance:JSON.parse(fs.readFileSync(new URL('fixtures.json',source))).provenance},
    harness:{browserSha256:sha256(fs.readFileSync(new URL('oracle-browser.js',source))),runnerSha256:sha256(fs.readFileSync(new URL('run-windows-oracle.mjs',source)))}};
fs.mkdirSync(new URL('outputs/',source),{recursive:true});
const save=()=>fs.writeFileSync(new URL('hardware-result.json',source),JSON.stringify(report,null,2));
async function connect(endpoint,event) {
    const socket=new WebSocket(endpoint);sockets.push(socket);await new Promise((resolve,reject)=>{socket.onopen=resolve;socket.onerror=reject;});
    const pending=new Map();let id=0;
    socket.onmessage=message=>{const value=JSON.parse(message.data);if(pending.has(value.id)){const item=pending.get(value.id);pending.delete(value.id);clearTimeout(item.timer);value.error?item.reject(Error(JSON.stringify(value.error))):item.resolve(value.result);}else if(value.method)event?.(value);};
    socket.onclose=()=>{for(const item of pending.values()){clearTimeout(item.timer);item.reject(Error('CDP connection closed'));}pending.clear();};
    return {call:(method,params={})=>new Promise((resolve,reject)=>{if(socket.readyState!==WebSocket.OPEN){reject(Error('CDP socket closed'));return;}const sequence=++id;const timer=setTimeout(()=>{pending.delete(sequence);reject(Error('CDP timeout: '+method));},method==='Browser.close'?5000:300000);pending.set(sequence,{resolve,reject,timer});socket.send(JSON.stringify({id:sequence,method,params}));})};
}
try {
    server=http.createServer(async(request,response)=>{
        if(request.method==='POST'&&/^\/results\/render-[0-9]+\.bin$/.test(request.url)) {
            const chunks=[];for await(const chunk of request)chunks.push(chunk);const raw=Buffer.concat(chunks);
            fs.writeFileSync(new URL('outputs/'+request.url.slice('/results/'.length),source),raw);response.writeHead(200,{'Content-Type':'application/json'});response.end(JSON.stringify({sha256:sha256(raw)}));return;
        }
        if(request.url==='/') {response.writeHead(200,{'Content-Type':'text/html'});response.end('<!doctype html><title>Combined terrain mesh water image correctness</title><script type="module" src="oracle-browser.js"></script>');return;}
        const filename=request.url.split('?')[0].slice(1);
        if(!/^(?:[a-z0-9-]+\.(json|js)|blobs\/[a-f0-9]{64}\.(bin|wgsl))$/.test(filename)){response.writeHead(404);response.end();return;}
        fs.readFile(new URL(filename,source),(error,bytes)=>{if(error){response.writeHead(404);response.end();return;}
            response.writeHead(200,{'Content-Type':filename.endsWith('.js')?'text/javascript':'application/octet-stream'});response.end(bytes);});
    });
    await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
    const arguments_=['--headless=new','--remote-debugging-port=0',`--user-data-dir=${profile}`,'--no-first-run','--no-default-browser-check','--disable-background-networking','about:blank'];
    report.chromeArguments=arguments_.filter(x=>!x.startsWith('--user-data-dir='));let stderr='';
    chrome=spawn('C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',arguments_);const closed=new Promise(resolve=>chrome.once('close',resolve));
    chrome.stderr.on('data',bytes=>{stderr+=bytes;fs.appendFileSync(new URL('chrome.log',source),bytes);});
    let endpoint;for(let i=0;i<300;++i){endpoint=stderr.match(/DevTools listening on (ws:\/\/\S+)/)?.[1];if(endpoint)break;await sleep(100);}
    if(!endpoint)throw Error('Chrome did not launch');browser=await connect(endpoint);
    report.browser=await browser.call('Browser.getVersion');report.system=await browser.call('SystemInfo.getInfo');save();
    const target=await(await fetch(`http://127.0.0.1:${new URL(endpoint).port}/json/new?about:blank`,{method:'PUT'})).json();
    const page=await connect(target.webSocketDebuggerUrl,event=>{if(event.method==='Runtime.consoleAPICalled'){const message=event.params.args.map(a=>a.value??a.description??'').join(' ');console.log(message);fs.appendFileSync(new URL('console.log',source),message+'\n');}});
    await page.call('Runtime.enable');await page.call('Page.enable');await page.call('Page.navigate',{url:`http://127.0.0.1:${server.address().port}/`});
    const deadline=Date.now()+300000;for(;;){const ready=await page.call('Runtime.evaluate',{expression:'typeof window.oracle !== "undefined"',returnByValue:true});if(ready.result.value)break;if(Date.now()>deadline)throw Error('Oracle page did not start');await sleep(100);}
    const value=await page.call('Runtime.evaluate',{expression:'window.oracle',awaitPromise:true,returnByValue:true});
    if(value.exceptionDetails)throw Error(JSON.stringify(value.exceptionDetails));report.result=value.result.value;
    report.systemAfter=await browser.call('SystemInfo.getInfo');
    report.finishedAt=new Date().toISOString();save();if(!report.result.ok)process.exitCode=2;
    try{await browser.call('Browser.close');}catch{}await Promise.race([closed,sleep(5000)]);
}catch(error){report.error=String(error);save();throw error;}
finally{if(browser)try{await browser.call('Browser.close');}catch{}for(const socket of sockets)socket.close();if(chrome&&!chrome.killed)chrome.kill();if(server)server.close();try{fs.rmSync(profile,{recursive:true,force:true,maxRetries:5,retryDelay:200});}catch{}}
