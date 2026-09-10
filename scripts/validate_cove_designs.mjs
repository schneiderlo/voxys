// Exercise shipped input and UI. No screenshots, camera setters or state injection.
import assert from 'node:assert/strict';
import {mkdir, writeFile, readFile, readdir} from 'node:fs/promises';

export async function validateCoveDesigns(call, directory) {
    await mkdir(directory, {recursive:true});
    const expression='JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json()))';
    const evaluate=async expression=>{
        const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));
        return r.result.value;
    };
    const read=()=>evaluate(expression);
    const delay=ms=>new Promise(r=>setTimeout(r,ms));
    const wait=async(predicate,label)=>{
        const until=Date.now()+20000;
        let state;
        do{state=await read(); if(predicate(state))return state; await delay(30);}while(Date.now()<until);
        throw Error(`${label}: ${JSON.stringify(state)}`);
    };
    const key=(letter,down)=>call('Input.dispatchKeyEvent',{type:down?'keyDown':'keyUp',
        key:letter==='Space'?' ':letter.toLowerCase(),code:letter==='Space'?'Space':`Key${letter}`,
        windowsVirtualKeyCode:letter==='Space'?32:letter.charCodeAt(0),nativeVirtualKeyCode:letter==='Space'?32:letter.charCodeAt(0)});
    const hold=async(keys,ticks)=>{
        const first=BigInt((await read()).player.tick);
        try{
            for(const k of keys)await key(k,true);
            await wait(s=>BigInt(s.player.tick)>=first+BigInt(ticks),'movement ticks');
        }finally{for(const k of keys)await key(k,false);}
    };
    const mouse=async p=>{
        await call('Input.dispatchMouseEvent',{type:'mouseMoved',...p});
        await call('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',clickCount:1,...p});
        await call('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',clickCount:1,...p});
    };
    const click=async id=>{
        // Let the 100 ms HUD refresh finish before locating its next control.
        await delay(160);
        const p=await evaluate(`(()=>{const e=document.getElementById(${JSON.stringify(id)});
            if(!e||e.hidden||e.disabled||!e.getClientRects().length)return null;
            e.scrollIntoView({block:'nearest'});const r=e.getBoundingClientRect();
            const x=r.x+r.width/2,y=r.y+r.height/2;
            if(!e.contains(document.elementFromPoint(x,y)))return null;return {x,y};})()`);
        if(!p)throw Error(`${id} unavailable: ${JSON.stringify(await evaluate(`(()=>{const e=document.getElementById(${JSON.stringify(id)});if(!e)return {missing:true};const r=e.getBoundingClientRect();return {hidden:e.hidden,disabled:e.disabled,rect:{x:r.x,y:r.y,width:r.width,height:r.height},hit:document.elementFromPoint(r.x+r.width/2,r.y+r.height/2)?.outerHTML?.slice(0,500),panel:document.getElementById('salvage-preview').outerHTML.slice(0,450)};})()`))}`);
        await mouse(p);
    };
    const walkTo=async(x,z)=>{
        for(let n=0;n<100;++n){
            const s=await read(), p=s.player.feet, target=typeof x==='function'?x(s):[x,z];
            const dx=target[0]-p[0],dz=target[1]-p[2],length=Math.hypot(dx,dz);
            if(length<.14){await hold([],20);return;}
            const yaw=s.camera.yaw;
            const forward=(dx*Math.sin(yaw)+dz*Math.cos(yaw))/length;
            const right=(dx*Math.cos(yaw)-dz*Math.sin(yaw))/length;
            const keys=[];
            if(Math.abs(forward)>.4)keys.push(forward>0?'W':'S');
            if(Math.abs(right)>.4)keys.push(right>0?'D':'A');
            await hold(keys,Math.max(1,Math.min(5,Math.floor(length/.06)-1)));
        }
        throw Error(`Walk failed to reach ${x},${z}: ${JSON.stringify((await read()).player)}`);
    };
    const report={status:'running',kind:'Real named designs, persistence, backup, export/import and paid launch; no images',stages:[]};
    const ui=()=>evaluate(`({message:document.getElementById('design-status').textContent,busy:document.getElementById('workshop-designs').dataset.busy,
        names:[...document.getElementById('design-list').options].map(o=>o.textContent),name:document.getElementById('design-name').value})`);
    const waitUi=async(predicate,label)=>{
        const until=Date.now()+15000;let value;do{value=await ui();if(value.busy==='false'&&predicate(value))return value;await delay(40);}while(Date.now()<until);
        throw Error(label+': '+JSON.stringify(value));
    };
    const record=async name=>{
        const s=await read();assert.equal(s.failed,false);assert.equal(s.terrainSurface,'lego');
        assert.deepEqual(await evaluate('globalThis.voxyUncapturedGpuErrors||[]'),[]);
        const controls=await ui();report.stages.push({name,state:s,library:controls});await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));return s;
    };
    const open=async()=>{await click('salvage-workshop-toggle');await wait(s=>s.workshop.open,'workshop');};
    const expand=async()=>{
        const p=await evaluate(`(()=>{const e=document.querySelector('#workshop-designs summary');e.scrollIntoView({block:'center'});const r=e.getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2};})()`);
        await mouse(p);await waitUi(()=>true,'design library ready');
    };
    const setName=async text=>{
        await click('design-name');await call('Input.dispatchKeyEvent',{type:'keyDown',key:'a',code:'KeyA',windowsVirtualKeyCode:65,modifiers:2});
        await call('Input.dispatchKeyEvent',{type:'keyUp',key:'a',code:'KeyA',windowsVirtualKeyCode:65,modifiers:2});
        await call('Input.insertText',{text});assert.equal((await ui()).name,text);
    };
    const selectPart=async name=>{for(let i=0;i<13&&(await read()).workshop.name!==name;++i)await click('workshop-next');assert.equal((await read()).workshop.name,name);};
    const keep=async()=>{await click('workshop-keep');await wait(s=>!s.workshop.changed,'keep draft');};
    const importFile=async path=>{
        await call('Page.setInterceptFileChooserDialog',{enabled:true});await click('design-import');
        const doc=await call('DOM.getDocument',{});const input=await call('DOM.querySelector',{nodeId:doc.root.nodeId,selector:'#design-file'});
        await call('DOM.setFileInputFiles',{nodeId:input.nodeId,files:[path]});
    };
    try {
        await wait(s=>s.ready&&s.workshop?.canOpen&&s.boat.observedTick>0,'cove ready');await open();await expand();
        assert.deepEqual((await ui()).names,[]);
        await click('workshop-setting-enabled');await keep(); // Winch disabled in saved design.
        await selectPart('Propeller');await click('workshop-setting-limit');await keep();
        await click('workshop-add');await wait(s=>s.workshop.valid&&s.workshop.parts===12,'expanded draft');await keep();
        await setName('Harbor tug');const beforeName=(await read()).workshop;
        // Real key events inside the name field must not operate the workshop.
        for(const letter of ['W','E','R','V','X']){await key(letter,true);await key(letter,false);}
        const afterName=(await read()).workshop;assert.equal(afterName.revision,beforeName.revision);assert.equal(afterName.parts,beforeName.parts);assert.equal(afterName.open,true);
        await setName('Harbor tug');await click('design-save-new');await waitUi(u=>u.names.includes('Harbor tug')&&u.message.startsWith('Saved'),'save named design');
        const saved=await record('saved-unlaunched-design');assert.equal(saved.boat.parts,11);assert.equal(saved.session.inventory.salvageMaterial,'48');
        await setName('Harbor tug copy');await click('design-duplicate');await waitUi(u=>u.names.length===2&&u.names.includes('Harbor tug copy'),'duplicate');
        await setName('Harbor tug spare');await click('design-rename');await waitUi(u=>u.names.includes('Harbor tug spare')&&!u.names.includes('Harbor tug copy'),'rename');
        await selectPart('Cargo cradle');await click('workshop-remove');await keep();
        await click('design-update');await waitUi(u=>u.message.startsWith('Saved'),'update selected');await record('updated-design-with-backup');
        await click('design-restore');await waitUi(u=>u.message==='Previous saved version restored.','restore saved version');
        await record('backup-restored');
        // Reload the actual page within the same browser profile/origin. The
        // design library survives; this is explicitly not world-state recovery.
        await call('Page.reload',{});await delay(1000);
        await wait(s=>s.ready&&s.workshop?.canOpen&&s.boat.observedTick>0,'fresh page ready');
        const hudUntil=Date.now()+10000;
        while(Date.now()<hudUntil&&!await evaluate(`(()=>{const b=document.getElementById('salvage-workshop-toggle'),p=document.getElementById('salvage-preview');if(!b||b.disabled||b.hidden||p.hidden)return false;b.scrollIntoView({block:'nearest'});const r=b.getBoundingClientRect();return b.contains(document.elementFromPoint(r.x+r.width/2,r.y+r.height/2));})()`))await delay(50);
        await open();await expand();
        await waitUi(u=>u.names.length===2,'saved records after reload');
        const reloaded=await record('reload-keeps-library');assert.equal(reloaded.boat.parts,11);assert.equal(reloaded.boat.thrustLimitNewtons,2500);
        // Native select control: End selects the alphabetically last spare.
        await click('design-list');await call('Input.dispatchKeyEvent',{type:'keyDown',key:'End',code:'End',windowsVirtualKeyCode:35});
        await call('Input.dispatchKeyEvent',{type:'keyUp',key:'End',code:'End',windowsVirtualKeyCode:35});
        await waitUi(u=>u.name==='Harbor tug spare','select persisted spare');
        await click('design-load');await waitUi(u=>u.message.startsWith('Loaded'),'load persisted boat');
        const loaded=await record('loaded-design-requires-purchase');assert.equal(loaded.workshop.parts,12);assert.equal(loaded.workshop.charge,'24');assert.equal(loaded.boat.parts,11);
        await selectPart('Winch');assert.equal((await read()).workshop.settings.enabled,false);
        await selectPart('Propeller');assert.equal((await read()).workshop.settings.limitPercent,75);
        await mkdir(`${directory}/downloads`,{recursive:true});await call('Browser.setDownloadBehavior',{behavior:'allow',downloadPath:`${directory}/downloads`});
        await setName('Exported tug');await click('design-export');await waitUi(u=>u.message.startsWith('Design export started'),'export');
        let filename;const until=Date.now()+10000;do{filename=(await readdir(`${directory}/downloads`)).find(n=>n.endsWith('.voxy-design.json'));if(filename)break;await delay(50);}while(Date.now()<until);
        assert(filename,'actual design download must finish');const exportedPath=`${directory}/downloads/${filename}`;
        const exported=JSON.parse(await readFile(exportedPath,'utf8'));assert.equal(exported.name,'Exported tug');assert.equal(exported.version,1);assert.match(exported.blueprint,/^[0-9a-f]+$/);
        await importFile(exportedPath);await waitUi(u=>u.names.includes('Exported tug')&&u.names.length===3,'import saved file');await record('export-import');
        const corrupt={...exported,name:'Corrupt tug',blueprint:exported.blueprint.slice(0,40)+(exported.blueprint[40]==='0'?'1':'0')+exported.blueprint.slice(41)};
        const corruptPath=`${directory}/corrupt.voxy-design.json`;await writeFile(corruptPath,JSON.stringify(corrupt));
        await importFile(corruptPath);await waitUi(u=>u.message.includes('damaged'),'reject damaged import');assert.equal((await ui()).names.length,3);await record('corruption-keeps-library');
        await click('workshop-launch');await wait(s=>!s.workshop.open&&!s.workshop.pending&&s.workshop.launches===1,'paid saved-design launch');
        const launched=await record('saved-design-paid-launch');assert.equal(launched.boat.parts,12);assert.equal(launched.session.inventory.salvageMaterial,'24');assert.equal(launched.boat.thrustLimitNewtons,1875);assert.equal(launched.tow.hasWinch,false);
        await walkTo(4.5,-49);await walkTo(4.5,-53);await hold(['E'],2);await wait(s=>s.player.onBoat,'board loaded craft');
        await walkTo(s=>[s.boat.helmPosition[0],s.boat.helmPosition[2]]);await wait(s=>s.player.interaction==='helm','loaded helm');
        await click('salvage-interact');await wait(s=>s.player.mode==='helm','use helm');const start=await read();
        await hold(['W'],150);const sailed=await record('saved-design-sails');assert(Math.hypot(sailed.boat.position[0]-start.boat.position[0],sailed.boat.position[2]-start.boat.position[2])>1);
        await click('salvage-leave');const deadline=Date.now()+20000;
        while(Date.now()<deadline&&!await evaluate('location.search.includes("experience=lego-world")'))await delay(50);
        assert(await evaluate('location.search.includes("experience=lego-world")'),'saved craft drains on Leave');report.status='passed';
    }catch(error){report.status='failed';report.error=String(error);throw error;}
    finally{await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));}
    return report;
}
