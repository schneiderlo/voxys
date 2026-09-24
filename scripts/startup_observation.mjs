// Only read-only startup observations may be repeated after a document change.
// Never use this retry policy for input, screenshots, or post-startup commands.
const sleepDefault=ms=>new Promise(resolve=>setTimeout(resolve,ms));

export function cdpProtocolError(method,error){
    const result=new Error(`${method}: ${JSON.stringify(error)}`);
    result.method=method;
    result.code=error.code;
    result.protocolMessage=error.message;
    return result;
}

function remaining(deadline,now,isOpen,phase){
    if(!isOpen())throw new Error(`Chrome connection is closed during ${phase}`);
    const milliseconds=deadline-now();
    if(milliseconds<=0)throw new Error(`${phase} deadline expired`);
    return milliseconds;
}

export async function waitForStartupCommit(navigations,requestedUrl,deadline,
    {now=Date.now,sleep=sleepDefault,isOpen=()=>true}={}){
    // The real directory entry redirects in its first script. Keep exercising
    // that route, but observe the application only after its final commit.
    const expected=new URL(requestedUrl);
    if(expected.pathname.endsWith('/'))expected.pathname+='index.html';
    expected.hash='';
    while(true){
        const milliseconds=remaining(deadline,now,isOpen,'Startup navigation');
        // CDP separates urlFragment from Frame.url. Fragment-only changes do
        // not replace the document, so they are not part of commit identity.
        const committed=navigations.find(frame=>{
            const actual=new URL(frame.url);actual.hash='';return actual.href===expected.href;
        });
        if(committed)return committed;
        await sleep(Math.min(25,milliseconds));
    }
}

export async function observeStartup(call,params,deadline,
    {now=Date.now,sleep=sleepDefault,isOpen=()=>true,onRetry=()=>{}}={}){
    while(true){
        const milliseconds=remaining(deadline,now,isOpen,'Startup observation');
        try{return await call('Runtime.evaluate',params,milliseconds);}
        catch(error){
            const navigationError=error.method==='Runtime.evaluate'&&error.code===-32000
                &&['Inspected target navigated or closed','Execution context was destroyed.'].includes(error.protocolMessage);
            if(!navigationError)throw error;
            const left=remaining(deadline,now,isOpen,'Startup observation');
            onRetry(error);
            await sleep(Math.min(25,left));
        }
    }
}
