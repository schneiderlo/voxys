// Optional real-protocol regression; no engine or GPU workload.
// Node 22+: node scripts/test_startup_observation_browser.mjs /path/to/chrome
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {mkdtemp,rm} from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import {tmpdir} from 'node:os';
import {cdpProtocolError,observeStartup,waitForStartupCommit} from './startup_observation.mjs';

assert(process.argv[2],'Pass an explicit Chrome executable path');
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const server=http.createServer((request,response)=>{
    response.setHeader('Content-Type','text/html');
    response.end(request.url.startsWith('/index.html')
        ? '<script>globalThis.ready=Promise.resolve("ready");</script>'
        : '<script>globalThis.ready=new Promise(()=>{});</script>');
});
await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});
const directory=await mkdtemp(path.join(tmpdir(),'voxys-navigation-test-'));
const chrome=spawn(process.argv[2],['--headless=new','--no-sandbox','--disable-dev-shm-usage',
    '--no-first-run','--no-default-browser-check','--remote-debugging-port=0',
    `--user-data-dir=${directory}`,'about:blank'],{stdio:['ignore','ignore','pipe']});
let logs='',socket,spawnError;
chrome.stderr.on('data',data=>logs+=data);
const shutdown=new Promise(resolve=>{
    chrome.once('close',resolve);chrome.once('error',error=>{spawnError=error;resolve();});
});
try{
    let port;
    for(let n=0;n<2400&&!port;n++){
        if(spawnError)throw spawnError;
        port=logs.match(/DevTools listening on ws:\/\/127\.0\.0\.1:(\d+)\//)?.[1];
        if(!port)await delay(25);
    }
    assert(port,'Chrome debugging unavailable: '+logs);
    const target=await(await fetch(`http://127.0.0.1:${port}/json/new?about:blank`,{method:'PUT'})).json();
    socket=new WebSocket(target.webSocketDebuggerUrl);
    const pending=new Map(),navigations=[];let sequence=0;
    socket.addEventListener('message',event=>{
        const message=JSON.parse(event.data),request=pending.get(message.id);
        if(message.method==='Page.frameNavigated'&&!message.params.frame.parentId)navigations.push(message.params.frame);
        if(!request)return;pending.delete(message.id);
        message.error?request.reject(cdpProtocolError(request.method,message.error)):request.resolve(message.result);
    });
    socket.addEventListener('close',()=>{
        for(const request of pending.values())request.reject(new Error('Chrome closed'));pending.clear();
    });
    await new Promise((resolve,reject)=>{
        socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',reject,{once:true});
    });
    const call=(method,params={},timeoutMs=30000)=>new Promise((resolve,reject)=>{
        const id=++sequence,timer=setTimeout(()=>{pending.delete(id);reject(new Error('Timeout '+method));},timeoutMs);
        pending.set(id,{method,resolve:value=>{clearTimeout(timer);resolve(value);},reject:error=>{clearTimeout(timer);reject(error);}});
        socket.send(JSON.stringify({id,method,params}));
    });
    await call('Page.enable');await call('Runtime.enable');
    const params={awaitPromise:true,returnByValue:true,expression:'globalThis.ready'};
    const base=`http://127.0.0.1:${server.address().port}`;
    for(const retry of [false,true]){
        const url=base+'/?test='+Number(retry)+'#keep',deadline=Date.now()+30000;
        const firstNavigation=navigations.length;
        await call('Page.navigate',{url});
        while(!navigations.slice(firstNavigation).some(frame=>frame.url===url.split('#')[0])){
            assert(Date.now()<deadline,'Fixture document did not commit');await delay(10);
        }
        // Confirm the initial fixture has run before capturing its Promise.
        const initialized=await call('Runtime.evaluate',{awaitPromise:true,returnByValue:true,expression:`new Promise(resolve=>{
            const check=()=>globalThis.ready instanceof Promise?resolve(true):setTimeout(check,0);check();})`});
        assert.equal(initialized.result.value,true);
        let retries=0;
        const observation=retry
            ? observeStartup(call,params,deadline,{isOpen:()=>socket.readyState===WebSocket.OPEN,onRetry:()=>retries++})
            : call('Runtime.evaluate',params);
        // Attach failure handling now. A second read-only command is a barrier:
        // the first evaluation has captured the old document's pending Promise.
        const checked=retry?observation.then(result=>assert.equal(result.result.value,'ready'))
            :assert.rejects(observation,error=>error.method==='Runtime.evaluate'&&error.code===-32000
                &&['Inspected target navigated or closed','Execution context was destroyed.'].includes(error.protocolMessage));
        await call('Runtime.evaluate',{expression:'1',returnByValue:true});
        await call('Runtime.evaluate',{expression:`const next=new URL('index.html',location.href);
            next.search=location.search;next.hash=location.hash;setTimeout(()=>location.replace(next.href),0);`});
        await checked;
        assert.equal(retries,Number(retry));
        const committed=await waitForStartupCommit(navigations,url,deadline);
        assert.equal(committed.url,base+'/index.html?test='+Number(retry));
        assert.equal(committed.urlFragment,'#keep');
    }
    console.log('Real Chrome: original observation fails; startup helper recovers once; canonical commit preserves query/fragment.');
}finally{
    socket?.close();chrome.kill('SIGKILL');await shutdown;
    await new Promise(resolve=>server.close(resolve));
    await rm(directory,{recursive:true,force:true,maxRetries:5,retryDelay:100});
}
