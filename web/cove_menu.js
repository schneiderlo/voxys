(function(root,factory){
    const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;else root.VoxyCoveMenu=api;
})(globalThis,function(){
    'use strict';
    // English is the installed catalog. Stable keys keep UI copy separate from
    // game actions and leave room for translated catalogs without save changes.
    const english=Object.freeze({
        'menu.open':'Menu & help','menu.title':'Cove menu','menu.back':'Back',
        'page.pause':'Overview','page.job':'Job','page.map':'Map','page.inventory':'Inventory',
        'page.settings':'Settings','page.help':'Help','page.tools':'Winch & lift',
        'pause.resume':'Resume expedition','save.now':'Save checkpoint','rescue.now':'Rescue boat',
        'map.title':'Around the Cove','map.note':'Observed locations. North is up; the coastline is not shown.',
        'inventory.title':'Your resources','inventory.note':'Blueprints save a design. Launch checks your owned parts and material cost.',
        'workshop.open':'Open workshop','tutorial.restart':'Restart tutorial','tutorial.hide':'Hide tutorial',
        'help.previous':'Previous help','help.next':'Next help',
    });
    const set=(element,value)=>{if(element&&element.textContent!==String(value))element.textContent=String(value);};
    const validPosition=p=>Array.isArray(p)&&p.length===3&&p.every(Number.isFinite);
    const read=(engine)=>JSON.parse(engine.UTF8ToString(engine._voxy_get_salvage_preview_json()));
    function install(engine,environment,controller,saves){
        const document=environment.document,panel=document.getElementById('salvage-preview');
        const more=document.getElementById('salvage-more-controls');
        const nav=document.getElementById('cove-menu-nav');
        if(!more||!nav)return undefined;
        const byId=id=>document.getElementById(id),buttons=Array.from(nav.querySelectorAll('[data-cove-page]'));
        const pages=Array.from(more.querySelectorAll('[data-cove-section]'));
        const forwards=Array.from(more.querySelectorAll('[data-cove-forward]'));
        if(panel.dataset?.scene!=='cove'){
            for(const page of pages)page.hidden=false;
            for(const id of ['workshop-test','workshop-test-note','cove-practice','cove-menu-nav','cove-menu-state','cove-checkpoint-state','cove-result','cove-page-map','cove-page-inventory',
                'cove-tutorial','cove-help-title','cove-help-steps','cove-help-prev','cove-help-next','cove-menu-back'])if(byId(id))byId(id).hidden=true;
            for(const button of forwards)button.hidden=true;
            return undefined;
        }
        const handlers=[],priorOpen=environment['voxyCoveMenu'];
        let stopped=false,current='pause',state=null,helpPage=0,lastPause=null,world=null,mapKey='';
        const on=(element,event,handler)=>{if(element){element.addEventListener(event,handler);handlers.push([element,event,handler]);}};
        for(const element of more.querySelectorAll('[data-cove-text]'))set(element,english[element.dataset.coveText]||element.textContent);
        const preferences=environment.VoxyCovePreferences?.install(engine,environment,byId('cove-preferences'),controller);
        const allowed=s=>Boolean(s?.active&&s.ready&&!s.failed&&s.session?.admissionOpen!==false);
        const show=page=>{
            if(!pages.some(element=>element.dataset.coveSection===page))return false;
            current=page;if(page==='settings')preferences?.refresh();
            for(const element of pages)element.hidden=element.dataset.coveSection!==page;
            for(const button of buttons)button.setAttribute('aria-pressed',String(button.dataset.covePage===page));
            // Existing disclosure permissions are still managed by Preview.
            if(page==='job'&&byId('salvage-job'))byId('salvage-job').open=true;
            if(page==='tools'&&byId('salvage-field-tools'))byId('salvage-field-tools').open=true;
            return true;
        };
        const close=()=>{more.open=false;controller?.release?.();};
        const open=(page='pause')=>{
            if(stopped)return false;
            try{state=read(engine);}catch{return false;}
            if(!allowed(state))return false;
            const target=page==='camera'?'settings':page;
            const previous=current,wasOpen=more.open;
            if(!show(target))return false;
            more.open=true;
            if(!controller?.openSection(more,back)){show(previous);more.open=wasOpen;return false;}
            buttons.find(button=>button.dataset.covePage===target)?.focus({preventScroll:true});
            tick(state);return true;
        };
        const act=action=>{
            if(stopped)return false;
            const next=read(engine);if(!allowed(next))return false;
            return engine._voxy_salvage_preview_action(action)===1;
        };
        const back=()=>{
            if(current!=='pause'){show('pause');buttons[0]?.focus({preventScroll:true});}
            else close();
        };
        on(byId('cove-menu-back'),'click',back);
        on(byId('cove-tutorial-open'),'click',()=>open('help'));
        on(more,'keydown',event=>{
            if(more.open&&!event.defaultPrevented&&!event.metaKey&&!event.target?.closest?.('#cove-preferences')
                &&!['INPUT','SELECT','TEXTAREA'].includes(event.target?.tagName)){
                const code=event.code||'';
                const key=/^Key[A-Z]$/.test(code)?code.charCodeAt(3):/^Digit[0-9]$/.test(code)?code.charCodeAt(5)
                    :/^F(?:[1-9]|1[0-2])$/.test(code)?289+Number(code.slice(1))
                    :({Space:32,Enter:257,Tab:258,Backspace:259,Insert:260,Delete:261,ArrowRight:262,ArrowLeft:263,ArrowDown:264,ArrowUp:265}[code]||0);
                const modifiers=(event.shiftKey?1:0)|(event.ctrlKey?2:0)|(event.altKey?4:0);
                try{const preferences=JSON.parse(engine.ccall('voxy_cove_preferences_action','string',['number','string'],[1,'']));
                    const binding=preferences.bindings?.find(row=>['pause','save'].includes(row.action)&&key!==0
                        &&((row.key===key&&row.modifiers===modifiers)||(row.alternate===key&&row.alternateModifiers===modifiers)));
                    if(binding){event.preventDefault();event.stopPropagation();if(event.repeat)return;
                        if(binding.action==='save')environment['voxyCoveSave']?.();
                        else {const currentState=read(engine),resume=currentState.pause?.phase==='paused';if(act(resume?91:90)&&resume)close();}
                        return;
                    }
                }catch{} // Core state remains authoritative if its getter is unavailable.
            }
            if(event.key==='Escape'){
                event.preventDefault();event.stopPropagation();back();
            }else if(more.open)event.stopPropagation();
        });
        on(more,'keyup',event=>{if(controller?.active?.())event.stopPropagation();});
        on(more,'toggle',()=>{
            if(stopped)return;
            if(more.open&&!controller?.active?.())controller?.openSection(more,back);
            else if(!more.open)controller?.release?.();
        });
        for(const button of buttons)on(button,'click',()=>{show(button.dataset.covePage);if(state)tick(state);});
        for(const button of forwards)on(button,'click',()=>{
            if(stopped||button.disabled)return;
            const target=byId(button.dataset.coveForward);
            // Use the existing button, whose handler rechecks the real action.
            if(target&&!target.hidden&&!target.disabled)target.click();
        });
        on(byId('workshop-test'),'click',()=>{const next=read(engine);if(next.practice?.canBegin)act(340);});
        on(byId('cove-practice-return'),'click',()=>{const next=read(engine);if(next.practice?.canReturn)act(341);});
        on(byId('cove-tutorial-action'),'click',()=>{
            if(stopped)return;
            const next=read(engine),card=next.ui?.tutorial;
            if(allowed(next)&&card?.enabled&&Number.isInteger(card.action)&&card.action>0)act(card.action);
        });
        on(byId('cove-tutorial-restart'),'click',()=>act(352));
        on(byId('cove-tutorial-toggle'),'click',()=>act(353));
        on(byId('cove-help-prev'),'click',()=>{helpPage=(helpPage+4)%5;if(state)updateHelp(state);});
        on(byId('cove-help-next'),'click',()=>{helpPage=(helpPage+1)%5;if(state)updateHelp(state);});
        function updateHelp(s){
            const binding=id=>{
                const row=s.ui?.actions?.find(action=>action.id===id);
                return row?(s.gamepad?.connected?row.padLabel:row.keyLabel)||row.label:id;
            };
            const cards=[
                ['Walk and board',[`Move: ${binding('move_forward')}, ${binding('move_back')}, ${binding('move_left')}, ${binding('move_right')}.`,
                    `Follow the teal dock lane. Use ${binding('interact')} when Board boat appears.`,`At the helm, use ${binding('interact')} to drive or leave it.`]],
                ['Build with bricks',['Open the workshop beside the dock. Clear the cargo cradle if you need deck space.',
                    'Choose a brick. A green preview fits; red explains what is blocked.','Keep your edit. Launch uses the shown material cost.']],
                ['Recover the generator',['Accept the recovery job. Board your boat and approach the generator.',
                    `Use ${binding('hook')} in range, then ${binding('reel_in')} to lift.`,
                    'Bring it to the harbor. Deliver only when the game shows it is ready.']],
                ['Keep your progress',['Pause, then choose Save checkpoint. Wait for the save confirmation.',
                    'Continue reopens your last saved expedition on this browser and address.','Leave does not save. Blueprint files store designs, not mission progress.']],
                ['Get unstuck and stay comfortable',['Rescue returns the boat and fitted parts. It saves and waits for Resume.',
                    'Settings offers larger text, contrast, camera comfort and remappable controls.',
                    'Back returns to the previous menu. Closing a menu does not resume a paused expedition.']],
            ];
            const [title,lines]=cards[helpPage];set(byId('cove-help-title'),`${helpPage+1} / ${cards.length} · ${title}`);
            const list=byId('cove-help-steps'),key=JSON.stringify(lines);
            if(list&&list.dataset.lines!==key){list.dataset.lines=key;list.replaceChildren();for(const line of lines){const item=document.createElement('li');item.textContent=line;list.append(item);}}
        }
        function updateMap(s){
            const rows=(Array.isArray(s.landmarks)?s.landmarks:[]).filter(row=>typeof row.id==='string'&&typeof row.label==='string'&&validPosition(row.position)).slice(0,48);
            if(validPosition(s.player?.feet)&&!rows.some(row=>row.id==='player'))rows.push({id:'player',label:'You',position:s.player.feet});
            const key=JSON.stringify(rows.map(row=>[row.id,row.label,...row.position.map(n=>Math.round(n*10)/10)]));
            if(key===mapKey)return;mapKey=key;
            const svg=byId('cove-map'),list=byId('cove-landmark-list');svg?.replaceChildren();list?.replaceChildren();
            if(!rows.length){const li=document.createElement('li');li.textContent='Locations are not available yet.';list?.append(li);return;}
            const lo=[Math.min(...rows.map(row=>row.position[0]))-2,Math.min(...rows.map(row=>row.position[2]))-2];
            const hi=[Math.max(...rows.map(row=>row.position[0]))+2,Math.max(...rows.map(row=>row.position[2]))+2];
            const scale=Math.min(280/(hi[0]-lo[0]),180/(hi[1]-lo[1]));
            rows.forEach((row,index)=>{
                const li=document.createElement('li'),distance=validPosition(s.player?.feet)?Math.hypot(row.position[0]-s.player.feet[0],row.position[2]-s.player.feet[2]):null;
                li.textContent=`${index+1}. ${row.label}${distance===null?'':` · ${distance.toFixed(1)} m`}`;list?.append(li);
                if(!svg||!document.createElementNS)return;
                const x=20+(row.position[0]-lo[0])*scale,y=20+(row.position[2]-lo[1])*scale;
                const dot=document.createElementNS('http://www.w3.org/2000/svg','circle');dot.setAttribute('cx',x);dot.setAttribute('cy',y);dot.setAttribute('r',row.id==='player'?7:5);dot.setAttribute('class',row.id==='player'?'cove-map-player':'cove-map-place');svg.append(dot);
                const label=document.createElementNS('http://www.w3.org/2000/svg','text');label.setAttribute('x',x+8);label.setAttribute('y',y+4);label.textContent=String(index+1);svg.append(label);
            });
        }
        function tick(s){
            if(stopped)return;state=s;
            const nextWorld=s.world||null,phase=s.pause?.phase||'running';
            const caption=byId('cove-event-caption');if(caption){const text=s.ui?.captionsEnabled===false?'':s.ui?.caption||'';set(caption,text);caption.hidden=!text;}
            if(!allowed(s)||(world!==null&&world!==nextWorld)){close();show('pause');}
            else if(lastPause==='paused'&&phase==='running'&&current==='pause')close();
            world=nextWorld;lastPause=phase;
            if(byId('cove-menu-resume'))byId('cove-menu-resume').hidden=!allowed(s)||phase!=='paused';
            set(byId('cove-menu-state'),!allowed(s)?'Expedition unavailable. Check the save status.':phase==='paused'?'Expedition paused. Resume when ready.':phase==='running'?'Expedition running. Pause to stop the world.':'Finishing movement before pause…');
            const practice=s.practice;
            if(byId('cove-practice'))byId('cove-practice').hidden=!practice?.active;
            set(byId('cove-practice-status'),practice?.message||'Test mode. No cost or rewards. Return restores your workshop.');
            if(byId('cove-practice-return'))byId('cove-practice-return').disabled=!allowed(s)||!practice?.canReturn;
            if(byId('workshop-test'))byId('workshop-test').disabled=!allowed(s)||!practice?.canBegin;
            if(practice?.active){
                for(const id of ['salvage-save','salvage-job-accept','salvage-job-deliver','salvage-harbor-install','salvage-harbor-attach','salvage-harbor-raise','salvage-harbor-lower','salvage-harbor-stop','salvage-harbor-release','salvage-cut','workshop-launch','salvage-workshop-toggle'])if(byId(id))byId(id).disabled=true;
                if(byId('salvage-reset')){set(byId('salvage-reset'),'Return from test');byId('salvage-reset').disabled=!practice.canReturn;}
            }
            const saving=saves?.status?.();
            set(byId('cove-checkpoint-state'),saving?.message||'Pause and save before leaving. Leaving does not save.');
            for(const button of forwards){const target=byId(button.dataset.coveForward);button.disabled=!allowed(s)||!target||target.hidden||target.disabled;
                if(button.dataset.coveForward==='salvage-pause')set(button,phase==='paused'?'Resume expedition':'Pause expedition');
                if(button.dataset.coveForward==='salvage-reset')set(button,practice?.active?'Return from test':'Rescue boat');}
            set(byId('cove-result'),s.job?.phase==='completed'?(s.job.durable?`Delivery saved. Material available: ${s.session?.inventory?.salvageMaterial??'—'}.`:'Delivery is awaiting save confirmation.'):'');
            const card=s.ui?.tutorial,tutorial=byId('cove-tutorial');
            if(tutorial)tutorial.hidden=!card;
            const tutorialVisible=s.ui?.tutorialsEnabled!==false;
            const guide=byId('cove-tutorial-open');if(guide){guide.hidden=!card||!tutorialVisible||Boolean(s.workshop?.open)||Boolean(card.complete);guide.disabled=!allowed(s);set(guide,`Guide ${card?.number??card?.step??1}/${card?.total??7} · ${card?.title||'Learn to play'}`);}
            for(const id of ['cove-tutorial-progress','cove-tutorial-title','cove-tutorial-text','cove-tutorial-action'])if(byId(id))byId(id).hidden=!tutorialVisible;
            if(card){
                set(byId('cove-tutorial-progress'),card.complete?'Tutorial complete':`Step ${card.number??card.step} / ${card.total}`);
                set(byId('cove-tutorial-title'),card.title);set(byId('cove-tutorial-text'),card.text);
                const action=byId('cove-tutorial-action');set(action,card.actionLabel||'Continue');
                action.hidden=!tutorialVisible||!Number.isInteger(card.action)||card.action<=0;action.disabled=!allowed(s)||!card.enabled;
                set(byId('cove-tutorial-toggle'),s.ui?.tutorialsEnabled===false?'Show tutorial':'Hide tutorial');
            }
            for(const id of ['cove-tutorial-restart','cove-tutorial-toggle'])if(byId(id))byId(id).disabled=!allowed(s);
            if(current==='map'&&more.open)updateMap(s);
            if(current==='help'&&more.open)updateHelp(s);
            const inventory=byId('cove-inventory');
            if(inventory&&current==='inventory'&&more.open){
                const values=[['Material',s.session?.inventory?.salvageMaterial],['Machinery',s.session?.inventory?.specialMachinery],['Fitted parts',s.boat?.parts],['Stored parts',s.workshop?.storedParts],['Boat mass',s.boat?.massKg===undefined?undefined:`${s.boat.massKg} kg`]];
                const key=JSON.stringify(values);if(inventory.dataset.values!==key){inventory.dataset.values=key;inventory.replaceChildren();for(const [name,value]of values){const dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=name;dd.textContent=value??'—';inventory.append(dt);inventory.append(dd);}}
            }
            if(s.ui?.actions?.length){
                const key=id=>{const row=s.ui.actions.find(info=>info.id===id);return (s.gamepad?.connected?row?.padLabel:row?.keyLabel)||row?.label||id;};
                set(byId('salvage-help'),s.workshop?.open
                    ?`Choose a brick, then Keep: ${key('keep')}. Rotate: ${key('rotate_y')}. Undo: ${key('undo')}. Launch: ${key('launch')}.`
                    :`Move: ${key('move_forward')} / ${key('move_back')}. Jump: ${key('jump')}. Interact: ${key('interact')}.`);
                if(!s.rescue?.pending&&!s.harbor?.pending&&!s.job?.savePending&&!s.busy&&!s.workshop?.open){
                    set(byId('salvage-status'),practice?.active?practice.message:phase==='paused'?'Expedition paused. Choose Resume when ready.':phase!=='running'?'Finishing the current movement…'
                        :s.player?.mode==='helm'?`Throttle: ${key('move_forward')} / ${key('move_back')}. Leave helm: ${key('interact')}.`
                        :s.player?.mode==='swimming'?'Swimming. Move toward the boat to reboard, or choose Rescue.'
                        :s.player?.onBoat?`Walk to the helm to sail. Hook nearby salvage: ${key('hook')}.`
                        :`Follow the dock lane. Board when prompted: ${key('interact')}.`);
                }
                set(byId('salvage-controller-help'),s.workshop?.open
                    ?`Tools: ${key('tools_menu')}. Keep: ${key('keep')}. Back cancels or stops. Right stick orbits; hold R3 to pan, L3 to zoom.`
                    :`Workshop: ${key('workshop')}. Pause menu: ${key('pause')}. In menus, arrows navigate, Confirm chooses and Back returns.`);
            }
            preferences?.tick(s);
            const labels={ 'salvage-pause':'pause','salvage-reset':'rescue','salvage-save':'save','salvage-workshop-toggle':'workshop',
                'salvage-interact':'interact','salvage-hook':'hook','salvage-reel':'reel_in','salvage-payout':'pay_out',
                'salvage-job-accept':'accept_job','salvage-job-deliver':'deliver','salvage-harbor-install':'power_harbor'};
            Object.assign(labels,{'workshop-next':'next_part','workshop-prev':'previous_part','workshop-rotate':'rotate_y','workshop-remove':'remove','workshop-keep':'keep','workshop-undo':'undo','workshop-redo':'redo','workshop-launch':'launch','workshop-rotate-x':'rotate_x','workshop-rotate-z':'rotate_z'});
            for(const [id,action]of Object.entries(labels)){
                const target=byId(id),info=s.ui?.actions?.find(row=>row.id===action);
                if(!target||!info)continue;const key=s.gamepad?.connected?info.padLabel:info.keyLabel;
                const base=target.textContent.split(' · ')[0];set(target,base+(key&&key!=='Unbound'?` · ${key}`:''));
            }

        }
        show('pause');environment['voxyCoveMenu']=open;
        return {tick,reveal:show,page:()=>current,open,close,cleanup(){
            if(stopped)return;close();stopped=true;preferences?.cleanup();
            for(const [element,event,handler]of handlers)element.removeEventListener(event,handler);
            if(environment['voxyCoveMenu']===open)environment['voxyCoveMenu']=priorOpen;
        }};
    }
    function installLanding(environment=globalThis){
        const document=environment.document,panel=document.getElementById('cove-landing');if(!panel)return undefined;
        const canvas=document.getElementById('voxy-canvas'),help=document.getElementById('cove-landing-instructions');
        // Title accessibility is presentation only. Full settings still pass
        // through the core validator when an expedition starts.
        try{const text=environment.localStorage?.getItem(environment.VoxyCovePreferences?.storageKey||'voxys.cove.input-preferences.v1');
            if(text&&text.length<=32768){const prefs=JSON.parse(text);
                if(prefs.version===1&&[1,1.25,1.5].includes(prefs.textScale)&&typeof prefs.highContrast==='boolean'){
                    const target=document.documentElement||document.body;
                    target.style?.setProperty?.('--cove-ui-scale',String(prefs.textScale));
                    target.setAttribute('data-cove-contrast',String(prefs.highContrast));
                }
            }
        }catch{} // Missing, damaged or blocked optional metadata keeps defaults.

        const priorInput=environment['voxyCoveLandingMenuInput'],priorActive=environment['voxyCoveLandingMenuActive'];
        const handlers=[];let stopped=false,owned=false;
        const focused=()=>!document.hidden&&document.hasFocus?.()!==false;
        const choices=()=>Array.from(panel.querySelectorAll('button,summary,a[href]')).filter(element=>environment.VoxyControllerMenu.visible(element,panel,environment));
        const focus=element=>element?.focus({preventScroll:true});
        const close=()=>{panel.hidden=true;help.hidden=true;owned=false;focus(canvas);};
        const on=(id,handler)=>{const element=document.getElementById(id);element?.addEventListener('click',handler);if(element)handlers.push([element,handler]);};
        const opener=document.getElementById('cove-landing-open');if(opener)opener.hidden=false;
        on('cove-landing-open',()=>{panel.hidden=false;owned=true;focus(choices()[0]);});
        on('cove-landing-close',close);
        on('cove-landing-help',()=>{help.hidden=false;focus(document.getElementById('cove-landing-back'));});
        on('cove-landing-back',()=>{help.hidden=true;focus(document.getElementById('cove-landing-help'));});
        const active=()=>!stopped&&!panel.hidden&&focused();
        const input=events=>{
            if(stopped||!focused())return false;
            if(events.menu){if(panel.hidden){panel.hidden=false;owned=true;focus(choices()[0]);}else close();return true;}
            if(panel.hidden)return false;
            if(!owned){if(Object.values(events).some(Boolean)){owned=true;focus(choices()[0]);}return true;}
            if(events.back){back();return true;}
            const list=choices(),at=list.indexOf(document.activeElement);
            if(events.up||events.left||events.down||events.right){focus(list[(Math.max(at,0)+list.length+((events.up||events.left)?-1:1))%list.length]);return true;}
            if(events.confirm&&list.includes(document.activeElement))document.activeElement.click();
            return true;
        };
        const back=()=>{
            if(!help.hidden){help.hidden=true;focus(document.getElementById('cove-landing-help'));return;}
            const drawer=document.activeElement?.closest?.('details[open]');
            if(drawer&&panel.contains(drawer)){drawer.open=false;focus(drawer.querySelector('summary'));return;}
            close();
        };
        const blur=()=>{owned=false;};
        const hidden=()=>{if(document.hidden)blur();};
        const key=event=>{if(!panel.hidden&&event.key==='Escape'){event.preventDefault();event.stopPropagation();back();}};
        panel.addEventListener('keydown',key);environment.addEventListener('blur',blur);document.addEventListener('visibilitychange',hidden);
        panel.hidden=false;owned=true;environment['voxyCoveLandingMenuInput']=input;environment['voxyCoveLandingMenuActive']=active;
        return {cleanup(){if(stopped)return;close();stopped=true;for(const [element,handler]of handlers)element.removeEventListener('click',handler);
            panel.removeEventListener('keydown',key);environment.removeEventListener('blur',blur);document.removeEventListener('visibilitychange',hidden);
            if(environment['voxyCoveLandingMenuInput']===input)environment['voxyCoveLandingMenuInput']=priorInput;
            if(environment['voxyCoveLandingMenuActive']===active)environment['voxyCoveLandingMenuActive']=priorActive;
        }};
    }
    return {install,installLanding,english};
});
