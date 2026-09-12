// Explicit, bounded visual review of the real canvas. No game state setters.
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';

export async function beginCoveVisualCapture(call,directory){
    await mkdir(directory,{recursive:false});
    const evaluate=async expression=>{
        const result=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        assert(!result.exceptionDetails,JSON.stringify(result.exceptionDetails));return result.result.value;
    };
    const read=()=>evaluate(`(()=>{const panel=document.getElementById('salvage-preview'),r=panel.getBoundingClientRect();
        return {state:JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json())),
        width:document.querySelector('canvas').width,height:document.querySelector('canvas').height,
        hud:{x:r.x,y:r.y,width:r.width,height:r.height,bottom:r.bottom,
            scrollHeight:panel.scrollHeight,clientHeight:panel.clientHeight,
            moreControlsOpen:document.getElementById('salvage-more-controls').open}};})()`);
    // Readiness can precede the freshly spawned boat settling onto the water.
    // Require actual completed simulation, not an arbitrary render-frame count.
    let before=await read();const deadline=Date.now()+30000;
    while(BigInt(before.state.boat.physicsTicks.completed)-BigInt(before.state.boat.physicsTicks.base)<180n){
        assert(Date.now()<deadline,'Boat did not complete its first three simulation seconds');
        await new Promise(resolve=>setTimeout(resolve,100));before=await read();
    }
    assert(before.state.ready&&!before.state.failed&&before.state.characterCamera.valid);
    await writeFile(`${directory}/preflight.json`,JSON.stringify(before,null,2)+'\n');
    assert(before.hud.x>before.width*.6&&!before.hud.moreControlsOpen,'Compact controls must start at the right');
    assert(before.hud.height<=390&&before.hud.scrollHeight<=before.hud.clientHeight,
        'Every primary control must fit the compact arrival panel without scrolling');
    const png=await call('Page.captureScreenshot',{format:'png'});
    await writeFile(`${directory}/browser-start.png`,Buffer.from(png.data,'base64'));
    await evaluate(`(()=>{
        const type=['video/webm;codecs=vp9','video/webm;codecs=vp8'].find(t=>MediaRecorder.isTypeSupported(t));
        if(!type)throw Error('No WebM recorder available');
        const canvas=document.querySelector('canvas'),stream=canvas.captureStream(30),chunks=[];
        let recorder,timer,bytes=0,failed;
        const cleanup=()=>{clearTimeout(timer);stream.getTracks().forEach(track=>track.stop());};
        try{recorder=new MediaRecorder(stream,{mimeType:type,videoBitsPerSecond:6000000});}
        catch(error){cleanup();throw error;}
        globalThis.voxyVisualReview=new Promise((resolve,reject)=>{
            recorder.ondataavailable=e=>{
                if(!e.data.size||failed)return;
                bytes+=e.data.size;
                if(bytes>20000000){failed=Error('Review movie outside budget');cleanup();if(recorder.state!=='inactive')recorder.stop();return;}
                chunks.push(e.data);
            };
            recorder.onerror=event=>{failed=Error(event.error?.message||'Capture failed');cleanup();reject(failed);};
            recorder.onstop=async()=>{
                cleanup();
                try {
                    if(failed)throw failed;
                    const blob=new Blob(chunks,{type}),buffer=new Uint8Array(await blob.arrayBuffer());
                    if(buffer.length===0||buffer.length>20000000)throw Error('Review movie outside budget');
                    let binary='';for(let i=0;i<buffer.length;i+=16384)binary+=String.fromCharCode(...buffer.subarray(i,i+16384));
                    resolve({type,bytes:buffer.length,data:btoa(binary)});
                } catch(error){reject(error);}
            };
        });
        // Retain a rejection handler until the controls journey finalizes it.
        globalThis.voxyVisualReview.catch(()=>{});
        try{recorder.start(250);timer=setTimeout(()=>{if(recorder.state!=='inactive')recorder.stop();},12000);}
        catch(error){cleanup();throw error;}return true;
    })()`);
    return async()=>{
        const movie=await evaluate('globalThis.voxyVisualReview');
        await writeFile(`${directory}/playable-motion.webm`,Buffer.from(movie.data,'base64'));
        const report={kind:'One real browser startup and 12-second canvas recording during actual controls',
            seconds:12,targetFps:30,bytes:movie.bytes,mimeType:movie.type,before,
            limitation:'Recording is visual evidence, not a performance benchmark; native/browser HUDs differ.'};
        await writeFile(`${directory}/capture.json`,JSON.stringify(report,null,2)+'\n');
        await evaluate('delete globalThis.voxyVisualReview');return report;
    };
}
