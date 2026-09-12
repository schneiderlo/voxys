// One cohesive OS-controller journey. No navigator override, game setters,
// screenshots or repeated sailing. Blueprint export and DOM inspection are read-only.
import assert from 'node:assert/strict';
import {mkdir,readFile,writeFile} from 'node:fs/promises';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {fileURLToPath} from 'node:url';

export async function validateCoveUI(call,directory,{expectedPresentationParts=9,appliedSettingsReportPath=null,savedContinuationReportPath=null,restoredDesignReportPath=null}={}){
    assert(Number.isInteger(expectedPresentationParts)&&expectedPresentationParts>=1&&expectedPresentationParts<=16);
    await mkdir(directory,{recursive:true});
    const maximumSeconds=savedContinuationReportPath?120:240,started=Date.now(),deadline=started+maximumSeconds*1000;
    const report={status:'running',maximumSeconds,expectedPresentationParts,
        kind:'Real controller menus/settings/practice; remapped keyboard Pause/Save; saved-history restore; no images or game-state injection',
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
    const dom=()=>evaluate(`(()=>{const active=document.activeElement;return {
        active:{id:active?.id,tag:active?.tagName,text:active?.textContent?.trim().slice(0,120)},
        menu:document.getElementById('salvage-more-controls')?.open,
        page:document.querySelector('[data-cove-page][aria-pressed="true"]')?.dataset.covePage,
        save:document.getElementById('salvage-save-status')?.textContent,
        settings:document.getElementById('cove-preferences-status')?.textContent,
        caption:document.getElementById('cove-event-caption')?.textContent,
        modal:Boolean(document.querySelector('.cove-controller-dialog[open]')),
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
        const panel=document.getElementById('salvage-preview'),more=document.getElementById('salvage-more-controls');
        const scope=document.querySelector('.cove-controller-dialog[open]')||(!document.getElementById('salvage-workshop').hidden?panel:more.open?more:panel);
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
    const preferences=()=>evaluate("JSON.parse(voxyModule.ccall('voxy_cove_preferences_action','string',['number','string'],[1,'']))");
    const setOption=async(selector,value)=>{
        await go(selector);
        for(let count=0;count<100;++count){
            const options=await evaluate(`(()=>{const e=document.querySelector(${JSON.stringify(selector)}),options=[...e.options].filter(o=>!o.disabled&&!o.hidden);return {value:e.value,at:options.findIndex(o=>o.value===e.value),target:options.findIndex(o=>o.value===${JSON.stringify(String(value))}),count:options.length};})()`);
            if(options.value===String(value))return;assert(options.target>=0,'requested option is offered');
            await direction('dx',(options.target-options.at+options.count)%options.count<=(options.at-options.target+options.count)%options.count?1:-1);
        }
        throw Error('Could not select '+selector);
    };
    const click=async selector=>{
        const locate=()=>evaluate(`(()=>{const e=document.querySelector(${JSON.stringify(selector)});if(!e||e.hidden||e.disabled||!e.getClientRects().length)return null;
            e.scrollIntoView({block:'nearest'});const r=e.getBoundingClientRect(),x=r.x+r.width/2,y=r.y+r.height/2;
            return e.contains(document.elementFromPoint(x,y))?{x,y}:null;})()`);
        let point;const end=Math.min(deadline,Date.now()+10000);
        while(Date.now()<end){const before=await locate();await delay(120);const after=await locate();if(before&&after&&Math.hypot(before.x-after.x,before.y-after.y)<.5){point=after;break;}}
        assert(point,'visible unobstructed pointer target '+selector);
        await call('Input.dispatchMouseEvent',{type:'mouseMoved',...point});
        await call('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',buttons:1,clickCount:1,...point});
        await call('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',buttons:0,clickCount:1,...point});
    };
    const layout=async label=>{
        const measured=await evaluate(`(()=>{const panel=document.getElementById('salvage-preview'),active=document.activeElement;
            const bounds=e=>{const r=e.getBoundingClientRect();return {left:r.left,top:r.top,right:r.right,bottom:r.bottom,width:r.width,height:r.height};};
            const p=bounds(panel),a=bounds(active),x=(a.left+a.right)/2,y=(a.top+a.bottom)/2;
            const horizontal=[...panel.querySelectorAll('button,summary,label,p,h3,h4,select')].filter(e=>e.getClientRects().length).map(e=>({id:e.id,tag:e.tagName,...bounds(e)}));
            return {width:innerWidth,height:innerHeight,panel:p,active:a,activeId:active.id,fontSize:getComputedStyle(panel).fontSize,
                unobstructed:active.contains(document.elementFromPoint(x,y)),horizontal,bodyWidth:document.body.scrollWidth};})()`);
        report.navigation.push({layout:label,...measured});await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));
        assert(measured.panel.left>=-1&&measured.panel.right<=measured.width+1&&measured.panel.top>=-1&&measured.panel.bottom<=measured.height+1);
        assert(measured.active.height>=43&&measured.active.top>=0&&measured.active.bottom<=measured.height&&measured.unobstructed,'focused control remains readable and reachable after normal focus scrolling');
        for(const item of measured.horizontal)assert(item.left>=measured.panel.left-1&&item.right<=measured.panel.right+1,'no horizontal content clipping: '+JSON.stringify(item));
        assert(measured.bodyWidth<=measured.width+1);return measured;
    };
    const resizeViewport=async(width,height)=>{
        // CDP changes the reported viewport before a browser rendering opportunity
        // necessarily delivers resize. Observe that event and its following frame;
        // never scroll or repair focus from the acceptance driver.
        await evaluate(`(()=>{const panel=document.getElementById('salvage-preview');
            const sample=label=>{const active=document.activeElement,r=active.getBoundingClientRect();return {label,at:performance.now(),width:innerWidth,height:innerHeight,
                focus:active.id,owner:window.voxyControllerMenuActive?.(),scrollTop:panel.scrollTop,font:getComputedStyle(panel).fontSize,top:r.top,bottom:r.bottom};};
            const evidence={samples:[sample('before-request')],delivered:false,frame:false};globalThis.__coveUiResizeEvidence=evidence;
            const resized=()=>{if(innerWidth!==${width}||innerHeight!==${height})return;
                window.removeEventListener('resize',resized);evidence.delivered=true;evidence.samples.push(sample('resize-delivered'));
                requestAnimationFrame(()=>{evidence.samples.push(sample('following-browser-frame'));evidence.frame=true;});};
            window.addEventListener('resize',resized);})();`);
        await call('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:false});
        await wait(()=>evaluate('globalThis.__coveUiResizeEvidence?.frame===true'),'actual resize event and following browser frame');
        report.navigation.push({resize:await evaluate('globalThis.__coveUiResizeEvidence')});
        await writeFile(`${directory}/summary.json`,JSON.stringify(report,null,2));
    };
    const landingFrame=()=>evaluate('voxyModule._voxy_get_frame_count()');
    const waitLanding=async(predicate,label)=>{
        const end=Math.min(deadline,Date.now()+20000);while(Date.now()<end){try{if(await predicate())return;}catch{}await delay(35);}throw Error(label);
    };
    const landingEdge=async(down,up,navigates=false)=>{
        const before=await landingFrame();let actualPad;
        const sourceUrl=navigates?await evaluate('location.href'):null;let received;
        try{
            await send(down);await delay(140);
            if(navigates){
                await waitLanding(async()=>{
                    const sample=await evaluate(`(()=>{const url=location.href;return {url,frame:url===${JSON.stringify(sourceUrl)}?(voxyModule?._voxy_get_frame_count?.()??null):null};})()`);
                    if(sample.url===navigates||(sample.url===sourceUrl&&sample.frame>before)){received=sample;return true;}return false;
                },'title samples held Confirm or navigates to the exact saved URL');
            }else{await waitLanding(async()=>await landingFrame()>before,'landing received controller frame');actualPad=await observedPad();}
        }finally{await send(up);}
        if(!navigates){const release=await landingFrame();await waitLanding(async()=>await landingFrame()>release+1,'landing received neutral release');}
        report.controls.push({landing:down,before,navigates,sourceUrl,received,actualPad});
    };
    const landingGo=async selector=>{
        for(let step=0;step<40;++step){
            const position=await evaluate(`(()=>{const panel=document.getElementById('cove-landing'),target=document.querySelector(${JSON.stringify(selector)});
                const list=[...panel.querySelectorAll('button,summary,a[href]')].filter(e=>window.VoxyControllerMenu.visible(e,panel,window));
                return {target:list.indexOf(target),active:list.indexOf(document.activeElement),count:list.length};})()`);
            assert(position.target>=0,'title target is visible');if(position.target===position.active)return;
            const forward=(position.target-position.active+position.count)%position.count,backward=(position.active-position.target+position.count)%position.count;
            await landingEdge({axis:'dy',value:position.active<0||forward<=backward?1:-1},{axis:'dy',value:0});
        }
        throw Error('Controller could not reach title '+selector);
    };
    const economy=s=>({inventory:s.session.inventory,job:{phase:s.job.phase,revision:s.job.revision,durable:s.job.durable},harbor:{installed:s.harbor.installed}});
    const sameEconomy=(actual,baseline)=>assert.deepEqual(economy(actual),economy(baseline),'test/menu actions preserve actual economy and job state');
    const verifyRestoredDesign=async name=>{
        // Outside an owned menu, the bound Pause key opens Overview even when
        // already paused (Application::updateCovePlayer). Resume is the real
        // existing forwarded button, whose handler also releases menu ownership.
        if(!(await dom()).menu)await key('o','KeyO',79);
        await wait(async s=>s.pause.phase==='paused'&&(await dom()).menu&&(await dom()).page==='pause','remapped O opens paused Overview');
        await use('#cove-menu-resume');
        await wait(async s=>s.pause.phase==='running'&&!s.gamepad.menuOwner&&!(await dom()).menu
            &&await evaluate('window.voxyControllerMenuActive?.()===false'),'actual Resume and DOM poll release menu ownership');
        await button('view');await wait(s=>s.ready&&!s.busy&&s.workshop.open&&!s.workshop.pending,'View opens the ready restored owned design at the saved berth');
        assert.equal(await blueprint(),report.originalBlueprint);await capture(name);
    };
    const finishSavedPicker=async saved=>{
        await use('#salvage-leave');await wait(async()=> (await dom()).modal,'owned Leave confirmation');
        await go('.cove-dialog-actions button:last-child');
        // Leaving destroys this page's frame counter. Deliver one real focused
        // Confirm and release it, then join the destination page by identity.
        // Do not wait for the old Cove serial to advance across navigation.
        report.controls.push({navigation:'confirmed Leave',focus:await dom()});
        try{await send({button:'a',down:true});await delay(140);}finally{await send({button:'a',down:false});}
        await wait(()=>evaluate('Boolean(document.getElementById("cove-landing")&&!document.getElementById("cove-landing").hidden)'), 'Leave completes and title appears',{reload:true,seconds:45});
        await waitLanding(()=>evaluate('Boolean(voxyModule?._voxy_is_initialized?.()===1&&window.voxyCoveLandingMenuActive?.())'),'real title input owner ready');
        // Landing owns input, so global F9 is deliberately unavailable here.
        // Real OS edges remain held until an actual title frame samples them.
        await landingEdge({button:'rs',down:true},{button:'rs',down:false});
        await landingGo('#cove-saved-worlds > summary');await landingEdge({button:'a',down:true},{button:'a',down:false});
        const titleStyle=await evaluate("({scale:getComputedStyle(document.documentElement).getPropertyValue('--cove-ui-scale').trim(),contrast:document.documentElement.getAttribute('data-cove-contrast')})");
        assert.equal(titleStyle.scale,'1.5');assert.equal(titleStyle.contrast,'true');report.landingStyle=titleStyle;
        const link=await evaluate(`(()=>{const links=[...document.querySelectorAll('#cove-saved-list a')];return links.map(a=>({href:a.href,label:a.textContent}));})()`);
        assert.equal(link.length,1);assert.equal(new URL(link[0].href).searchParams.get('world'),saved.world);report.savedHistory=link;
        await landingGo('#cove-saved-list a');await landingEdge({button:'a',down:true},{button:'a',down:false},link[0].href);
        const restored=await wait(s=>s.ready&&s.restore?.phase==='ready'&&s.pause.phase==='paused'&&s.ui?.textScale===1.5,'strict history restore and preference reload',{reload:true,seconds:60});
        await uncap();await handshake();assert.equal(restored.world,saved.world);assert.equal(restored.boat.buildId,saved.boat.buildId);
        assert.equal(restored.boat.topologyRevision,saved.boat.topologyRevision);assert.deepEqual(restored.boat.roots.map(root=>root.key),saved.boat.roots.map(root=>root.key));
        assert.deepEqual(stock(restored),stock(saved));sameEconomy(restored,saved);
        const prefs=await preferences();assert.deepEqual(prefs,report.preferences,'strict reload restores every applied preference');
        await verifyRestoredDesign('saved-picker-restores-exact-owned-design-and-preferences');
    };
    try{
        const ready=await reply();assert.equal(ready.ready,true);report.controllerBackend=ready;
        if(savedContinuationReportPath){
            const previous=JSON.parse(await readFile(savedContinuationReportPath,'utf8'));
            const priorSave=previous.stages.find(stage=>stage.name==='normal-save-keeps-original-expedition-and-settings');
            assert(priorSave?.ui?.save?.startsWith('Saved.')&&previous.originalBlueprint&&previous.preferences,'baseline contains an actual durable-save capture, design and preferences');
            const saved=priorSave.state;
            const restored=await wait(s=>s.ready&&s.restore?.phase==='ready'&&s.pause.phase==='paused'&&s.ui?.textScale===1.5,
                'strict existing saved world and retained settings',{seconds:60});
            invariant(restored);assert.equal(restored.world,saved.world);assert.equal(restored.boat.buildId,saved.boat.buildId);
            assert.equal(restored.boat.topologyRevision,saved.boat.topologyRevision);assert.deepEqual(restored.boat.roots.map(root=>root.key),saved.boat.roots.map(root=>root.key));
            assert.deepEqual(stock(restored),stock(saved));sameEconomy(restored,saved);
            assert.deepEqual(await preferences(),previous.preferences,'all retained canonical preferences match actual applied source');
            report.preferences=previous.preferences;report.originalBlueprint=previous.originalBlueprint;report.initial=restored;
            report.continuedEvidence={path:savedContinuationReportPath,world:saved.world,stages:previous.stages.map(stage=>stage.name),
                note:'Only saved-world Leave and controller history reload remain. No settings, layout, Test/Return or manual save is repeated; normal strict restoration may republish its lineage.'};
            await uncap();await handshake();await capture('strict-saved-world-start-for-picker-continuation');
            if(restoredDesignReportPath){
                const picker=JSON.parse(await readFile(restoredDesignReportPath,'utf8'));
                assert.equal(picker.failureState?.world,saved.world);assert.equal(picker.failureState?.restore?.phase,'ready');
                assert.equal(picker.failureDom?.save,'Loaded. Resume when ready.');
                assert(picker.savedHistory?.some(link=>new URL(link.href).searchParams.get('world')===saved.world),'previous real picker reached this saved world');
                assert.deepEqual(picker.preferences,previous.preferences);
                report.continuedPickerEvidence={path:restoredDesignReportPath,world:saved.world,
                    note:'Prior controller Leave/history navigation and strict reload are retained. Only explicit Resume and the original blueprint check run here.'};
                await verifyRestoredDesign('explicit-resume-opens-exact-restored-original-blueprint');
            }else await finishSavedPicker(restored);
        }else{
        await wait(s=>s.ready&&s.ui?.tutorial&&s.workshop?.canOpen&&s.boat?.active,'fresh admitted Cove UI');await uncap();await handshake();
        const initial=await read();invariant(initial);report.initial=initial;assert.equal(initial.boat.parts,11);assert.equal(initial.boat.massKg,1035);
        assert.deepEqual(initial.boat.paidPartIds,[]);assert.deepEqual(initial.session.inventory,{salvageMaterial:'48',specialMachinery:'0'});
        const viewport=await evaluate('({width:innerWidth,height:innerHeight})');report.viewport=viewport;
        await button('menu');await wait(async s=>s.pause.phase==='paused'&&(await dom()).page==='pause','controller opens paused overview');
        await use('[data-cove-page="settings"]');
        let prefs;
        if(appliedSettingsReportPath){
            const previous=JSON.parse(await readFile(appliedSettingsReportPath,'utf8'));
            assert(previous.stages.some(stage=>stage.name==='controller-applied-accessibility-and-remapped-pause'),'retained report actually passed settings controls');
            prefs=await preferences();assert.deepEqual(prefs,previous.preferences,'optional device preferences survived the prior actual controller application');
            report.appliedSettingsEvidence={path:appliedSettingsReportPath,world:previous.initial.world,stage:'controller-applied-accessibility-and-remapped-pause',
                note:'Earlier world was not yet saved. This fresh Cove retains the actual applied device preferences and continues unpassed checks.'};
        }else{
            await go('#cove-pref-textScale');await layout('standard-text-wide');
            await setOption('#cove-pref-textScale','1.5');await use('#cove-pref-highContrast');
            await setOption('#cove-binding-action','pause');await setOption('#cove-binding-key','79');await use('#cove-preferences-apply');
            await wait(s=>s.ui.textScale===1.5&&s.ui.highContrast,'accepted text scale and contrast');
            prefs=await preferences();await capture('controller-applied-accessibility-and-remapped-pause');
        }
        assert.equal(prefs.textScale,1.5);assert.equal(prefs.highContrast,true);assert.equal(prefs.bindings.find(row=>row.action==='pause').key,79);report.preferences=prefs;
        await go('#cove-preferences-apply');await resizeViewport(640,480);
        await go('#cove-preferences-apply');const large=await layout('150-percent-small-screen');assert(parseFloat(large.fontSize)>=19.49);
        for(const page of ['job','map','inventory','help']){
            await use(`[data-cove-page="${page}"]`);await wait(async()=> (await dom()).page===page,'actual '+page+' page');
            await go('#cove-menu-back');await layout(page+'-150-percent-small-screen');
            if(page==='job'){
                await use('#cove-menu-resume');await wait(async s=>s.pause.phase==='running'&&(await dom()).page==='job'
                    &&await evaluate('!document.getElementById("salvage-job-accept").disabled'),'Resume keeps actual Job page available');
                await key('o','KeyO',79);await wait(s=>s.pause.phase==='paused','pause again without leaving Job');
            }

            if(page==='map')assert(await evaluate('document.querySelectorAll("#cove-landmark-list li").length')>=5,'map lists actual admitted landmarks');
            if(page==='inventory')assert.match(await evaluate('document.getElementById("cove-inventory").textContent'),/48/);
        }
        await button('b');await wait(async()=> (await dom()).page==='pause','controller Back returns overview');
        await capture('controller-reaches-help-map-inventory-at-large-text');
        await resizeViewport(viewport.width,viewport.height);
        // A real pointer switches from controller focus to the existing Resume
        // button. The old key must stay inactive; the rebound key must pause.
        await click('[data-cove-forward="salvage-pause"]');await wait(s=>s.pause.phase==='running','physical pointer resumes');
        await key('p','KeyP',80);await fresh((await read()).assetFixture.submittedSerial);assert.equal((await read()).pause.phase,'running','old P binding is inactive');
        await key('o','KeyO',79);await wait(async s=>s.pause.phase==='paused'&&(await dom()).menu,'new O key opens paused menu');
        await go('[data-cove-forward="salvage-pause"]');await key('o','KeyO',79);await wait(s=>s.pause.phase==='running','rebound O also resumes while general menu owns keyboard');
        await button('view');await wait(s=>s.workshop.open,'controller View opens workshop');
        report.originalBlueprint=await blueprint();
        for(let count=0;count<16&&(await read()).workshop.name!=='Cargo cradle';++count)await button('rb');
        assert.equal((await read()).workshop.name,'Cargo cradle');await use('#workshop-remove');await wait(s=>s.workshop.changed,'remove cradle draft');
        await use('#workshop-keep');await wait(s=>!s.workshop.changed&&s.workshop.parts===10,'kept ten-part test draft');
        const draft=await blueprint(),draftState=await read();report.testBlueprint=draft;
        assert.notEqual(draft,report.originalBlueprint);assert.equal(draftState.workshop.massKg,945);assert.deepEqual(stock(draftState),stock(initial));
        await capture('kept-test-design-does-not-spend-or-replace-owned-boat');
        await use('#workshop-test');const tested=await wait(s=>s.practice?.phase==='running'&&s.practice.canReturn&&!s.workshop.open,'temporary boat admitted',{seconds:50});
        assert.equal(tested.boat.parts,10);assert.equal(tested.boat.massKg,945);sameEconomy(tested,initial);
        await wait(()=>evaluate(`['salvage-save','salvage-job-accept','salvage-job-deliver','salvage-harbor-install','workshop-launch','design-save-new'].every(id=>document.getElementById(id).disabled)`),'test permissions reach the visible UI');
        const forbidden=await evaluate(`['salvage-save','salvage-job-accept','salvage-job-deliver','salvage-harbor-install','workshop-launch','design-save-new'].map(id=>({id,disabled:document.getElementById(id).disabled}))`);
        assert(forbidden.every(row=>row.disabled),'normal save/reward/library/Launch controls are unavailable in test');
        await capture('temporary-test-runs-without-economy-or-save-authority',{forbidden});
        await click('#cove-practice-return');const returned=await wait(s=>!s.practice.active&&Number(s.practice.returns)===1&&s.workshop.open&&s.pause.phase==='paused','Return restores paused workshop',{seconds:50});
        assert.deepEqual(stock(returned),stock(initial));sameEconomy(returned,initial);assert.equal(await blueprint(),draft);
        assert.equal(returned.workshop.undo,draftState.workshop.undo);assert.equal(returned.workshop.parts,10);assert.equal(returned.boat.buildId,initial.boat.buildId);
        await capture('return-restores-exact-draft-history-and-original-ownership');
        // Resume and close the workshop without Launch. The saved expedition is
        // the unchanged owned boat; the unlaunched local draft is intentionally
        // not claimed as a durable blueprint library entry.
        await click('#salvage-pause');await wait(s=>s.pause.phase==='running','Resume restored workshop');
        await button('view');await wait(s=>!s.workshop.open,'close workshop without a paid Launch');
        await key('o','KeyO',79);await wait(s=>s.pause.phase==='paused','pause original expedition for save');
        await go('[data-cove-forward="salvage-save"]');await key('F10','F10',121);
        await wait(()=>evaluate('document.getElementById("salvage-save-status").textContent.startsWith("Saved.")'),'physical bound F10 confirms normal durable save',{seconds:40});
        const saved=await capture('normal-save-keeps-original-expedition-and-settings');assert.deepEqual(stock(saved),stock(initial));
        await finishSavedPicker(saved);
        }
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
