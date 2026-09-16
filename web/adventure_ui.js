(function(root,factory){
    const api=factory();
    if(typeof module==='object'&&module.exports)module.exports=api;
    else root.VoxyAdventureUI=api;
})(globalThis,function(){
    'use strict';
    const pieces=Object.freeze(['Foundation','Floor','Wall','Open doorway','Flat roof','Stairs','Beam',
        'Brick 1 × 2','Brick 2 × 2','Brick 2 × 4','Bed','Chest','Workbench','Pier','Hinged door']);
    const maximumStateCharacters=512*1024;
    const settingsModes=new Set(['settings','controls','combat-binding','binding-choice']);
    const modes=new Set(['explore','build','catalog','dialogue','workbench','chest','journal','pause','bag','guide',...settingsModes]);
    const clipped=(v,n=480)=>typeof v==='string'?v.slice(0,n):'';
    const text=(el,v)=>{const next=String(v??'');if(el.textContent!==next)el.textContent=next;};
    const ownsCompass=v=>v?.kind===5&&v?.quantity===1;
    // Missing metadata keeps compatibility with the original room-only host.
    // An explicit new ID must describe the installed preview, never guess a
    // room from its label or invent an unlock in the browser.
    const blueprintKind=(value,piece)=>value===undefined?(piece===0?1:0)
        :Number.isInteger(value)&&value>=0&&value<=2?value:null;
    const matchingBlueprint=(kind,piece)=>kind===0?piece!==0:kind===1?piece===0:kind===2?piece===14:false;
    const visible=el=>{
        for(let node=el;node;node=node.parentElement){
            if(node.hidden)return false;
            const parent=node.parentElement;
            if(parent?.tagName==='DETAILS'&&!parent.open&&node.tagName!=='SUMMARY')return false;
        }
        return true;
    };
    function install(engine,environment=globalThis){
        const document=environment.document;
        if(!document||typeof engine?._adventure_action!=='function'||typeof engine?._get_adventure_state_json!=='function')return undefined;
        const handlers=[];let rowHandlers=[],state=null,ready=false,stopped=false,uiOwned=false,attached=false;
        let rowsKey='',priorMode='',pending=null,pendingCombat=null,pendingInteraction=null,publishedSelection='',programmaticFocus=false;
        let preferences=null,preferencesInstalled=false;
        const on=(el,type,fn)=>{el.addEventListener(type,fn);handlers.push([el,type,fn]);};
        const add=(parent,tag,label,attrs={})=>{
            const el=document.createElement(tag);if(label!==null)el.textContent=label;
            for(const[k,v]of Object.entries(attrs))el.setAttribute(k,String(v));parent.append(el);return el;
        };
        const hadClass=document.body.classList?.contains('voxy-adventure')===true;
        document.body.classList?.add('voxy-adventure');
        const panel=add(document.body,'aside',null,{id:'adventure-ui','data-voxy-ui':'true','aria-label':'Adventure controls'});
        // Optional DOM observation for local, ordinary-input gameplay checks.
        // This inert node mirrors the UI's bounded read; it accepts no commands.
        let observationNode=null;
        try{
            const url=new URL(environment.location?.href);
            if(['http:','https:'].includes(url.protocol)&&['localhost','127.0.0.1','[::1]'].includes(url.hostname)
                &&url.searchParams.get('adventureObserve')==='1')
                observationNode=add(panel,'script','null',{id:'adventure-observation',type:'application/json'});
        }catch{}
        // The shipping WASM export is void: this acknowledges dispatch only.
        // Accepted gameplay state always comes from the next runtime snapshot.
        const dispatch=(action,value)=>{const result=engine._adventure_action(action,value);return result===undefined||result===1;};
        const ownInput=owned=>{
            if(stopped||uiOwned===owned)return;uiOwned=owned;
            try{engine._adventure_action(15,owned?1:0);}catch{}
        };
        const returnToWorld=()=>{document.getElementById('voxy-canvas')?.focus?.({preventScroll:true});ownInput(false);};
        const focusPublished=b=>{
            if(!b||document.activeElement===b)return;
            programmaticFocus=true;
            try{b.focus({preventScroll:true});}finally{programmaticFocus=false;}
            b.scrollIntoView?.({block:'nearest',inline:'nearest'});
        };
        const read=()=>{
            const ptr=engine._get_adventure_state_json();if(!ptr)throw Error();
            const raw=engine.UTF8ToString(ptr);if(typeof raw!=='string'||raw.length>maximumStateCharacters)throw Error();
            const value=JSON.parse(raw);
            if(!value||typeof value!=='object'||Array.isArray(value)||typeof value.build!=='boolean'
                ||!Number.isInteger(value.piece)||value.piece<0||value.piece>pieces.length||!modes.has(value.mode))throw Error();
            if(!matchingBlueprint(blueprintKind(value.blueprintKind,value.piece),value.piece))throw Error();
            return {value,raw};
        };
        const act=(action,value=0)=>{
            if(stopped||!ready)return false;
            try{const queued=dispatch(action,value);refresh();return queued;}
            catch{text(status,'That action is unavailable. Try again.');return false;}
        };
        const button=(parent,label,id,action,value=0)=>{
            const b=add(parent,'button',label,{type:'button',id});
            on(b,'click',event=>{
                if(b.disabled||!visible(b))return;
                if(action===7){
                    if(event.detail>1||pendingInteraction!==null)return;
                    pendingInteraction=observation(state);
                    try{if(!act(action,value)){pendingInteraction=null;refresh();}}finally{returnToWorld();}
                    return;
                }
                try{act(action,value);}finally{if(action===18)returnToWorld();}
            });return b;
        };
        const guideButton=(parent,label,id,topic)=>{
            const b=add(parent,'button',label,{type:'button',id});
            on(b,'click',event=>{
                if(event.detail>1||b.disabled||!visible(b)||pending!==null)return;
                pending=observation(state);
                if(!act(29,topic)){pending=null;refresh();}
            });return b;
        };
        const quest=button(panel,'','adventure-quest',16);
        add(quest,'span','Your adventure',{class:'adventure-eyebrow'});
        const objective=add(quest,'strong','',{id:'adventure-objective'});
        const menu=button(panel,'Menu','adventure-menu',9);
        const compass=add(panel,'section',null,{id:'adventure-compass','aria-label':'Trail compass'});
        const compassArrow=add(compass,'span','↑',{id:'adventure-compass-arrow','aria-hidden':'true'});
        const compassReading=add(compass,'span','',{id:'adventure-compass-reading'});
        const compassTarget=button(compass,'Next destination','adventure-compass-target',18);
        const status=add(panel,'p','Preparing your adventure…',{id:'adventure-status',role:'status','aria-live':'polite'});
        const playbar=add(panel,'nav',null,{id:'adventure-playbar','aria-label':'Play actions'});
        // Keep text and actions in one wrapping dock so larger text and a
        // longer danger cue cannot overlap a separately positioned status.
        playbar.append(status);
        const combat=add(playbar,'section',null,{id:'adventure-combat','aria-label':'Health and combat'});
        const vitality=add(combat,'div',null,{class:'adventure-vitality'});
        const healthText=add(vitality,'strong','',{id:'adventure-health'});
        const healthMeter=add(vitality,'meter',null,{id:'adventure-health-meter',min:0,max:100,low:30,high:70,optimum:100,'aria-label':'Health'});
        const threat=add(combat,'span','',{id:'adventure-threat'});
        const fighting=add(combat,'div',null,{class:'adventure-combat-actions'});
        const worldButton=(label,id,action)=>{
            const b=add(fighting,'button',label,{type:'button',id});
            on(b,'click',event=>{
                if(event.detail>1||b.disabled||!visible(b)||pendingCombat!==null)return;
                pendingCombat=observation(state);
                try{if(!act(action)){pendingCombat=null;refresh();}}finally{returnToWorld();}
            });return b;
        };
        const attack=worldButton('Attack','adventure-attack',27);
        const dodge=worldButton('Dodge','adventure-dodge',28);
        const revive=worldButton('Return home','adventure-revive',11);
        const explorationActions=add(playbar,'div',null,{id:'adventure-exploration-actions'});
        const interact=button(explorationActions,'E · Use nearby','adventure-interact',7);
        button(explorationActions,'B · Build','adventure-build',21);
        const bag=button(explorationActions,'Bag','adventure-bag',24);
        const journal=button(explorationActions,'Journal','adventure-journal',16);
        const stock=add(panel,'p','',{id:'adventure-stock','aria-label':'Building supplies'});
        const builder=add(panel,'section',null,{id:'adventure-builder','aria-label':'Build freely'});
        const chosen=add(builder,'div',null,{class:'adventure-chosen'});
        const pieceImage=add(chosen,'img',null,{id:'adventure-piece-image',alt:'',width:64,height:64});
        const chosenText=add(chosen,'div',null);
        const selected=add(chosenText,'strong','',{id:'adventure-selected'});
        const blueprintCount=add(chosenText,'span','',{id:'adventure-blueprint-count',class:'adventure-eyebrow'});
        const cost=add(chosenText,'p','',{id:'adventure-cost'});
        const buildCount=add(chosenText,'p','',{id:'adventure-build-count',role:'status','aria-live':'polite'});
        const buildFeedback=add(builder,'p','',{id:'adventure-build-feedback'});
        const buildChoices=add(chosen,'div',null,{class:'adventure-build-choices'});
        const starter=button(buildChoices,'Starter room','adventure-starter',14);
        const starterContents=add(starter,'small','Bed, chest & bench',{class:'adventure-row-detail'});
        button(buildChoices,'Choose pieces','adventure-individual',13);
        const starterHint=add(builder,'p','After placing, use the bed to set your home.',{id:'adventure-starter-hint'});
        const placement=add(builder,'p','',{id:'adventure-placement',role:'status','aria-live':'polite'});
        const buildActions=add(builder,'div',null,{class:'adventure-actions'});
        const place=button(buildActions,'Place','adventure-place',4);place.className='adventure-primary';
        button(buildActions,'R · Rotate','adventure-rotate',3);
        button(buildActions,'Lower','adventure-lower',12,-1);button(buildActions,'Raise','adventure-raise',12,1);
        button(buildActions,'Remove','adventure-remove',5);button(buildActions,'Undo','adventure-undo',6);
        button(buildActions,'Done','adventure-done',22);
        const buildingHelp=guideButton(buildActions,'Building help','adventure-building-help',1);
        const sheet=add(panel,'section',null,{id:'adventure-menu-panel',role:'dialog','aria-labelledby':'adventure-menu-title'});
        const sheetHead=add(sheet,'div',null,{class:'adventure-sheet-heading'});
        const menuTitle=add(sheetHead,'h2','',{id:'adventure-menu-title'});
        button(sheetHead,'Close · Esc','adventure-close',20);
        const dialogueRole=add(sheet,'p','',{id:'adventure-dialogue-role',class:'adventure-eyebrow'});
        const menuText=add(sheet,'p','',{id:'adventure-dialogue-text'});
        const menuStatus=add(sheet,'p','',{id:'adventure-menu-status',role:'status','aria-live':'polite'});
        const preferencesStatus=add(sheet,'p','',{id:'adventure-preferences-status',role:'status','aria-live':'polite'});
        const categories=add(sheet,'nav',null,{id:'adventure-categories','aria-label':'Building categories'});
        const categoryButtons=['Structure','Bricks','Furniture'].map((label,i)=>button(categories,label,`adventure-category-${i}`,13,i+1));
        const menuRows=add(sheet,'div',null,{id:'adventure-menu-rows',class:'adventure-menu-rows'});
        const saveRow=add(sheet,'div',null,{id:'adventure-save-row'});
        const saveStatus=add(saveRow,'p','',{id:'adventure-save-status',role:'status','aria-live':'polite'});
        const more=add(sheet,'details',null,{id:'adventure-more'});add(more,'summary','Help & worlds');
        const howToPlay=guideButton(more,'How to play','adventure-how-to-play',0);
        const controlHelp=add(more,'p','',{id:'adventure-control-help'});
        button(more,'Return to a safe place','adventure-recover',11);
        const links=add(more,'nav',null,{'aria-label':'Adventure worlds'});
        add(links,'a','Continue saved adventure',{href:'?experience=adventure',id:'adventure-continue'});
        add(links,'a','Start a new adventure',{href:'?experience=adventure&new=1',id:'adventure-new'});
        add(links,'a','Cove & playground',{href:'?experience=lego-world',id:'adventure-prototypes'});
        const coveContinue=document.getElementById('cove-continue');
        const coveHome=coveContinue&&!coveContinue.hidden&&coveContinue.href?coveContinue.parentElement:null;
        if(coveHome){coveContinue.remove();links.append(coveContinue);}
        const observation=next=>`${String(next.menuToken)}:${String(next.observation)}`;
        const updateRows=next=>{
            const rows=(Array.isArray(next.rows)?next.rows.slice(0,64):[]).map((row,index)=>{
                const piece=Number.isInteger(row?.pieceKind)&&row.pieceKind>=0&&row.pieceKind<=pieces.length?row.pieceKind:null;
                const kind=blueprintKind(row?.blueprintKind,piece);
                return {index,detail:clipped(row?.detail,180),label:clipped(row?.label,180),
                    enabled:row?.enabled===true&&(kind===0||matchingBlueprint(kind,piece)),
                    intent:Number.isInteger(row?.intent)&&row.intent>0&&row.intent<=2147483647?row.intent:0,
                    pieceKind:piece,blueprintKind:kind};
            });
            const key=JSON.stringify([next.mode,next.menuToken,rows]);
            if(key!==rowsKey){
                rowsKey=key;
                const hadRowFocus=menuRows.contains(document.activeElement);
                for(const[b,event,fn]of rowHandlers)b.removeEventListener(event,fn);rowHandlers=[];menuRows.replaceChildren();
                for(const row of rows){
                    if(!row.label)continue;
                    const b=add(menuRows,'button',null,{type:'button','data-intent':row.intent,'data-enabled':row.enabled,'data-row':row.index});
                    const blueprint=next.mode==='catalog'&&(row.blueprintKind===1||row.blueprintKind===2);
                    if(blueprint){
                        b.className='adventure-blueprint';
                        add(b,'span',row.blueprintKind===1?'Room blueprint':'3-piece blueprint',{class:'adventure-eyebrow'});
                    }
                    if(row.pieceKind)add(b,'img',null,{alt:'',src:`adventure_piece_${String(row.pieceKind).padStart(2,'0')}.svg`,width:88,height:64});
                    add(b,'span',row.label);b.disabled=!row.enabled||!row.intent;
                    if(blueprint&&row.blueprintKind===1)add(b,'span','Bed, chest & bench',{class:'adventure-blueprint-contents'});
                    if(row.detail){add(b,'br',null);add(b,'small',row.detail,{class:'adventure-row-detail'});}
                    const token=next.menuToken,mode=next.mode;
                    const fn=event=>{
                        if(event.detail>1||b.disabled||!visible(b)||pending!==null||state?.menuToken!==token||state?.mode!==mode)return;
                        pending=observation(state);
                        if(!act(10,row.intent)){pending=null;refresh();}
                    };
                    const focused=()=>{
                        if(programmaticFocus||b.disabled||!visible(b)||pending!==null||state?.menuToken!==token||state?.mode!==mode
                            ||state?.menuSelected===row.index)return;
                        try{dispatch(26,row.intent);}catch{}
                    };
                    b.addEventListener('click',fn);rowHandlers.push([b,'click',fn]);
                    b.addEventListener('focus',focused);rowHandlers.push([b,'focus',focused]);
                }
                if(hadRowFocus){
                    const buttons=Array.from(menuRows.querySelectorAll('button'));
                    focusPublished(buttons.find(b=>Number(b.dataset.row)===next.menuSelected&&!b.disabled)||buttons.find(b=>!b.disabled));
                }
            }
            for(const b of menuRows.querySelectorAll('button'))b.disabled=!ready||pending!==null||b.dataset.enabled!=='true'||Number(b.dataset.intent)===0;
            const selection=`${next.menuToken}:${next.menuSelected}`;
            if(selection!==publishedSelection){
                const buttons=Array.from(menuRows.querySelectorAll('button'));
                for(const b of buttons)b.setAttribute('aria-current',String(Number(b.dataset.row)===next.menuSelected));
                if(!sheet.hidden)focusPublished(buttons.find(b=>Number(b.dataset.row)===next.menuSelected&&!b.disabled));
                publishedSelection=selection;
            }
        };
        const refresh=()=>{
            if(stopped)return;let next;
            try{
                const snapshot=read();next=snapshot.value;
                if(observationNode)text(observationNode,snapshot.raw);
            }catch{
                if(observationNode)text(observationNode,'null');
                ready=false;status.hidden=false;text(status,'Adventure status is unavailable.');compass.hidden=true;
                playbar.hidden=false;combat.hidden=true;explorationActions.hidden=true;
                builder.hidden=true;sheet.hidden=true;
                for(const b of panel.querySelectorAll('button'))b.disabled=true;return;
            }
            state=next;ready=next.ready!==false&&next.failed!==true;
            if(ready&&!preferencesInstalled){
                preferencesInstalled=true;
                try{preferences=environment.VoxyAdventurePreferences?.install(engine,environment)||null;}catch{}
            }
            try{preferences?.tick(next);}catch{}
            if(!attached){try{attached=dispatch(19,1);}catch{}}
            if(pending!==null&&pending!==observation(next))pending=null;
            if(pendingCombat!==null&&pendingCombat!==observation(next))pendingCombat=null;
            if(pendingInteraction!==null&&pendingInteraction!==observation(next))pendingInteraction=null;
            for(const b of panel.querySelectorAll('button'))b.disabled=!ready;
            buildingHelp.disabled=howToPlay.disabled=!ready||pending!==null;
            const creative=next.creative===true;
            const mode=next.mode,explore=mode==='explore',build=mode==='build',catalog=mode==='catalog';
            panel.setAttribute('aria-label',creative?'Building controls':'Adventure controls');
            panel.dataset.mode=mode;playbar.hidden=!explore;builder.hidden=!build;sheet.hidden=explore||build;
            combat.hidden=creative;
            bag.hidden=journal.hidden=creative;
            stock.hidden=creative||(!build&&!catalog);status.hidden=!explore;
            menu.hidden=!explore;quest.hidden=creative||!explore;
            text(status,clipped(next.status)||'Explore the landscape and choose a home site.');
            text(objective,clipped(next.objective)||clipped(next.quest?.objective)||clipped(next.guide)||'Explore the landscape. Build a home.');
            text(interact,`E · ${clipped(next.interaction,100)||'Use nearby'}`);
            const health=Number.isInteger(next.health)&&next.health>=0&&next.health<=100?next.health:null;
            const defeated=health===0;
            text(healthText,`Health ${health===null?'—':`${health} / 100`}`);
            healthMeter.hidden=health===null;healthMeter.setAttribute('value',health??0);
            combat.dataset.wounded=String(health!==null&&health<=30);
            text(threat,clipped(next.combatLabel,180));threat.hidden=!threat.textContent;
            attack.hidden=defeated;dodge.hidden=defeated;revive.hidden=!defeated;
            const attackControl=clipped(next.attackControl,120),dodgeControl=clipped(next.dodgeControl,120);
            text(attack,attackControl?`Attack · ${attackControl}`:'Attack');
            text(dodge,dodgeControl?`Dodge · ${dodgeControl}`:'Dodge');
            text(controlHelp,[clipped(next.lookControl,240)||'Choose look controls in Settings.',
                attackControl?`Attack: ${attackControl}.`:'Choose an attack control in Settings.',
                dodgeControl?`Dodge: ${dodgeControl}.`:'Choose a dodge control in Settings.'].join(' '));
            if(creative)text(controlHelp,clipped(next.lookControl,240)||'Choose look controls in Settings.');
            const continueLink=document.getElementById('adventure-continue'),newLink=document.getElementById('adventure-new');
            text(continueLink,creative?'Continue saved build':'Continue saved adventure');continueLink.setAttribute('href',creative?'?experience=build':'?experience=adventure');
            text(newLink,creative?'Start a new build':'Start a new adventure');newLink.setAttribute('href',creative?'?experience=build&new=1':'?experience=adventure&new=1');
            explorationActions.hidden=defeated;
            attack.disabled=!ready||!explore||health===null||defeated||next.staffEquipped!==true||next.attackReady!==true||pendingCombat!==null;
            dodge.disabled=!ready||!explore||health===null||defeated||next.dodgeReady!==true||pendingCombat!==null;
            revive.disabled=!ready||!explore||!defeated||pendingCombat!==null;
            interact.disabled=!ready||!explore||defeated||pendingInteraction!==null;
            attack.setAttribute('title',next.staffEquipped===true?'Strike with your Trail staff':'Craft a Trail staff at a workbench, then equip it in your bag');
            const count=v=>Number.isSafeInteger(v)&&v>=0?String(v):'—';
            text(stock,`Wood ${count(next.wood)} · Stone ${count(next.stone)} · Scrap ${count(next.scrap)}`);
            const blueprint=blueprintKind(next.blueprintKind,next.piece);
            text(selected,clipped(next.selected,100)||(blueprint===1?'Starter room':blueprint===2?'Wide stone step':pieces[next.piece-1]));
            text(blueprintCount,blueprint===2?'3 pieces':'');blueprintCount.hidden=blueprint!==2;
            starter.setAttribute('aria-pressed',String(blueprint===1));
            starter.hidden=creative;starterHint.hidden=creative||blueprint!==1;starterContents.hidden=blueprint===2;
            pieceImage.hidden=blueprint===1;
            if(next.piece>0)pieceImage.setAttribute('src',`adventure_piece_${String(next.piece).padStart(2,'0')}.svg`);
            text(cost,clipped(next.costText));
            buildCount.hidden=buildFeedback.hidden=!creative;
            text(buildCount,`${Number.isSafeInteger(next.parts)&&next.parts>=0?next.parts:'—'} / 1,024 pieces placed`);
            text(buildFeedback,`Last action: ${clipped(next.status)||'Choose a brick, aim and build.'}`);
            text(place,blueprint===1?'Place room':blueprint===2?'Place step':'Place');
            text(placement,`${next.valid?'✓':'!'} ${clipped(next.previewReason)||(next.valid?'Ready to place.':'Aim at ground or a built piece.')}`);
            placement.dataset.valid=String(next.valid===true);place.disabled=!ready||!build||next.valid!==true;
            const dialogue=mode==='dialogue'?next.dialogue:null;
            text(menuTitle,clipped(next.menuTitle,100)||clipped(dialogue?.name,80)||mode[0].toUpperCase()+mode.slice(1));
            text(dialogueRole,clipped(dialogue?.role,80));dialogueRole.hidden=!dialogue?.role;
            text(menuText,clipped(next.menuText,2048)||(mode==='journal'?clipped(next.quest?.objective):clipped(dialogue?.text,1024)));
            menuText.hidden=!menuText.textContent;text(menuStatus,clipped(next.menuStatus));menuStatus.hidden=!menuStatus.textContent;
            text(preferencesStatus,clipped(next.preferencesStatus));preferencesStatus.hidden=!settingsModes.has(mode)||!preferencesStatus.textContent;
            if(!preferencesStatus.hidden)menuStatus.hidden=true;
            categories.hidden=!catalog;
            categoryButtons.forEach((b,i)=>b.setAttribute('aria-pressed',String(next.catalogCategory===i)));
            updateRows(next);
            const navigation=next.compass,equipped=ownsCompass(next.equippedUtility)&&navigation?.equipped===true;
            const targetValid=equipped&&navigation?.available===true&&Number.isFinite(navigation.bearing)
                &&Number.isFinite(navigation.distance)&&navigation.distance>=0;
            compass.hidden=creative||!explore||!equipped;
            text(compassTarget,'Next destination');compassTarget.disabled=!ready||!equipped;
            if(targetValid){
                const bearing=((navigation.bearing%360)+360)%360,direction=['N','NE','E','SE','S','SW','W','NW'][Math.round(bearing/45)%8];
                const distance=navigation.distance<1000?`${Math.round(navigation.distance)} m`:`${(navigation.distance/1000).toFixed(1)} km`;
                text(compassReading,`${clipped(navigation.label,80)||'Destination'} · ${direction} · ${distance}`);
                const yaw=next.camera?.yaw;compassArrow.hidden=!Number.isFinite(yaw);
                if(Number.isFinite(yaw))compassArrow.style.transform=`rotate(${((bearing+yaw*180/Math.PI)%360+360)%360}deg)`;
            }else{compassArrow.hidden=true;text(compassReading,clipped(navigation?.reason)||'Directions are unavailable.');}
            saveRow.hidden=mode!=='pause';more.hidden=mode!=='pause';
            text(saveStatus,clipped(next.saveStatus)||(next.dirty?'Changes are not saved yet.':'Save to keep your progress.'));
            panel.style.setProperty('--adventure-text-scale',String([1,1.25,1.5].includes(next.textScale)?next.textScale:1));
            panel.dataset.contrast=String(next.highContrast===true);
            if(mode!==priorMode){
                if(!sheet.hidden){
                    if(document.pointerLockElement)document.exitPointerLock?.();
                    const buttons=Array.from(menuRows.querySelectorAll('button'));
                    const selectedRow=buttons.find(b=>Number(b.dataset.row)===next.menuSelected&&!b.disabled)||buttons.find(b=>!b.disabled);
                    focusPublished(selectedRow||document.getElementById('adventure-close'));
                }else if(priorMode&&(!['explore','build'].includes(priorMode)||panel.contains(document.activeElement)))returnToWorld();
            }
            priorMode=mode;
        };
        on(panel,'pointerdown',event=>event.stopPropagation());
        on(panel,'keydown',event=>{
            if(event.repeat&&(event.key==='Enter'||event.key===' '))event.preventDefault();
            if(event.key==='Escape'){event.preventDefault();if(!event.repeat){act(20);returnToWorld();}}
            if(event.key==='Tab'&&!sheet.hidden){
                const buttons=Array.from(sheet.querySelectorAll('button,a[href],summary')).filter(b=>!b.disabled&&visible(b));
                const index=buttons.indexOf(document.activeElement);
                if(buttons.length&&((event.shiftKey&&index<=0)||(!event.shiftKey&&(index===buttons.length-1||index<0)))){
                    event.preventDefault();buttons[event.shiftKey?buttons.length-1:0].focus({preventScroll:true});
                }
            }
            event.stopPropagation();
        });
        on(panel,'keyup',event=>event.stopPropagation());
        on(panel,'focusin',()=>ownInput(true));
        on(panel,'focusout',event=>{if(!panel.contains(event.relatedTarget))ownInput(false);});
        on(environment,'beforeunload',event=>{if(state?.dirty){event.preventDefault();event.returnValue='';}});
        refresh();const timer=environment.setInterval(refresh,100);
        return {refresh,cleanup(){
            if(stopped)return;ownInput(false);if(attached){try{engine._adventure_action(19,0);}catch{}}
            stopped=true;pendingInteraction=null;environment.clearInterval(timer);
            try{preferences?.cleanup();}catch{}
            for(const[el,type,fn]of handlers)el.removeEventListener(type,fn);
            for(const[b,event,fn]of rowHandlers)b.removeEventListener(event,fn);
            if(coveHome){coveContinue.remove();coveHome.append(coveContinue);}panel.remove();
            if(!hadClass)document.body.classList?.remove('voxy-adventure');
        }};
    }
    return {install,pieces,maximumStateCharacters};
});
