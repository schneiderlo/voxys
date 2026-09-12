// One cohesive OS-controller journey. No navigator override, game setters,
// screenshots or repeated sailing. Blueprint export and DOM inspection are read-only.
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {fileURLToPath} from 'node:url';

export async function validateCoveBuilderTools(call,directory,{expectedPresentationParts=9}={}){
    assert(Number.isInteger(expectedPresentationParts)&&expectedPresentationParts>=1&&expectedPresentationParts<=16);
    await mkdir(directory,{recursive:true});
    const started=Date.now(),deadline=started+300000;
    const report={status:'running',maximumSeconds:300,expectedPresentationParts,
        kind:'OS controller build/group/name/library/Launch; one durable boat+library reload; physical mouse save cross-input; no images',
        stages:[],controls:[],navigation:[]};
    const evaluate=async expression=>{
        const result=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
        if(result.exceptionDetails)throw Error(JSON.stringify(result.exceptionDetails));return result.result.value;
    };
    const read=()=>evaluate('JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json()))');
    const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
    const wait=async(predicate,label,{reload=false,seconds=20}={})=>{
        const end=Math.min(deadline,Date.now()+seconds*1000);let state,lastError;
        while(Date.now()<end){
            try{state=await read();}catch(error){if(!reload)throw error;lastError=String(error);await delay(40);continue;}
            if(await predicate(state))return state;await delay(25);
        }
        throw Error(`${label}: ${JSON.stringify({state,lastError})}`);
    };
    const owner=s=>({world:s.world,build:s.boat.buildId,topology:s.boat.topologyRevision,
        incarnation:s.boat.physicsTicks.incarnation,roots:s.boat.roots.map(root=>root.key),
        bodyIndex:s.boat.mechanisms.bodyIndex,bodyGeneration:s.boat.mechanisms.bodyGeneration});
    const stock=s=>({parts:s.boat.parts,massKg:s.boat.massKg,paidPartIds:s.boat.paidPartIds,
        inventory:s.session.inventory,storedPartIds:s.workshop.storedPartIds});
    const invariant=s=>{
        assert(s.active&&s.ready&&!s.failed&&s.boat.active&&s.session.admissionOpen,'active admitted Cove');
        assert.equal(s.terrainSurface,'lego');assert.equal(s.boat.physicsTicks.failed,false);
        assert.equal(s.assetFixture.presentationParts,expectedPresentationParts);
        assert(s.boat.mechanisms.bodyIndex>0&&s.boat.mechanisms.bodyGeneration>0);
        assert.equal(s.boat.mechanisms.incarnation,s.boat.physicsTicks.incarnation);
        const f=s.assetFixture;
        assert(BigInt(f.gpuReservationBytes)>0n&&BigInt(f.gpuReservationBytes)<=16n*1024n*1024n);
        assert(f.environmentReady&&BigInt(f.environmentGpuBytes)>0n&&f.draws>0,'lit owned fixture');
    };
    const dom=()=>evaluate(`(()=>{
        const active=document.activeElement,panel=document.getElementById('workshop-designs');
        const input=document.getElementById('design-name'),list=document.getElementById('design-list');
        return {active:{id:active?.id,tag:active?.tagName,text:active?.textContent?.trim().slice(0,120),
            disabled:Boolean(active?.disabled),key:active?.dataset?.controllerKey},
            modal:Boolean(document.querySelector('.cove-controller-dialog[open]')),
            library:{busy:panel?.dataset.busy,name:input?.value,selected:list?.value,
                choices:[...(list?.options||[])].map(o=>({id:o.value,name:o.textContent})),
                message:document.getElementById('design-status')?.textContent},
            problem:document.getElementById('salvage-workshop-status')?.textContent,
            controllerActive:Boolean(window.voxyControllerMenuActive?.()),
            errors:globalThis.voxyUncapturedGpuErrors||[]};})()`);
    const capture=async(name,extra={})=>{
        const state=await read();invariant(state);const identity=owner(state),ownership=stock(state);
        const submission=await wait(s=>{
            invariant(s);assert.deepEqual(owner(s),identity);assert.deepEqual(stock(s),ownership);
            return BigInt(s.assetFixture.submittedSerial)>BigInt(state.assetFixture.submittedSerial);
        },'fresh same-owner submission');
        const completed=await wait(s=>{
            invariant(s);assert.deepEqual(owner(s),identity);assert.deepEqual(stock(s),ownership);
            return BigInt(s.assetFixture.completedSerial)>=BigInt(submission.assetFixture.submittedSerial)
                &&BigInt(s.boat.physicsTicks.completed)>=BigInt(submission.boat.mechanisms.tick);
        },'actual fixture and physics completion');
        const ui=await dom();assert.deepEqual(ui.errors,[]);
        report.stages.push({name,state,ui,...extra,submission:{owner:identity,fixture:submission.assetFixture.submittedSerial,
            physics:submission.boat.mechanisms.tick},completion:{fixture:completed.assetFixture.completedSerial,physics:completed.boat.physicsTicks.completed}});
        await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));return state;
    };
    const blueprint=async()=>{
        const bytes=await evaluate("voxyModule.ccall('voxy_salvage_blueprint_action','string',['number','string'],[1,''])");
        assert.match(bytes,/^53564250[0-9a-f]+$/);return bytes;
    };
    const python=process.env.VOXY_TEST_PYTHON||'python3';
    const child=spawn(python,[fileURLToPath(new URL('./cove_virtual_gamepad.py',import.meta.url))],{stdio:['pipe','pipe','pipe']});
    const lines=createInterface({input:child.stdout}),replies=[],waiters=[];let stderr='',exitInfo=null;
    child.stderr.on('data',data=>{stderr+=data.toString();});
    lines.on('line',line=>{
        let reply;try{reply=JSON.parse(line);}catch{reply={ok:false,error:'Non-JSON controller output: '+line};}
        if(waiters.length)waiters.shift().resolve(reply);else replies.push(reply);
    });
    child.on('error',error=>{exitInfo={error:String(error)};while(waiters.length)waiters.shift().reject(error);});
    child.on('close',(code,signal)=>{exitInfo={code,signal};while(waiters.length)waiters.shift().reject(Error('Controller helper exited: '+JSON.stringify(exitInfo)));});
    const reply=()=>{
        if(replies.length)return Promise.resolve(replies.shift());
        if(exitInfo)return Promise.reject(Error(JSON.stringify(exitInfo)));
        return new Promise((resolve,reject)=>{
            const pending={resolve:value=>{clearTimeout(timer);resolve(value);},reject:error=>{clearTimeout(timer);reject(error);}};
            const timer=setTimeout(()=>{const index=waiters.indexOf(pending);if(index>=0)waiters.splice(index,1);reject(Error('Controller helper acknowledgment timed out'));},5000);
            waiters.push(pending);
        });
    };
    const send=async command=>{child.stdin.write(JSON.stringify(command)+'\n');const value=await reply();assert.equal(value.ok,true,JSON.stringify(value));};
    const fresh=before=>wait(s=>BigInt(s.assetFixture.submittedSerial)>BigInt(before),'controller edge reached a rendered frame');
    const observedPad=()=>evaluate(`(()=>{const p=[...navigator.getGamepads()].find(p=>p?.connected&&p.mapping==='standard');
        return p?{id:p.id,index:p.index,pressed:p.buttons.map((b,i)=>b.pressed?i:-1).filter(i=>i>=0),axes:[...p.axes]}:null;})()`);
    const event=async(down,up,label)=>{
        assert(Date.now()<deadline,'journey time bound');
        await wait(s=>s.gamepad.connected&&s.gamepad.armed,'controller neutral and armed');
        const before=(await read()).assetFixture.submittedSerial;
        let actualPad;
        try{
            await send(down);await delay(65);await fresh(before);actualPad=await observedPad();
            if(down.button){
                const expected={a:0,b:1,x:2,y:3,lb:4,rb:5,view:8,menu:9,ls:10,rs:11}[down.button];
                assert(actualPad?.pressed.includes(expected),`OS ${down.button} must reach standard browser button ${expected}: ${JSON.stringify(actualPad)}`);
            }
        }finally{await send(up);}
        const released=(await read()).assetFixture.submittedSerial;await fresh(released);
        await wait(s=>s.gamepad.connected&&s.gamepad.armed,'controller released and rearmed');
        report.controls.push({input:label,frameBefore:before,frameAfter:(await read()).assetFixture.submittedSerial,actualPad});
    };
    const button=name=>event({button:name,down:true},{button:name,down:false},name);
    const direction=(axis,value)=>event({axis,value},{axis,value:0},`${axis}:${value}`);
    const cameraMove=async(modifier,axis,value)=>{
        await wait(s=>s.gamepad.connected&&s.gamepad.armed,'camera controller neutral');
        const before=(await read()).assetFixture.submittedSerial;
        try{
            await send({button:modifier,down:true});await send({axis,value});
            await delay(100);await fresh(before);
        }finally{
            await send({axis,value:0});await send({button:modifier,down:false});
        }
        await fresh((await read()).assetFixture.submittedSerial);
        await wait(s=>s.gamepad.connected&&s.gamepad.armed,'camera controller released');
        report.controls.push({input:`hold ${modifier} + ${axis}:${value}`,frameBefore:before,
            frameAfter:(await read()).assetFixture.submittedSerial});
    };
    const key=async(key,code,keyCode)=>{
        try{await call('Input.dispatchKeyEvent',{type:'keyDown',key,code,windowsVirtualKeyCode:keyCode,nativeVirtualKeyCode:keyCode});}
        finally{await call('Input.dispatchKeyEvent',{type:'keyUp',key,code,windowsVirtualKeyCode:keyCode,nativeVirtualKeyCode:keyCode});}
    };
    const uncap=async()=>{
        if(!await evaluate('voxyModule._voxy_get_uncapped_fps()'))await key('F9','F9',120);
        await wait(()=>evaluate('Boolean(voxyModule._voxy_get_uncapped_fps())'),'physical F9 presentation control');
    };
    const handshake=async()=>{
        // Right-stick click has no dock/paused gameplay action. The real OS
        // button exposes the pad to Chrome, then neutral releases arm it.
        await send({button:'rs',down:true});await delay(180);await send({button:'rs',down:false});
        await wait(s=>s.gamepad?.connected&&s.gamepad.armed,'browser privacy poke and neutral handshake');
    };
    const menu=async()=>{if(!(await read()).gamepad.menuOwner){await button('menu');await wait(s=>s.gamepad.menuOwner,'controller menu owns input');}};
    const leaveMenu=async()=>{if((await read()).gamepad.menuOwner){await button('menu');await wait(s=>!s.gamepad.menuOwner,'controller menu releases input');}};
    const locate=selector=>evaluate(`(()=>{
        const requested=document.querySelector(${JSON.stringify(selector)});
        if(!requested)return {missing:true};
        const scope=document.querySelector('.cove-controller-dialog[open]')||document.getElementById('salvage-preview');
        let target=requested,closed;
        for(let at=requested.parentElement;at&&scope.contains(at);at=at.parentElement)if(at.tagName==='DETAILS'&&!at.open)closed=at;
        if(closed)target=closed.querySelector('summary');
        const choices=[...scope.querySelectorAll('button, summary, input:not([type="file"]), select, a[href]')].filter(element=>{
            if(element.disabled||!element.getClientRects().length)return false;
            for(let at=element;at;at=at.parentElement){if(at.hidden||at.inert)return false;
                if(at.tagName==='DETAILS'&&!at.open&&element!==at.querySelector('summary'))return false;if(at===scope)break;}
            const style=getComputedStyle(element);return style.display!=='none'&&style.visibility!=='hidden';
        });
        return {target:choices.indexOf(target),active:choices.indexOf(document.activeElement),count:choices.length,
            disabled:target.disabled,closed:Boolean(closed),modal:scope.tagName==='DIALOG',
            activeId:document.activeElement?.id,activeText:document.activeElement?.textContent?.trim().slice(0,60),
            targetText:target.textContent?.trim().slice(0,60)};
    })()`);
    const go=async selector=>{
        await menu();let steps=0;
        while(steps++<180&&Date.now()<deadline){
            const state=await locate(selector);
            if(state.missing)throw Error('Missing controller target: '+selector);
            if(state.target<0){await delay(110);continue;}
            if(state.active===state.target){
                if(state.closed){await button('a');continue;}
                report.navigation.push({selector,steps,focus:state});return;
            }
            const forward=(state.target-state.active+state.count)%state.count;
            const backwards=(state.active-state.target+state.count)%state.count;
            await direction(state.modal?'dx':'dy',state.active<0||forward<=backwards?1:-1);
        }
        throw Error('Controller could not reach '+selector+': '+JSON.stringify(await locate(selector)));
    };
    const use=async selector=>{await go(selector);await button('a');};
    const waitLibrary=async predicate=>wait(async()=>{const ui=(await dom()).library;return ui.busy==='false'&&predicate(ui);},'durable library operation');
    const name=async text=>{
        await use('#design-name');await wait(async()=>Boolean((await dom()).modal),'controller name keyboard');
        for(const character of text)await use(`[data-controller-key="${character}"]`);
        await go('.cove-controller-dialog .cove-dialog-actions button:last-child');await button('a');
        await wait(async()=>{const ui=await dom();return !ui.modal&&ui.library.name===text;},'controller typed exact name');
    };
    const keep=async()=>{await leaveMenu();await button('a');await wait(s=>!s.workshop.changed&&!s.workshop.brickTool,'Keep acknowledged');};
    const selectPart=async wanted=>{
        await leaveMenu();for(let count=0;count<16;++count){const s=await read();if(s.workshop.name===wanted)return s;await button('rb');}
        throw Error('Controller could not select '+wanted);
    };
    const launch=async label=>{
        const prior=(await read()).workshop.launches;await use('#workshop-launch');
        const state=await wait(s=>s.workshop.launches===prior+1&&!s.workshop.open&&!s.workshop.pending,'Launch acknowledged',{seconds:45});
        assert.equal(state.boat.parts,12);assert.equal(state.boat.massKg,993);
        assert.deepEqual(state.boat.paidPartIds,['35','36']);
        assert.deepEqual(state.session.inventory,{salvageMaterial:'42',specialMachinery:'0'});
        return capture(label);
    };
    const mouseSave=async()=>{
        const locate=()=>evaluate(`(()=>{const e=document.getElementById('salvage-save');
            if(!e||e.hidden||e.disabled)return null;e.scrollIntoView({block:'nearest'});
            const r=e.getBoundingClientRect(),x=r.x+r.width/2,y=r.y+r.height/2;
            return e.contains(document.elementFromPoint(x,y))?{x,y}:null;})()`);
        let point;const end=Math.min(deadline,Date.now()+10000);
        while(Date.now()<end){const before=await locate();await delay(160);const after=await locate();
            if(before&&after&&Math.hypot(before.x-after.x,before.y-after.y)<.5){point=after;break;}}
        assert(point,'real Save expedition button must be visible and unobstructed');
        await call('Input.dispatchMouseEvent',{type:'mouseMoved',...point});
        await call('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',buttons:1,clickCount:1,...point});
        await call('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',buttons:0,clickCount:1,...point});
        await wait(()=>evaluate('document.getElementById("salvage-save-status").textContent.startsWith("Saved.")'),'actual durable expedition save',{seconds:40});
    };
    try{
        const ready=await reply();assert.equal(ready.ready,true);report.controllerBackend=ready;
        await wait(s=>s.ready&&s.workshop?.canOpen&&s.boat?.active,'fresh Cove');await uncap();await handshake();
        const initial=await read();invariant(initial);report.initial=initial;
        assert.equal(initial.boat.parts,11);assert.equal(initial.boat.massKg,1035);
        assert.deepEqual(initial.boat.paidPartIds,[]);assert.deepEqual(initial.session.inventory,{salvageMaterial:'48',specialMachinery:'0'});
        await button('view');await wait(s=>s.workshop.open,'View opens workshop');
        // The browser reveals its workshop panel on the UI refresh. That panel
        // changes the usable camera rectangle after the engine first opens.
        // Establish a settled layout before attributing changes to the stick.
        let cameraKey=null,cameraStableSince=0;
        await wait(async s=>{
            const visible=await evaluate("!document.getElementById('salvage-workshop').hidden&&document.getElementById('salvage-preview').dataset.workshop==='true'");
            const next=JSON.stringify(s.workshop.camera);
            if(!visible||s.workshop.camera.framePending||next!==cameraKey){cameraKey=next;cameraStableSince=Date.now();return false;}
            return Date.now()-cameraStableSince>=200;
        },'visible workshop and settled camera rectangle');
        const cameraBefore=(await read()).workshop.camera,designBeforeCamera=await blueprint();
        report.cameraControls={before:cameraBefore};
        await cameraMove('rs','rx',.7);const cameraPan=(await read()).workshop.camera;
        report.cameraControls.pan=cameraPan;
        assert(Math.hypot(...cameraPan.target.map((value,i)=>value-cameraBefore.target[i]))>.0001,'R3 + right stick pans the actual workshop target');
        assert.equal(cameraPan.yaw,cameraBefore.yaw);assert.equal(cameraPan.distance,cameraBefore.distance);
        await cameraMove('ls','ry',.7);const cameraZoom=(await read()).workshop.camera;
        report.cameraControls.zoom=cameraZoom;
        assert(Math.abs(cameraZoom.distance-cameraPan.distance)>.0001,'L3 + right stick zooms the actual workshop camera');
        assert.deepEqual(cameraZoom.target,cameraPan.target);assert.equal(cameraZoom.yaw,cameraPan.yaw);
        assert.equal(await blueprint(),designBeforeCamera);assert.deepEqual(stock(await read()),stock(initial));
        report.cameraControls.designUnchanged=true;
        await selectPart('Cargo cradle');await use('#workshop-remove');await wait(s=>s.workshop.changed,'remove cradle preview');await keep();
        assert.equal((await read()).workshop.massKg,945);
        await use('#workshop-brick-2x2');await wait(s=>s.workshop.brickTool&&s.workshop.valid,'controller chooses a fitting 2x2');
        assert.equal((await read()).workshop.partCost,'3');await leaveMenu();await button('a');
        await wait(s=>s.workshop.placedBricks===1&&s.workshop.brickTool,'controller places exactly one brick');
        await button('b');await wait(s=>!s.workshop.brickTool&&!s.workshop.changed,'stop unused preview');
        await selectPart('Brick 2 x 2');const firstBlueprint=await blueprint();
        await capture('controller-built-one-brick');
        await use('#workshop-duplicate');let rejected=await wait(s=>s.workshop.changed&&!s.workshop.valid,'copy +32X is blocked by the 98-tick brick');
        assert(rejected.workshop.problemCode>0);assert.match((await dom()).problem,/blocked|overlap/i);
        await capture('copy-overlap-explained');await leaveMenu();await button('b');assert.equal(await blueprint(),firstBlueprint);
        await use('#workshop-duplicate');await leaveMenu();await button('y');await wait(s=>s.workshop.changed&&s.workshop.valid,'Snap finds a valid copied brick');await keep();
        assert.equal((await read()).workshop.parts,12);assert.equal((await read()).workshop.massKg,993);
        await button('lb');await use('#workshop-toggle-next');await wait(s=>s.workshop.selectedCount===2,'controller selects both new bricks');
        const groupBlueprint=await blueprint(),group=(await read()).workshop;
        assert.equal(group.charge,'6');assert.equal(group.refund,'0');
        await leaveMenu();await direction('rt',1);rejected=await wait(s=>s.workshop.changed&&!s.workshop.valid,'group raised one plate is disconnected');
        assert.deepEqual(rejected.workshop.selectedParts,group.selectedParts);assert.equal(rejected.workshop.placement[1],group.placement[1]+16);
        assert(rejected.workshop.problemCode>0);await button('a');assert.equal((await read()).workshop.revision,group.revision,'invalid Keep cannot commit');
        assert.deepEqual(stock(await read()),stock(initial),'all draft operations preserve owned boat and stock');
        await capture('group-move-refusal-preserves-ownership');await button('b');assert.equal(await blueprint(),groupBlueprint);
        await use('#workshop-undo');await wait(s=>s.workshop.parts===11&&!s.workshop.changed,'Undo removes the copied brick');assert.equal(await blueprint(),firstBlueprint);
        await use('#workshop-redo');await wait(s=>s.workshop.parts===12&&!s.workshop.changed,'Redo restores copied brick');assert.equal(await blueprint(),groupBlueprint);
        await capture('undo-redo-exact-design');
        // Redo restores its edit selection. Rebuild the explicit two-brick group
        // through the controller, without assuming selection is durable history.
        await leaveMenu();await selectPart('Brick 2 x 2');await button('lb');
        if((await read()).workshop.name!=='Brick 2 x 2')await button('rb');
        // Select the first of the two final membership slots, then toggle next.
        const parts=(await read()).workshop.parts;
        for(let i=0;i<parts;++i){const s=await read();if(s.workshop.selected===group.selectedParts[0])break;await button('rb');}
        assert.equal((await read()).workshop.selected,group.selectedParts[0]);
        if((await read()).workshop.selectedCount>1)await use('#workshop-only-primary');
        await use('#workshop-toggle-next');await wait(s=>s.workshop.selectedCount===2,'restore brick group');
        await name('BUILD');await use('#design-save-new');await waitLibrary(ui=>ui.choices.some(row=>row.name==='BUILD')&&ui.message.startsWith('Saved'));
        await name('COPY');await use('#design-duplicate');await waitLibrary(ui=>ui.choices.length===2&&ui.name==='COPY'&&ui.message.startsWith('Saved'));
        report.originalBlueprint=groupBlueprint;
        await use('#workshop-paint-blue');await wait(s=>s.workshop.changed,'group paint preview');await keep();
        const blueBlueprint=await blueprint();assert.notEqual(blueBlueprint,groupBlueprint);report.blueBlueprint=blueBlueprint;
        await use('#design-update');await waitLibrary(ui=>ui.message.startsWith('Saved')&&ui.name==='COPY');
        const beforeLaunch=await capture('controller-named-saved-copied-blue-group');assert.deepEqual(stock(beforeLaunch),stock(initial));
        await launch('controller-launched-paid-build');await button('menu');await wait(s=>s.pause.phase==='paused','controller pauses for physical save');
        await mouseSave();const saved=await capture('durably-saved-blue-boat-and-library');
        await call('Page.reload',{ignoreCache:true});await delay(300);
        await wait(s=>s.ready&&s.restore?.phase==='ready'&&s.pause.phase==='paused','restored saved boat',{reload:true,seconds:60});
        await uncap();await handshake();const restored=await read();
        assert.equal(restored.world,saved.world);assert.equal(restored.boat.buildId,saved.boat.buildId);
        assert.equal(restored.boat.topologyRevision,saved.boat.topologyRevision);assert.deepEqual(stock(restored),stock(saved));
        await button('menu');await wait(s=>s.pause.phase==='running','controller resumes saved boat');
        await button('view');await wait(s=>s.workshop.open,'controller reopens restored workshop');assert.equal(await blueprint(),blueBlueprint);
        await go('#design-list');await waitLibrary(ui=>ui.choices.length===2);
        for(let i=0;i<3;++i){const ui=(await dom()).library;if(ui.name==='BUILD'&&ui.choices.find(row=>row.id===ui.selected)?.name==='BUILD')break;await direction('dx',1);}
        assert.equal((await dom()).library.name,'BUILD');await use('#design-load');await waitLibrary(ui=>ui.message.startsWith('Loaded “BUILD”'));
        assert.equal(await blueprint(),groupBlueprint);assert.equal((await read()).workshop.charge,'0');assert.equal((await read()).workshop.refund,'0');
        assert.deepEqual(stock(await read()),stock(saved));await capture('controller-loaded-exact-durable-original-design');
        const final=await launch('controller-launched-loaded-design-for-free');assert.deepEqual(stock(final),stock(saved));
        report.status='passed';
    }catch(error){
        report.status='failed';report.error=String(error);try{report.failureState=await read();report.failureDom=await dom();}catch{}
        throw error;
    }finally{
        if(!exitInfo){child.stdin.write('{"close":true}\n');child.stdin.end();const end=Date.now()+2000;while(!exitInfo&&Date.now()<end)await delay(20);}
        if(!exitInfo){child.kill('SIGTERM');await delay(100);}
        lines.close();report.controllerExit=exitInfo;report.controllerStderr=stderr;report.seconds=(Date.now()-started)/1000;
        if(report.status==='passed'&&(exitInfo?.code!==0||exitInfo?.signal||stderr)){report.status='failed';report.error='Controller helper did not exit cleanly';}
        await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));
    }
    assert.equal(report.status,'passed',report.error);return report;
}
