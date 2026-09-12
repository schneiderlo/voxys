(function (root, factory) {
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.VoxySalvagePreview = api;
})(globalThis, function () {
    'use strict';
    function install(engine, environment = globalThis) {
        const document = environment.document;
        const panel = document.getElementById('salvage-preview');
        const cove = panel.dataset?.scene === 'cove';
        const status = document.getElementById('salvage-status');
        const reset = document.getElementById('salvage-reset');
        const pause = document.getElementById('salvage-pause');
        const leave = document.getElementById('salvage-leave');
        const moreControls=document.getElementById('salvage-more-controls');
        if(moreControls)moreControls.open=!cove;
        let wasWorkshop=false;
        if (cove) leave.textContent = 'Leave Cove';
        else document.getElementById('salvage-field-tools')?.setAttribute('open','');
        const interact = document.getElementById('salvage-interact');
        const objectivePanel=document.getElementById('salvage-objective');
        const objectiveTitle=document.getElementById('salvage-objective-title');
        const objectiveDetail=document.getElementById('salvage-objective-detail');
        const objectiveButton=document.getElementById('salvage-objective-action');
        const fieldTools=document.getElementById('salvage-field-tools');
        let objectiveAction=null;
        const cutter = document.getElementById('salvage-cut');
        const workshopPanel=document.getElementById('salvage-workshop');
        const workshopToggle=document.getElementById('salvage-workshop-toggle');
        const workshopButtons=workshopPanel ? Array.from(workshopPanel.querySelectorAll('[data-workshop-action]')) : [];
        const workshopHandlers=new Map();
        const cameraPanel=document.getElementById('salvage-camera');
        const cameraButtons=cameraPanel?Array.from(cameraPanel.querySelectorAll('[data-camera-action]')):[];
        const cameraHandlers=new Map();
        let cameraAvailable=false;
        const priorCameraMenu=environment['voxyCoveCameraMenu'];
        const controllerMenu=cove?environment.VoxyControllerMenu?.install(environment,panel):null;
        const designLibrary=environment.VoxyDesignLibrary?.install(engine,environment,controllerMenu);
        const expeditionSaves=environment.VoxyCoveSaves?.install(engine,environment);
        const coveMenu=environment.VoxyCoveMenu?.install(engine,environment,controllerMenu,expeditionSaves);
        const jobPanel=document.getElementById('salvage-job');
        const jobStatus=document.getElementById('salvage-job-status');
        const jobButtons=['salvage-job-accept','salvage-job-deliver'].map(id=>document.getElementById(id));
        const jobHandlers=new Map();
        const harborPanel=document.getElementById('salvage-harbor');
        const harborStatus=document.getElementById('salvage-harbor-status');
        const harborButtons=['install','attach','raise','lower','stop','release']
            .map(name=>document.getElementById(`salvage-harbor-${name}`));
        const harborHandlers=[];
        let harborHeld=false,harborAwaiting=false;
        const towPanel = document.getElementById('salvage-towing');
        const towStatus = document.getElementById('salvage-tow-status');
        const towButtons = ['salvage-hook','salvage-reel','salvage-payout','salvage-hold']
            .map(id => document.getElementById(id));
        const towHandlers = new Map();
        const lodPanel = document.getElementById('salvage-lods');
        const lodButtons = lodPanel ? Array.from(lodPanel.querySelectorAll('button')) : [];
        const lodHandlers = new Map();
        const guidePanel = document.getElementById('salvage-guides');
        const guideButtons = guidePanel ? Array.from(guidePanel.querySelectorAll('button')) : [];
        const inspectionButtons = [...lodButtons, ...guideButtons];
        let stopped = false, pending = null, resetCount = 0, requestedReset = 0, hasRescue = false;
        let pausePhase='running',brickToolActive=false;
        let leaveConfirming=false;
        let timer;
        const cleanup = () => {
            if (stopped) return;
            blurHarbor();
            stopped = true;
            designLibrary?.cleanup();
            coveMenu?.cleanup();
            controllerMenu?.cleanup();
            expeditionSaves?.cleanup();
            environment.clearInterval(timer);
            reset.removeEventListener('click', onReset);
            pause?.removeEventListener('click',onPause);
            leave.removeEventListener('click', onLeave);
            interact?.removeEventListener('click', onInteract);
            objectiveButton?.removeEventListener('click',onObjective);
            objectiveAction=null;
            if(objectivePanel)objectivePanel.hidden=true;
            cutter?.removeEventListener('click',onCut);
            workshopToggle?.removeEventListener('click',onWorkshop);
            panel.removeEventListener('keydown',onWorkshopKey);
            cameraPanel?.removeEventListener('keydown',onCameraKey);
            cameraPanel?.removeEventListener('keyup',onCameraKey);
            for(const [button,handler] of cameraHandlers)button.removeEventListener('click',handler);
            if(environment['voxyCoveCameraMenu']===openCameraMenu)environment['voxyCoveCameraMenu']=priorCameraMenu;
            if(cameraPanel){cameraPanel.hidden=true;cameraPanel.open=false;}cameraAvailable=false;
            if(moreControls)moreControls.open=false;
            for(const [button,handler] of workshopHandlers)button.removeEventListener('click',handler);
            for (const [button, handler] of jobHandlers) button.removeEventListener('click', handler);
            for (const [button, handler] of towHandlers) button.removeEventListener('click', handler);
            for (const [button, handler] of lodHandlers) button.removeEventListener('click', handler);
            for(const [button,event,handler] of harborHandlers)button.removeEventListener(event,handler);
            environment.removeEventListener('pointerup',stopHarbor);
            environment.removeEventListener('pointercancel',stopHarbor);
            environment.removeEventListener('blur',blurHarbor);
            environment.removeEventListener('pagehide', cleanup);
            panel.hidden = true;
        };
        const fail = () => {
            status.textContent = 'Preview stopped. Reload the page to try again.';
            reset.disabled = leave.disabled = true;
            if(pause)pause.disabled=true;
            if (interact) interact.disabled = true;
            if(cutter)cutter.disabled=true;
            if(workshopToggle)workshopToggle.disabled=true;
            for(const button of workshopButtons)button.disabled=true;
            cameraAvailable=false;
            for(const button of cameraButtons)button.disabled=true;
            if(cameraPanel)cameraPanel.open=false;
            controllerMenu?.tick({active:false,ready:false,failed:true});
            for (const button of [...inspectionButtons,...towButtons.filter(Boolean),...jobButtons.filter(Boolean),...harborButtons.filter(Boolean)]) button.disabled = true;
            pending = null;
            objectiveAction=null;
            if(objectiveButton){objectiveButton.hidden=true;objectiveButton.disabled=true;}
            if(objectiveTitle)objectiveTitle.textContent='Cove unavailable';
            if(objectiveDetail)objectiveDetail.textContent='Reload the page to try again.';
        };
        const act = action => {
            if (stopped || engine._voxy_is_initialized?.() !== 1) return false;
            try { return engine._voxy_salvage_preview_action(action) === 1; }
            catch { fail(); return false; }
        };
        const stopHarbor=()=>{if(harborHeld){act(56);harborHeld=false;}};
        const blurHarbor=()=>{stopHarbor();if(harborAwaiting){act(56);harborAwaiting=false;}};
        const onReset = () => {
            if (pending || reset.disabled) return;
            if (act(1)) {
                pending = hasRescue?'rescue':'reset'; requestedReset = resetCount;
                reset.disabled = true;
                for (const button of [...inspectionButtons,...towButtons.filter(Boolean),...jobButtons.filter(Boolean),...harborButtons.filter(Boolean)]) button.disabled = true;
                status.textContent = hasRescue?'Recovering your boat and fitted parts…':'Resetting the cove…';
            }
        };
        const onLeave = async () => {
            if (pending === 'leave' || leave.disabled || leaveConfirming) return;
            if(cove&&controllerMenu?.confirm){
                let before;try{before=JSON.parse(engine.UTF8ToString(engine._voxy_get_salvage_preview_json()));}catch{return;}
                // Revoked admission already prevents progress; its existing
                // Leave action must remain an escape even when UI ownership
                // can no longer open a confirmation modal.
                if(before.session?.admissionOpen!==false){
                    const saved=expeditionSaves?.status?.();leaveConfirming=true;
                    const accepted=await controllerMenu.confirm({title:'Leave the Cove?',confirmLabel:'Leave without saving',
                        message:before.practice?.active?'Leaving discards this temporary test. Return to the workshop first to recover your draft. Your saved expedition stays unchanged.':(saved?.hasConfirmedSave?'Your last confirmed checkpoint remains available. ':'No checkpoint has been confirmed. ')
                            +'Leaving does not save. Cancel, pause and choose Save checkpoint to keep your latest progress.'});
                    leaveConfirming=false;if(stopped||!accepted)return;
                    let after;try{after=JSON.parse(engine.UTF8ToString(engine._voxy_get_salvage_preview_json()));}catch{return;}
                    if(before.world!==after.world||before.observation?.incarnation!==after.observation?.incarnation)return;
                    tick();if(leave.disabled||pending)return;
                }
            }
            if (act(2)) {
                pending = 'leave';
                reset.disabled = leave.disabled = true;
                for (const button of [...inspectionButtons,...towButtons.filter(Boolean),...jobButtons.filter(Boolean),...harborButtons.filter(Boolean)]) button.disabled = true;
                status.textContent = 'Leaving the cove…';
            }
        };
        const onInteract = () => {
            if (!pending && interact && !interact.disabled) act(30);
        };
        const onPause=()=>{
            if(!pending && pause && !pause.disabled && act(pausePhase==='paused'?91:90))tick();
        };
        const onWorkshopKey=event=>{
            // Keep button/text actions in the UI. Shift alone is also a camera
            // gesture modifier: retain it when a player holds Shift before
            // dragging from a focused UI button back onto the build surface.
            // Key-up still reaches the engine so a released modifier cannot stick.
            if(panel.dataset?.workshop==='true'&&!['ShiftLeft','ShiftRight'].includes(event.code))event.stopPropagation();
        };
        const onCut=()=>{if(!pending&&cutter&&!cutter.disabled&&act(95))tick();};
        const onCameraKey=event=>{
            if(!cameraPanel||cameraPanel.hidden)return;
            // A mouse-opened drawer has not reset the engine's held keys.
            // Let their releases through; an owned controller menu has already
            // cleared those inputs and may safely consume both event edges.
            if(event.type==='keyup'&&!controllerMenu?.active?.())return;
            event.stopPropagation();
            if(event.type==='keydown'&&(event.key==='F2'||event.key==='Escape')){
                event.preventDefault();
                if(controllerMenu?.active?.())environment.voxyControllerMenuInput?.({menu:true});
                else {cameraPanel.open=false;document.getElementById('voxy-canvas')?.focus({preventScroll:true});}
            }
        };
        const updateCamera=state=>{
            if(!cameraPanel)return;
            cameraPanel.hidden=!cove||!state.player||Boolean(state.workshop?.open);
            const camera=state.characterCamera;
            cameraAvailable=!cameraPanel.hidden&&state.active&&state.ready&&!state.failed&&!pending
                &&state.session?.admissionOpen!==false&&camera?.available===true;
            if(!cameraAvailable)cameraPanel.open=false;
            const labels={320:`View: ${camera?.mode==='orbit'?'Orbit':'Chase'}`,
                322:`Frame load: ${camera?.frameLoad?'On':'Off'}`,323:`Reduced motion: ${camera?.reducedMotion?'On':'Off'}`};
            for(const button of cameraButtons){
                const action=Number(button.dataset.cameraAction);
                button.disabled=!cameraAvailable||(action===324&&!(Number.isFinite(camera?.distance)&&camera.distance>1.5))
                    ||(action===325&&!(Number.isFinite(camera?.distance)&&camera.distance<12));
                if(labels[action]&&button.textContent!==labels[action])button.textContent=labels[action];
                if(action===320||action===322||action===323)button.setAttribute('aria-pressed',String(
                    action===320?camera?.mode==='chase':action===322?Boolean(camera?.frameLoad):Boolean(camera?.reducedMotion)));
            }
        };
        const openCameraMenu=()=>{
            tick();
            if(stopped||!cameraAvailable)return false;
            const expanded=moreControls?.open,previousPage=coveMenu?.page();
            coveMenu?.reveal('settings');
            if(moreControls)moreControls.open=true;
            const opened=Boolean(controllerMenu?.openSection(cameraPanel));
            // A modal may already own focus. Do not expand the panel on a
            // refused handoff or change that owner's focus.
            if(!opened){if(moreControls)moreControls.open=Boolean(expanded);if(previousPage)coveMenu?.reveal(previousPage);}
            return opened;
        };
        const onWorkshop=()=>{if(!pending && workshopToggle && !workshopToggle.disabled && act(60))tick();};
        const available=(button,owner)=>Boolean(button&&!button.hidden&&!button.disabled&&(!owner||!owner.hidden));
        const updateObjective=state=>{
            if(!objectivePanel||!objectiveButton)return;
            objectiveAction=null;
            objectivePanel.hidden=!cove||Boolean(state.workshop?.open);
            if(objectivePanel.hidden){objectiveButton.hidden=true;objectiveButton.disabled=true;return;}
            const show=(step,title,detail,target=null,owner=null,drawer=null)=>{
                objectivePanel.dataset.step=step;
                // Keep the live region quiet while the objective is unchanged.
                if(objectiveTitle.textContent!==title)objectiveTitle.textContent=title;
                if(objectiveDetail.textContent!==detail)objectiveDetail.textContent=detail;
                // These are the existing controls' exact permissions after tick
                // updates them. Guidance never calculates gameplay eligibility.
                const enabled=available(target,owner);
                // Commit the final state once: transient disable/hide on every
                // refresh would steal focus from a still-valid objective.
                objectiveButton.hidden=!enabled;objectiveButton.disabled=!enabled;
                if(enabled){
                    objectiveAction={step,target,owner,drawer};
                    const label=drawer?'Open winch controls':target.textContent;
                    if(objectiveButton.textContent!==label)objectiveButton.textContent=label;
                    objectiveButton.hidden=false;objectiveButton.disabled=false;
                }
            };
            const job=state.job,tow=state.tow,lift=state.harbor;
            if(state.session?.admissionOpen===false&&pending!=='leave')
                return show('unavailable','Expedition unavailable','Check save status below. Reload to continue.');
            if(pending)return show('waiting','Please wait',pending==='leave'?'Leaving the Cove…':pending==='rescue'?'Recovering your boat…':'Preparing the Cove…');
            if(state.practice?.active)return show('practice','Free boat test',state.practice.message||'No cost or rewards. Return restores your original boat and workshop.',state.practice.canReturn?document.getElementById('cove-practice-return'):null);
            if(state.rescue?.pending)return show('recovering','Recovering your boat','Progress stays paused until the recovery is saved.');
            if(lift?.pending)return show('powering','Saving harbor power','Wait for the harbor installation and save to finish.');
            if(job?.pending)return show('securing','Updating your recovery','Wait for the current job action to finish.');
            if(job?.savePending)return show('saving','Saving your progress','Wait for the save to finish. Check Save expedition below for status or retry.');
            if(!state.ready||!state.active||state.busy||state.workshop?.pending)
                return show('waiting','Preparing the Cove','Your next action will appear when the game is ready.');
            if(pausePhase!=='running')return show('paused','Expedition paused',pausePhase==='paused'?'Resume when you are ready.':'Finishing the current movement. Please wait.');
            if(job?.phase==='available')return show('accept','Recover the generator','Your first job is the sunken generator beside the boat.',jobButtons[0],jobPanel);
            if(job?.phase==='completed'){
                if(!job.durable)return show('saving','Confirming your delivery','Wait for the saved delivery to be restored.');
                if(!lift)return show('delivered','Delivery saved','The generator is safely delivered. Build or explore when you are ready.');
                if(lift?.installed)return lift.durable
                    ?show('powered','Harbor powered','Generator delivered and harbor power saved.')
                    :show('powering','Confirming harbor power','Wait for the saved harbor installation to be ready.');
                if(available(harborButtons[0],harborPanel))return show('power','Power the harbor','Your delivery is saved. Power the lift from the dock.',harborButtons[0],harborPanel);
                const step=state.player?.interaction;
                return show('dock-'+step,'Return to dock controls','Step off the boat and walk to the dock to power the harbor.',
                    step==='dock'||step==='leave-helm'?interact:null);
            }
            if(job?.phase!=='accepted')return show('explore','Build and explore','Prepare your boat in the workshop, then explore the Cove.');
            // Delivery can be valid without a tow, winch or player on the boat.
            if(available(jobButtons[1],jobPanel))return show('deliver','Deliver the generator','The harbor can accept your load now.',jobButtons[1],jobPanel);
            if(tow?.hasWinch===false)return show('winch','Prepare a winch','Fit or enable a winch in the workshop.',workshopToggle);
            if(!state.player?.onBoat)return show('board','Board your boat','Follow the teal dock lane to the orange boarding pad.',
                state.player?.interaction==='board'?interact:null);
            if(!tow?.confirmed)return show('waiting-tow','Waiting for the winch','Let the current winch command finish.');
            if(tow.attached)return show('return','Bring the generator home','Tow it to the harbor, lift it, then let it settle.',fieldTools?towButtons[1]:null,towPanel,fieldTools);
            if(available(towButtons[0],towPanel))return show('hook','Hook the generator','The generator is within reach of your winch.',towButtons[0],towPanel);
            return show('approach','Reach the generator',tow.operable?'Use the helm to bring the generator within winch range.':'Move onto the boat section with the winch.',
                state.player?.interaction==='helm'?interact:null);
        };
        const onObjective=()=>{
            const requested=objectiveAction;
            if(stopped||!requested||objectiveButton.disabled)return;
            tick(); // Revalidate before forwarding a possibly stale card click.
            const current=objectiveAction;
            if(stopped||objectivePanel.hidden||!current||current.step!==requested.step
                ||current.target!==requested.target||!available(current.target,current.owner))return;
            if(current.drawer){
                coveMenu?.reveal('tools');
                if(moreControls)moreControls.open=true;
                current.drawer.open=true;current.target.focus();
            }
            else current.target.click(); // Exactly the established handler, once.
        };
        const tick = () => {
            if (stopped) return;
            let state;
            try {
                state = JSON.parse(engine.UTF8ToString(engine._voxy_get_salvage_preview_json()));
            } catch { fail(); return; }
            const workshopOpen=Boolean(state.workshop?.open);
            if(cove&&moreControls&&workshopOpen!==wasWorkshop)moreControls.open=false;
            wasWorkshop=workshopOpen;
            pausePhase=state.pause?.phase||'running';
            const paused=pausePhase!=='running';
            if(pause) {
                pause.hidden=!state.pause;
                pause.disabled=!state.ready || Boolean(pending) || Boolean(state.job?.savePending)||Boolean(state.harbor?.pending)||Boolean(state.rescue?.pending)
                    || (pausePhase==='running'?!state.pause?.canPause:pausePhase!=='paused');
                pause.textContent=pausePhase==='paused'?'Resume · P':paused?'Pausing…':'Pause · P';
                pause.setAttribute('aria-pressed',String(paused));
            }
            designLibrary?.tick(state);
            expeditionSaves?.tick(state);
            updateCamera(state);
            controllerMenu?.tick(state);
            if (state.failed) { fail(); return; }
            if (state.assetFixture && lodPanel) {
                lodPanel.hidden = cove;
                document.getElementById('salvage-title').textContent = state.workshop?.open ? 'Build your boat' : cove ? 'Salvage Cove' : state.assetFixture.assembly
                    ? (state.assetFixture.prototypeUploads ? 'Pontoon assembly check' : 'Assembly inspection') : 'Model inspection';
                document.getElementById('salvage-help').textContent = state.workshop?.open
                    ? '1 / 2 / 3: choose a brick · click to build · R: rotate · Esc: select · U: undo · Enter: launch' : state.player
                    ? 'WASD to walk · click to look · Space to jump · E to interact · R to return'
                    : 'WASD to fly · click to look · E / Q up and down';
                for (const button of lodButtons) {
                    button.disabled = !state.ready || Boolean(pending)
                        || !state.assetFixture.availableLods?.includes(button.dataset.salvageLod);
                    button.setAttribute('aria-pressed', String(button.dataset.salvageLod === state.assetFixture.forcedLod));
                }
            }
            if (state.assetFixture && guidePanel) {
                guidePanel.hidden = cove;
                for (const button of guideButtons) {
                    button.disabled = !state.ready || Boolean(pending);
                    button.setAttribute('aria-pressed', String(Number(button.dataset.salvageGuide) === state.assetFixture.guides));
                }
                document.getElementById('salvage-guide-note').textContent = state.assetFixture.guides === 1
                    ? 'See-through rulers. White marks: 1 m along length, 0.32 m up. Amber: model bounds. Red: right · green: up · blue: forward.'
                    : state.assetFixture.guides === 2
                    ? (state.assetFixture.assembly ? 'Connected socket X-ray. ' : 'Socket X-ray. ') + 'Green: outward · red: key direction · blue: third axis. Amber: required clearance. Clean view shows actual surfaces.'
                    : 'G switches guides. Clean view shows the actual joins.';
            }
            if (interact) {
                const actions = {board: 'Board boat · E', dock: 'Return to dock · E',
                    helm: 'Use helm · E', 'leave-helm': 'Leave helm · E'};
                interact.hidden = !state.player || Boolean(state.workshop?.open);
                interact.disabled = paused || !state.ready || Boolean(pending) || !actions[state.player?.interaction];
                interact.textContent = actions[state.player?.interaction] || 'Move closer to board';
            }
            if(cutter) {
                cutter.hidden=!state.cutter||Boolean(state.workshop?.open);
                cutter.disabled=paused||!state.ready||Boolean(pending)||!state.cutter?.canCut;
                cutter.textContent=state.cutter?.pending?'Preparing cut…':state.cutter?.label
                    ?`Cut weld: ${state.cutter.label} · C`:'Move near a weld to cut · C';
                cutter.title='Cuts the named nearby connection. Separate sections stay owned. Rescue brings them home; the workshop can rebuild them.';
            }
            if(jobPanel) {
                jobPanel.hidden=!state.job || Boolean(state.workshop?.open);
                if(state.job) {
                    const job=state.job,enabled=state.ready&&!paused&&!pending&&!job.pending&&!job.savePending;
                    if(jobButtons[0]) {jobButtons[0].hidden=job.phase!=='available';jobButtons[0].disabled=!enabled;}
                    if(jobButtons[1]) {jobButtons[1].hidden=job.phase!=='accepted';jobButtons[1].disabled=!enabled||!job.canDeliver;}
                    jobStatus.textContent=job.pending ? 'Securing the harbor hand-off…'
                        : job.savePending ? (state.rescue?.pending?'Saving your recovered boat…':state.harbor?.pending?'Saving the powered harbor lift…':'Secured. Saving the delivery…')
                        : job.phase==='completed' && job.durable ? 'Generator delivered and saved. +60 salvage material.'
                        : job.phase==='completed' ? 'Restoring the saved delivery…'
                        : job.phase==='available' ? 'First job: recover the sunken generator beside the boat.'
                        : job.canDeliver ? 'Ready for the harbor. Press H to deliver.'
                        : `Hook and lift the generator. Return near the dock and slow down. Harbor: ${job.harborDistance.toFixed(1)} m.`;
                }
            }
            if(harborPanel){
                const lift=state.harbor;
                harborPanel.hidden=!lift||state.job?.phase!=='completed'||Boolean(state.workshop?.open);
                if(lift){
                    harborAwaiting=Boolean(lift.attachmentPending);
                    const enabled=state.ready&&!paused&&!pending&&!lift.pending;
                    const permissions=[lift.canInstall,lift.canAttach,lift.canOperate,lift.canOperate,lift.motor!==0||lift.attachmentPending,lift.canRelease];
                    harborButtons.forEach((button,index)=>{
                        if(!button)return;
                        button.hidden=index===0?lift.installed:!lift.installed;
                        button.disabled=!enabled||!permissions[index];
                    });
                    harborStatus.textContent=lift.pending?'Saving the powered harbor lift…'
                        :!lift.installed?(lift.message||'Generator recovered. Power the harbor lift from the dock.')
                        :!lift.durable?'Restoring the harbor lift…'
                        :lift.brokenMask?'A lift cable broke. Release the rig before trying again.'
                        :!lift.compatible?lift.rigIssue
                        :!lift.atDock?'Return to the dock to use the harbor lift.'
                        :lift.attachmentPending?'Waiting for safe alignment. Stop cancels the request.'
                        :lift.attached?`Lift attached. ${lift.motor>0?'Raising.':lift.motor<0?'Lowering.':'Holding.'} Cables: ${lift.lengths.map(n=>n.toFixed(1)).join(' / ')} m.`
                        :lift.message?.startsWith('Attachment')?lift.message
                        :lift.attachmentIssue||lift.message||'Lift powered. Park your boat in the frame, then attach the lift.';
                }
            }
            if(towPanel) {
                const tow=state.tow;
                towPanel.hidden=!tow || Boolean(state.workshop?.open);
                if(tow) {
                    const ready=state.ready && !paused && !pending && tow.operable && tow.confirmed;
                    towButtons.forEach((button,index) => {
                        if(!button) return;
                        button.disabled=!ready || (index===0 ? !tow.attached && !tow.inRange : !tow.attached);
                    });
                    if(towButtons[0]) towButtons[0].textContent=tow.attached ? 'Release tow · F' : 'Hook salvage · F';
                    towStatus.textContent=`${tow.name} · ${tow.massKg} kg · ${tow.distance.toFixed(1)} m. `
                        + (tow.attached && Number.isFinite(tow.ropeLength) ? `Cable ${tow.ropeLength.toFixed(1)} m. ` : '')
                        + (tow.hasWinch===false ? 'Fit or enable a winch in the workshop.' : !tow.confirmed ? 'Applying winch control…' : tow.broken ? 'Cable broke. Move closer to hook again.'
                        : tow.attached ? (tow.motor>0 ? 'Reeling in.' : tow.motor<0 ? 'Paying out.' : 'Cable attached. Sail to tow the load.')
                        : !tow.operable ? 'Board the boat to use its winch.' : tow.inRange ? 'In reach. Press F to hook it.'
                        : 'Sail closer. Hook within 8 metres.');
                }
            }
            if(workshopToggle) {
                const w=state.workshop,open=Boolean(w?.open);
                brickToolActive=Boolean(open&&w.brickTool);
                workshopToggle.hidden=!w;
                panel.dataset.workshop=String(open);
                workshopToggle.disabled=paused||!state.ready||Boolean(pending)||Boolean(state.job?.pending)||Boolean(w?.pending)||(!open&&!w?.canOpen);
                workshopToggle.textContent=open?'Return to dock · B':'Workshop · B';
                if(workshopPanel)workshopPanel.hidden=!open;
                if(open) {
                    document.getElementById('salvage-workshop-part').textContent=w.brickTool
                        ? `${w.placedBricks||0} bricks placed · ${w.placedParts} boat parts`
                        : w.selectedCount>1?`${w.selectedCount} parts selected · Primary: ${w.name} · ${w.parts} parts in design`
                        : `${w.name} · Part ${w.selected+1} · ${w.parts} parts in design`;
                    const tool=document.getElementById('salvage-workshop-tool');
                    if(tool)tool.textContent=w.brickTool
                        ? `Building with ${w.catalogName.replace('Brick ','')}. Click to place another. R rotates; Esc selects. The preview is not charged.`
                        : 'Choose a brick once. Click to build with it again and again.';
                    const paintName=document.getElementById('workshop-paint-name');
                    if(paintName)paintName.textContent=w.canPaint?`Paint · ${w.paintName||'Custom'}`:'Brick paint';
                    const paintNote=document.getElementById('workshop-paint-note');
                    if(paintNote)paintNote.textContent=!w.canPaint?'Paint is free. Choose a brick to paint.':w.brickTool
                        ?(Array.isArray(w.brushPaint)?'Paint is free. Next bricks use this color.':'Paint is free. Choose a color for new bricks.')
                        :w.selectedCount>1?'Paint is free. Colors apply to the selected bricks; then Keep.':'Paint is free. Choose a color, then Keep.';
                    const note=document.getElementById('salvage-workshop-status');
                    const noTarget=w.valid&&w.pointerPlacement&&w.pointerTarget===false;
                    note.textContent=(w.launchMessage||(noTarget?(w.brickTool?'Point at a matching stud or socket.':'Point at a part to place, or Keep this position.'):w.message))
                        +(w.valid?` Mass: ${w.massKg.toFixed(0)} kg.`:'');
                    note.dataset.valid=String(w.valid);
                    note.dataset.problem=w.problemCode||'';
                    const selectionNote=document.getElementById('workshop-selection-note');
                    if(selectionNote)selectionNote.textContent=w.brickTool?'Stop the brick tool to select a group.'
                        :w.changed?'Keep or cancel this preview before changing the selection.'
                        :`${w.selectedCount||1} selected. Shift-click parts, or use Add next to selection. Group changes are kept and undone together.`;
                    const drawer=document.getElementById('workshop-catalog-name');
                    if(drawer)drawer.textContent=`${w.catalogName||'Parts'} · ${w.partCost||'0'} material${w.partMachinery!=='0'&&w.partMachinery?` + ${w.partMachinery} machinery`:''}`;
                    const stock=document.getElementById('salvage-workshop-stock');
                    if(stock) {
                        const charge=BigInt(w.charge||'0')-BigInt(w.refund||'0');
                        const machines=BigInt(w.machineryCharge||'0')-BigInt(w.machineryRefund||'0');
                        const terms=[charge>0n?`${charge} material`:charge<0n?`returns ${-charge} material`:'no material cost'];
                        if(machines!==0n)terms.push(machines>0n?`${machines} machinery`:`returns ${-machines} machinery`);
                        const recovered=document.getElementById('workshop-recovery-status');
                        if(recovered)recovered.textContent=w.recoveryDesigns?`Recovered design ${Number(w.recoverySelected||0)+1} of ${w.recoveryDesigns}. Load to edit or export.`:'Rebuild starter keeps up to four custom boat designs.';
                        stock.textContent=`Stock: ${w.materials||'0'} material · ${w.machinery||'0'} machinery · ${w.storedParts||0} stored parts. Launch: ${terms.join(', ')}.`
                            +(w.affordable===false?' More stock needed.':'');
                    }
                    const settings=w.settings||{};
                    const settingNote=document.getElementById('workshop-settings-note');
                    if(settingNote)settingNote.textContent=w.configurable
                        ? `${settings.enabled?'On':'Off'}${w.hasOutputLimit?` · Limit: ${settings.limitPercent}%`:''}${w.canReverse?` · ${settings.reversed?'Reversed':'Forward'}`:''}`
                        : 'No adjustable settings for this part yet.';
                    for(const button of workshopButtons) {
                        if(button.dataset.workshopBrick) {
                            const part=w.catalog?.find(p=>p.name===button.dataset.workshopBrick);
                            button.dataset.workshopAction=part?String(100+part.index):'-1';
                            button.hidden=!part;
                            button.setAttribute('aria-pressed',String(Boolean(w.pointerPlacement&&part?.index===w.catalogIndex)));
                            const label=button.querySelector?.('.brick-label');
                            if(label&&part)label.textContent=`${part.name.replace('Brick ','')} · ${part.cost} material`;
                        }
                        const action=button.dataset.workshopAction;
                        if(button.dataset.workshopPaint!==undefined)
                            button.setAttribute('aria-pressed',String(Boolean(w.canPaint&&Number.isInteger(w.paintIndex)
                                &&Number(button.dataset.workshopPaint)===w.paintIndex)));
                        if(action==='96')button.setAttribute('aria-pressed',String(!w.brickTool));
                        if(action==='85') { button.hidden=!w.configurable;button.textContent=`Turn ${settings.enabled?'off':'on'} · X`; }
                        if(action==='86') { button.hidden=!w.hasOutputLimit;button.textContent=`${w.name==='Helm'?'Steering':'Thrust'} limit · L`; }
                        if(action==='87') { button.hidden=!w.canReverse;button.textContent=`${settings.reversed?'Forward':'Reverse'} drive · N`; }
                    }
                    for(const button of workshopButtons)button.disabled=!state.ready||Boolean(pending)||Boolean(w.pending)||state.session?.admissionOpen===false
                        ||(button.dataset.workshopPaint!==undefined&&!w.canPaint)
                        ||(button.dataset.workshopAction==='71'&&(!w.valid||!w.changed))
                        ||(button.dataset.workshopAction==='72'&&!w.undo)
                        ||(button.dataset.workshopAction==='79'&&!w.canLaunch)
                        ||(button.dataset.workshopAction==='80'&&!w.canUndoLaunch)
                        ||(button.dataset.workshopAction==='81'&&!w.canRedoLaunch)
                        ||(button.dataset.workshopAction==='88'&&!w.canRebuild)
                        ||(['89','92','93'].includes(button.dataset.workshopAction)&&!w.canLoadRecovery)
                        ||(button.dataset.workshopAction==='94'&&!w.canRemoveRecovery)
                        ||(button.dataset.workshopAction==='96'&&!w.brickTool)
                        ||(button.dataset.workshopAction==='84'&&!w.canAdd&&!w.brickTool)
                        ||(button.dataset.workshopBrick&&!(w.canChooseBrick??w.canAdd))
                        ||(button.dataset.workshopBrick&&button.dataset.workshopAction==='-1')
                        ||(button.dataset.workshopAction==='85'&&!w.configurable)
                        ||(button.dataset.workshopAction==='86'&&!w.hasOutputLimit)
                        ||(button.dataset.workshopAction==='87'&&!w.canReverse);
                    for(const button of workshopButtons){
                        const action=Number(button.dataset.workshopAction);
                        if([300,301,302,303,304,305,309].includes(action))button.disabled||=Boolean(w.changed||w.brickTool);
                        if(action===301)button.disabled||=!(w.selectedCount>1);
                        if(action===306)button.disabled||=!(w.redoCount>0)||Boolean(w.changed||w.brickTool);
                        if([61,62].includes(action))button.disabled||=Boolean(w.changed&&!w.brickTool);
                    }
                }
                reset.hidden=open;
                const scope=document.getElementById('salvage-scope');if(scope)scope.hidden=open;
            }
            updateObjective(state);
            if (pending === 'leave' && state.active === false) {
                cleanup();
                // Navigation follows the C++ frame-boundary removal acknowledgement.
                environment.location.assign('?experience=lego-world');
                return;
            }
            if (engine._voxy_is_initialized?.() !== 1) { fail(); return; }
            hasRescue=Boolean(state.rescue);
            reset.textContent=hasRescue?'Rescue · R':'Reset · R';
            reset.title=hasRescue?'Return your boat and fitted parts to the dock. Undelivered cargo returns to its recovery site. Saves automatically.':'';
            resetCount = state.resets;
            if(pending==='rescue'&&state.rescue?.phase==='idle')pending=null;
            if (pending === 'reset' && state.ready && resetCount > requestedReset) pending = null;
            if(state.rescue?.pending)leave.disabled=true;
            if (pending) {coveMenu?.tick(state);return;}
            reset.disabled = paused || !state.ready || Boolean(state.job?.pending);
            leave.disabled = !state.active || Boolean(state.job?.savePending)||Boolean(state.harbor?.pending)||Boolean(state.rescue?.pending);
            status.textContent = state.rescue?.pending ? 'Recovering your boat. Resume unlocks after saving.' : state.harbor?.pending ? 'Installing the harbor lift. Resume unlocks after saving.' : state.job?.savePending ? 'Your haul is being saved. Resume unlocks after confirmation.'
                : paused ? (pausePhase==='paused'?'Expedition paused. Press P or Resume to continue.':'Finishing the current movement…')
                : state.workshop?.open ? (state.workshop.pending ? 'Preparing your boat…' : 'Choose a brick. Point, rotate, then click to place.') : state.player && !state.busy
                ? (state.player.mode === 'helm' ? 'W/S: throttle · A/D: steer · E: leave helm · R: rescue and save'
                    : state.player.mode === 'swimming' ? 'Swimming. Press R for rescue and save.'
                    : state.player.onBoat ? 'F hooks nearby salvage. Walk to the helm to sail.'
                    : 'Walk to the boat beside the dock, then press E to board.')
                : state.assetFixture
                ? (state.busy ? 'Preparing the scene…' : cove ? 'Board, hook the generator with F, then take the helm. R recovers boat and cargo.' : state.assetFixture.assembly
                    ? (state.assetFixture.prototypeUploads
                        ? 'Prototype crossbeams test connections. Their socket wells are not modeled.'
                        : 'Inspect the connected parts from every side.')
                    : 'Inspect the shape, sockets and surface.')
                : (state.busy ? 'Resetting the cove…' : 'Explore the dock and wreck.');
            coveMenu?.tick(state);
            if(coveMenu)updateObjective(state);
        };
        reset.addEventListener('click', onReset);
        pause?.addEventListener('click',onPause);
        leave.addEventListener('click', onLeave);
        interact?.addEventListener('click', onInteract);
        objectiveButton?.addEventListener('click',onObjective);
        cutter?.addEventListener('click',onCut);
        workshopToggle?.addEventListener('click',onWorkshop);
        panel.addEventListener('keydown',onWorkshopKey);
        cameraPanel?.addEventListener('keydown',onCameraKey);
        cameraPanel?.addEventListener('keyup',onCameraKey);
        if(cove)environment['voxyCoveCameraMenu']=openCameraMenu;
        for(const button of cameraButtons){
            const handler=()=>{
                tick(); // Permissions may have changed since the visible frame.
                if(!stopped&&cameraAvailable&&!button.disabled&&act(Number(button.dataset.cameraAction)))tick();
            };
            cameraHandlers.set(button,handler);button.addEventListener('click',handler);
        }
        for(const button of workshopButtons) {
            const handler=()=>{
                if(!pending&&!button.disabled&&act(Number(button.dataset.workshopAction))) {
                    tick();
                    // Selecting the brush hands rotation/undo back to the
                    // canvas immediately, before the first placement click.
                    if(button.dataset.workshopBrick||button.dataset.workshopAction==='96'
                        ||(button.dataset.workshopPaint!==undefined&&brickToolActive))
                        document.getElementById('voxy-canvas')?.focus({preventScroll:true});
                }
            };
            workshopHandlers.set(button,handler);button.addEventListener('click',handler);
        }
        for (const button of lodButtons) {
            const handler = () => { if (!button.disabled) act(10 + Number(button.dataset.salvageLod)); };
            lodHandlers.set(button, handler);
            button.addEventListener('click', handler);
        }
        for (const button of guideButtons) {
            const handler = () => { if (!button.disabled) act(20 + Number(button.dataset.salvageGuide)); };
            lodHandlers.set(button, handler);
            button.addEventListener('click', handler);
        }
        towButtons.forEach((button,index) => {
            if(!button) return;
            const handler=()=>{if(!pending && !button.disabled) act(40+index);};
            towHandlers.set(button,handler);button.addEventListener('click',handler);
        });
        jobButtons.forEach((button,index)=>{
            if(!button)return;
            const handler=()=>{if(!pending&&!button.disabled)act(50+index);};
            jobHandlers.set(button,handler);button.addEventListener('click',handler);
        });
        harborButtons.forEach((button,index)=>{
            if(!button)return;
            const listen=(event,handler)=>{button.addEventListener(event,handler);harborHandlers.push([button,event,handler]);};
            const start=()=>{if(!pending&&!button.disabled&&act(52+index)){if(index===2||index===3)harborHeld=true;if(index===1)harborAwaiting=true;}};
            if(index===2||index===3){
                listen('pointerdown',event=>{if(event.button!==undefined&&event.button!==0)return;event.preventDefault();start();});
                listen('keydown',event=>{if((event.key===' '||event.key==='Enter')&&!event.repeat){event.preventDefault();event.stopPropagation();start();}});
                listen('keyup',event=>{if(event.key===' '||event.key==='Enter'){event.preventDefault();stopHarbor();}});
                listen('blur',stopHarbor);
            }else listen('click',start);
        });
        environment.addEventListener('pointerup',stopHarbor);
        environment.addEventListener('pointercancel',stopHarbor);
        environment.addEventListener('blur',blurHarbor);
        environment.addEventListener('pagehide', cleanup);
        panel.hidden = false;
        timer = environment.setInterval(tick, 100);
        tick();
        return cleanup;
    }
    return { install };
});
