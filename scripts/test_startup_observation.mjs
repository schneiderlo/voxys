import assert from 'node:assert/strict';
import {test} from 'node:test';
import {cdpProtocolError,waitForStartupCommit,observeStartup} from './startup_observation.mjs';

function clock(){
    let time=0;
    return {now:()=>time,sleep:async ms=>{time+=ms;}};
}
const navigationError=message=>cdpProtocolError('Runtime.evaluate',{code:-32000,message});

test('CDP failures retain method, code and protocol message',()=>{
    const error=cdpProtocolError('Page.bringToFront',{code:-32000,message:'Inspected target navigated or closed'});
    assert.equal(error.method,'Page.bringToFront');
    assert.equal(error.code,-32000);
    assert.equal(error.protocolMessage,'Inspected target navigated or closed');
    assert.match(String(error),/Page.bringToFront.*-32000/);
});

test('directory entry waits for canonical CDP document URL, retaining query and ignoring fragment',async()=>{
    const timing=clock(),navigations=[{url:'http://localhost/?experience=build#keep'}];
    const final={url:'http://localhost/index.html?experience=build',urlFragment:'#keep'};
    let waits=0;
    const committed=await waitForStartupCommit(navigations,navigations[0].url,100,{
        ...timing,sleep:async ms=>{await timing.sleep(ms);waits++;navigations.push(final);},
    });
    assert.equal(committed,final);
    assert.equal(waits,1);
});

test('missing or wrong final commit reaches the original deadline',async()=>{
    const timing=clock();
    await assert.rejects(waitForStartupCommit([{url:'http://localhost/other.html'}],
        'http://localhost/',60,timing),/Startup navigation deadline expired/);
    assert.equal(timing.now(),60);
});

for(const message of ['Inspected target navigated or closed','Execution context was destroyed.']){
    test(`startup observation retries known document loss: ${message}`,async()=>{
        const timing=clock(),calls=[],retries=[];
        const params={expression:'globalThis.ready',returnByValue:true},expected={result:{value:true}};
        const result=await observeStartup(async(method,input,timeout)=>{
            calls.push({method,input,timeout});
            if(calls.length===1)throw navigationError(message);
            return expected;
        },params,100,{...timing,onRetry:error=>retries.push(error)});
        assert.equal(result,expected);
        assert.equal(retries.length,1);
        assert.deepEqual(calls,[{method:'Runtime.evaluate',input:params,timeout:100},
            {method:'Runtime.evaluate',input:params,timeout:75}]);
    });
}

test('repeated navigation cannot reset or extend the startup deadline',async()=>{
    const timing=clock(),timeouts=[];
    await assert.rejects(observeStartup(async(method,params,timeout)=>{
        timeouts.push(timeout);throw navigationError('Inspected target navigated or closed');
    },{},60,timing),/Startup observation deadline expired/);
    assert.deepEqual(timeouts,[60,35,10]);
    assert.equal(timing.now(),60);
});

test('closed connection is fatal even with the navigation-or-closed message',async()=>{
    let open=true,calls=0;
    await assert.rejects(observeStartup(async()=>{
        calls++;open=false;throw navigationError('Inspected target navigated or closed');
    },{},100,{...clock(),isOpen:()=>open}),/Chrome connection is closed/);
    assert.equal(calls,1);
});

for(const error of [
    cdpProtocolError('Runtime.evaluate',{code:-32000,message:'Target crashed'}),
    cdpProtocolError('Runtime.evaluate',{code:-32602,message:'Inspected target navigated or closed'}),
    cdpProtocolError('Page.navigate',{code:-32000,message:'Inspected target navigated or closed'}),
    new Error('Chrome request timed out: Runtime.evaluate'),
]){
    test(`non-navigation failure is not retried: ${error}`,async()=>{
        let calls=0;
        await assert.rejects(observeStartup(async()=>{calls++;throw error;},{},100,clock()),actual=>actual===error);
        assert.equal(calls,1);
    });
}

test('JavaScript exception details remain visible to the smoke failure gates',async()=>{
    const failure={exceptionDetails:{text:'Uncaught',exception:{description:'Error: GPU device lost'}}};
    let calls=0;
    assert.equal(await observeStartup(async()=>{calls++;return failure;},{},100,clock()),failure);
    assert.equal(calls,1);
});
