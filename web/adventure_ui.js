(function(root,factory){
    const api=factory();
    if(typeof module==='object'&&module.exports)module.exports=api;
    else root.VoxyAdventureUI=api;
})(globalThis,function(){
    'use strict';
    const pieces=Object.freeze([
        'Foundation','Floor','Wall','Open doorway','Flat roof','Stairs','Beam',
        'Brick 1 × 2','Brick 2 × 2','Brick 2 × 4','Bed','Chest','Workbench','Pier',
    ]);
    // Full read-only observations include 1,024 placed parts, 32 components,
    // matrices and menu rows. Keep a bounded observer without rejecting an
    // otherwise valid world merely because its house exceeds a small UI packet.
    const maximumStateCharacters=512*1024;
    const text=(element,value)=>{const next=String(value??'');if(element.textContent!==next)element.textContent=next;};
    function install(engine,environment=globalThis){
        const document=environment.document;
        if(!document||typeof engine?._adventure_action!=='function'
            ||typeof engine?._get_adventure_state_json!=='function')return undefined;
        const add=(parent,tag,label,attributes={})=>{
            const element=document.createElement(tag);
            if(label!==null)element.textContent=label;
            for(const[key,value]of Object.entries(attributes))element.setAttribute(key,String(value));
            parent.append(element);return element;
        };
        const hadAdventureClass=document.body.classList?.contains('voxy-adventure')===true;
        document.body.classList?.add('voxy-adventure');
        const panel=add(document.body,'aside',null,{id:'adventure-ui','data-voxy-ui':'true','aria-label':'Adventure controls'});
        const title=add(panel,'div',null,{class:'adventure-heading'});
        add(title,'strong','Voxys');
        const hide=add(title,'button','Hide controls',{type:'button',id:'adventure-collapse','aria-expanded':'true','aria-controls':'adventure-controls'});
        const controls=add(panel,'div',null,{id:'adventure-controls'});
        const guide=add(controls,'p','Explore the landscape. Build a home. Store, craft and rest.',{class:'adventure-guide'});
        add(controls,'p','Building preview — quests and enemies come next.',{class:'adventure-note',id:'adventure-stage'});
        const status=add(controls,'p','Preparing your adventure…',{id:'adventure-status',role:'status','aria-live':'polite'});
        const stock=add(controls,'p','',{id:'adventure-stock','aria-label':'Building supplies'});
        const actions=add(controls,'div',null,{class:'adventure-actions'});
        const button=(parent,label,id,action,value=0)=>{
            const b=add(parent,'button',label,{type:'button',id});
            on(b,'click',()=>act(action,value));return b;
        };
        const handlers=[];let stopped=false,state=null,rowsKey='',priorMenu='',ready=false,uiOwned=false;
        const on=(element,type,listener)=>{element.addEventListener(type,listener);handlers.push([element,type,listener]);};
        const ownInput=owned=>{
            if(stopped||uiOwned===owned)return;
            uiOwned=owned;
            // This is input ownership, not a gameplay command. Do not refresh
            // the DOM here: focus changes can occur during a normal refresh.
            try{engine._adventure_action(15,owned?1:0);}catch{}
        };
        const read=()=>{
            const ptr=engine._get_adventure_state_json();
            if(!ptr)throw Error('Adventure is not ready.');
            const raw=engine.UTF8ToString(ptr);
            if(typeof raw!=='string'||raw.length>maximumStateCharacters)throw Error('Adventure status is unavailable.');
            const value=JSON.parse(raw);
            if(!value||typeof value!=='object'||Array.isArray(value))throw Error('Adventure status is unavailable.');
            if(typeof value.build!=='boolean'||!Number.isInteger(value.piece)||value.piece<0||value.piece>pieces.length)
                throw Error('Adventure status is unavailable.');
            return value;
        };
        const act=(action,value=0)=>{
            if(stopped||!ready)return false;
            try{
                // UI never writes game state. Every operation crosses the same
                // validated intent boundary as keyboard/controller commands.
                const result=engine._adventure_action(action,value);
                refresh();return result===1;
            }catch{ text(status,'That action is unavailable. Try again.');return false; }
        };
        const build=button(actions,'Build','adventure-build',1);
        const starter=button(actions,'Starter room','adventure-starter',14);
        starter.setAttribute('aria-pressed','false');
        const interact=button(actions,'Use nearby','adventure-interact',7);
        const menu=button(actions,'Menu','adventure-menu',9);
        const builder=add(controls,'section',null,{id:'adventure-builder','aria-label':'Build freely'});
        const starterTitle=add(builder,'strong','Starter room',{id:'adventure-starter-title'});
        const individual=button(builder,'Choose individual pieces','adventure-individual',13);
        const selectorLabel=add(builder,'label','Building piece',{for:'adventure-piece'});
        const selector=add(builder,'select',null,{id:'adventure-piece'});
        pieces.forEach((name,index)=>add(selector,'option',name,{value:index+1}));
        const cost=add(builder,'p','',{id:'adventure-cost',class:'adventure-note'});
        const placement=add(builder,'p','Aim at the ground or a built piece.',{id:'adventure-placement',role:'status','aria-live':'polite'});
        const buildActions=add(builder,'div',null,{class:'adventure-actions'});
        const place=button(buildActions,'Place','adventure-place',4);
        button(buildActions,'Rotate','adventure-rotate',3);
        button(buildActions,'Lower','adventure-lower',12,-1);
        button(buildActions,'Raise','adventure-raise',12,1);
        button(buildActions,'Remove aimed piece','adventure-remove',5);
        button(buildActions,'Undo last build','adventure-undo',6);
        const buildHint=add(builder,'p','Choose a piece, aim, then Place. Use piers or stairs on slopes.',{class:'adventure-note'});
        const menuPanel=add(controls,'section',null,{id:'adventure-menu-panel','aria-label':'Current interaction'});
        const menuTitle=add(menuPanel,'h2','',{id:'adventure-menu-title'});
        const menuRows=add(menuPanel,'div',null,{id:'adventure-menu-rows',class:'adventure-menu-rows'});
        const saveRow=add(controls,'div',null,{class:'adventure-save-row'});
        const save=button(saveRow,'Save home & progress','adventure-save',8);
        const saveStatus=add(saveRow,'p','',{id:'adventure-save-status',role:'status','aria-live':'polite'});
        const more=add(controls,'details',null,{id:'adventure-more'});
        add(more,'summary','Help & worlds');
        add(more,'p','Your bed, chest and workbench make a house useful. Use them nearby to rest, store items and craft.',{class:'adventure-note'});
        button(more,'Return to a safe place','adventure-recover',11);
        const links=add(more,'nav',null,{'aria-label':'Adventure worlds'});
        const continued=add(links,'a','Continue saved adventure',{href:'?experience=adventure',id:'adventure-continue'});
        const fresh=add(links,'a','Start a new adventure',{href:'?experience=adventure&new=1',id:'adventure-new'});
        add(links,'a','Cove & playground',{href:'?experience=lego-world',id:'adventure-prototypes'});
        // The existing Cove helper owns its metadata and namespace validation.
        // Reuse its confirmed shortcut unchanged; never interpret saves here.
        const coveContinue=document.getElementById('cove-continue');
        const coveHome=coveContinue&&!coveContinue.hidden&&coveContinue.href?coveContinue.parentElement:null;
        if(coveHome){coveContinue.remove();links.append(coveContinue);}
        let rowHandlers=[];
        const updateRows=next=>{
            // Chest menus can contain all 32 backpack and 32 chest slots.
            const rows=Array.isArray(next.rows)?next.rows.slice(0,64):[];
            const normalized=rows.map((row,index)=>({label:String(typeof row==='string'?row:row?.label??'').slice(0,160),
                enabled:typeof row==='string'||row?.enabled!==false,index}));
            const key=JSON.stringify(normalized);
            if(key===rowsKey)return;
            rowsKey=key;
            const focused=menuRows.contains(document.activeElement)?document.activeElement?.dataset?.row:null;
            for(const[element,listener]of rowHandlers)element.removeEventListener('click',listener);
            rowHandlers=[];menuRows.replaceChildren();
            for(const row of normalized){
                if(!row.label)continue;
                const b=add(menuRows,'button',row.label,{type:'button','data-row':row.index});
                b.disabled=!row.enabled;
                const listener=()=>act(10,row.index);b.addEventListener('click',listener);rowHandlers.push([b,listener]);
                if(String(row.index)===focused&&!b.disabled)b.focus({preventScroll:true});
            }
        };
        const refresh=()=>{
            if(stopped)return;
            let next;
            try{next=read();}catch{
                ready=false;text(status,'Adventure status is unavailable.');
                for(const b of panel.querySelectorAll('button'))if(b!==hide)b.disabled=true;
                return;
            }
            state=next;ready=next.ready!==false&&next.failed!==true;
            for(const b of panel.querySelectorAll('button'))if(b!==hide)b.disabled=!ready;
            builder.hidden=!next.build;menuPanel.hidden=!next.menu;
            text(status,next.status||'Explore the landscape and choose a home site.');
            text(build,next.build?'Finish building':'Build');build.setAttribute('aria-pressed',String(Boolean(next.build)));
            const starterSelected=next.piece===0;
            starter.setAttribute('aria-pressed',String(Boolean(next.build&&starterSelected)));
            starterTitle.hidden=!starterSelected;individual.hidden=!starterSelected;
            selectorLabel.hidden=starterSelected;selector.hidden=starterSelected;
            text(starterTitle,next.selected||'Starter room');
            text(place,starterSelected?'Place room':'Place');
            text(buildHint,starterSelected?'Aim at open ground. Place the whole room, then make it yours.'
                :'Choose a piece, aim, then Place. Use piers or stairs on slopes.');
            text(interact,next.interaction||'Use nearby');
            text(guide,next.guide||'Explore the landscape. Build a home. Store, craft and rest.');
            const number=value=>Number.isSafeInteger(value)&&value>=0?String(value):'—';
            text(stock,`Wood ${number(next.wood)} · Stone ${number(next.stone)} · Scrap ${number(next.scrap)}`);
            if(Number.isInteger(next.piece)&&next.piece>=1&&next.piece<=pieces.length)selector.value=String(next.piece);
            selector.disabled=!ready;
            text(cost,next.costText||'');cost.hidden=!next.costText;
            text(placement,next.previewReason||(next.valid?'Ready to place.':'Aim at the ground or a built piece.'));
            placement.dataset.valid=String(Boolean(next.valid));place.disabled=!ready||!next.build||!next.valid;
            text(menuTitle,next.menu||'');
            updateRows(next);
            for(const b of menuRows.querySelectorAll('button')){
                const index=Number(b.dataset.row),row=Array.isArray(next.rows)?next.rows[index]:null;
                b.disabled=!ready||!row||(typeof row==='object'&&row.enabled===false);
            }
            save.disabled=!ready||next.canSave===false;
            text(saveStatus,next.saveStatus||(next.dirty?'Changes are not saved yet.':'Save to keep your progress.'));
            if(next.textScale&&[1,1.25,1.5].includes(next.textScale))panel.style.setProperty('--adventure-text-scale',String(next.textScale));
            panel.dataset.contrast=String(Boolean(next.highContrast));
            // Open a new interaction once; polling must never repeatedly steal focus.
            if(next.menu&&next.menu!==priorMenu&&!controls.hidden){
                if(document.pointerLockElement)document.exitPointerLock?.();
                menuRows.querySelector('button:not([disabled])')?.focus({preventScroll:true});
            }
            priorMenu=next.menu||'';
        };
        on(selector,'change',()=>{const value=Number(selector.value);if(Number.isInteger(value)&&value>=1&&value<=14)act(2,value);});
        on(hide,'click',()=>{controls.hidden=!controls.hidden;hide.setAttribute('aria-expanded',String(!controls.hidden));text(hide,controls.hidden?'Show controls':'Hide controls');});
        on(panel,'pointerdown',event=>event.stopPropagation());
        on(panel,'keydown',event=>{
            if(event.key==='Escape'){
                event.preventDefault();
                if(!event.repeat){
                    let menuOpen=false;try{menuOpen=Boolean(read().menu);}catch{}
                    const active=document.activeElement;
                    if(panel.contains(active))active.blur?.();
                    if(menuOpen)act(9);
                }
            }
            // Preserve native Enter/Space activation and Tab navigation, but
            // never also deliver those keys to the document's WASM callbacks.
            event.stopPropagation();
        });
        on(panel,'keyup',event=>event.stopPropagation());
        on(panel,'focusin',event=>{ownInput(true);event.target?.scrollIntoView?.({block:'nearest',inline:'nearest'});});
        on(panel,'focusout',event=>{if(!panel.contains(event.relatedTarget))ownInput(false);});
        const beforeUnload=event=>{if(state?.dirty){event.preventDefault();event.returnValue='';}};
        on(environment,'beforeunload',beforeUnload);
        refresh();
        const timer=environment.setInterval(refresh,250);
        return {refresh,cleanup(){
            if(stopped)return;ownInput(false);stopped=true;environment.clearInterval(timer);
            for(const[element,type,listener]of handlers)element.removeEventListener(type,listener);
            for(const[element,listener]of rowHandlers)element.removeEventListener('click',listener);
            if(coveHome){coveContinue.remove();coveHome.append(coveContinue);}
            panel.remove();
            if(!hadAdventureClass)document.body.classList?.remove('voxy-adventure');
        }};
    }
    return {install,pieces,maximumStateCharacters};
});
