// Real free-flight crossing of an inspection camera sector; no synthetic GPU state.
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';

export async function validateSalvageAssetSectors(call,directory) {
    await mkdir(directory,{recursive:false});
    const expression='JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json()))';
    const evaluate=async expression=>{
        const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));
        return r.result?.value;
    };
    const read=()=>evaluate(`({...${expression},lost:globalThis.voxyDeviceLost||null,errors:globalThis.voxyUncapturedGpuErrors||[]})`);
    const wait=async predicate=>{
        const end=Date.now()+30000;
        while(Date.now()<end){const s=await read();if(predicate(s))return s;await new Promise(r=>setTimeout(r,50));}
        throw Error('Sector crossing or GPU completion timed out');
    };
    const key=(letter,down)=>call('Input.dispatchKeyEvent',{type:down?'keyDown':'keyUp',key:letter.toLowerCase(),code:`Key${letter}`,windowsVirtualKeyCode:letter.charCodeAt(0),nativeVirtualKeyCode:letter.charCodeAt(0)});
    const report={status:'running',scope:'Same live assembly across a sector edge using real W/S input, clean and socket views; distant views do not replace close socket inspection',stages:[]};
    try {
        const initial=await wait(s=>s.ready&&BigInt(s.assetFixture.completedSerial)>0n);
        report.initial=initial;
        assert.deepEqual(initial.assetFixture.assembly,{parts:6,connections:5});
        const absolute=c=>c.local.map((v,i)=>v+256*c.sector[i]);
        const origin=initial.origin.map((v,i)=>v+[-7,3,-5][i]);
        const eye=[128.25,origin[1]+20,origin[2]-10];
        const target=origin.map((v,i)=>v+[1.25,.25,0][i]);
        const dir=target.map((v,i)=>v-eye[i]),yaw=Math.atan2(dir[0],dir[2]),pitch=Math.asin(dir[1]/Math.hypot(...dir));
        const record=async (name,before)=>{
            const s=await wait(s=>s.ready&&BigInt(s.assetFixture.completedSerial)>BigInt(before.assetFixture.submittedSerial)+2n);
            assert.equal(s.failed,false);assert.equal(s.lost,null);assert.deepEqual(s.errors,[]);
            assert(s.camera.local.every(v=>v>=-128&&v<128));
            for(const field of ['generation','uploads','prototypeUploads','gpuReservationBytes'])assert.equal(s.assetFixture[field],initial.assetFixture[field]);
            assert.deepEqual(s.assetFixture.assembly,initial.assetFixture.assembly);assert.equal(s.bodies,0);
            assert(s.assetFixture.draws>=6);assert.deepEqual(s.session,initial.session);
            const bytes=Buffer.from((await call('Page.captureScreenshot',{format:'png'})).data,'base64');
            await writeFile(`${directory}/${name}.png`,bytes);
            report.stages.push({name,state:s,absolute_eye:absolute(s.camera),png_sha256:createHash('sha256').update(bytes).digest('hex')});
            return s;
        };
        for(const guides of [0,2]) {
            assert.equal(await evaluate(`voxyModule._voxy_salvage_preview_action(${20+guides})`),1);
            assert.equal(await evaluate(`voxyModule._voxy_set_camera_pose(${[...eye,yaw,pitch].join(',')})`),1);
            const start=await record(`guides-${guides}-start`,await read());
            assert.equal(start.camera.sector[0],1);assert.equal(start.assetFixture.guides,guides);
            await key('W',true);
            try{await wait(s=>absolute(s.camera)[0]<127.75);}finally{await key('W',false);}
            const crossed=await record(`guides-${guides}-crossed`,await read());
            assert.equal(crossed.camera.sector[0],0);assert(absolute(crossed.camera)[0]>126,'Unexpected flight jump');
            await key('S',true);
            try{await wait(s=>absolute(s.camera)[0]>128.25);}finally{await key('S',false);}
            const returned=await record(`guides-${guides}-returned`,await read());
            assert.equal(returned.camera.sector[0],1);assert(absolute(returned.camera)[0]<130,'Unexpected return jump');
        }
        const before=await read();
        assert.equal(await evaluate('voxyModule._voxy_set_camera_pose(1e30,0,0,0,0)'),0);
        assert.deepEqual((await read()).camera,before.camera,'Unrepresentable pose changed camera');
        assert.equal(await evaluate('voxyModule._voxy_salvage_preview_action(1)'),1);
        const reset=await wait(s=>s.ready&&s.resets===initial.resets+1);
        assert(Math.hypot(...absolute(reset.camera).map((v,i)=>v-absolute(initial.camera)[i]))<.01);
        report.reset=reset;report.status='passed';
    } catch(error) { report.status='failed';report.error=String(error);throw error; }
    finally {
        await key('W',false).catch(()=>{});await key('S',false).catch(()=>{});
        await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2)+'\n');
    }
    return report;
}
