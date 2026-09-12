(function(root,factory){
    const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;else root.VoxyCovePreferences=api;
})(globalThis,function(){
    'use strict';
    const storageKey='voxys.cove.input-preferences.v1';
    const maximumBytes=32768;
    const bools=[['invertX','Invert horizontal look'],['invertY','Invert vertical look'],
        ['reelToggle','Press once to reel; press again to stop'],['orbitToggle','Press once to orbit; press again to stop'],
        ['highContrast','High contrast'],['tutorialsEnabled','Show tutorials'],['captionsEnabled','Show event captions']];
    const numbers=[['mouseSensitivity','Mouse look speed',.25,3,.25],['padSensitivity','Controller look speed',.25,3,.25],
        ['moveDeadzone','Movement stick deadzone',.05,.45,.05],['lookDeadzone','Camera stick deadzone',.05,.45,.05]];
    const keys=[[0,'Unbound'],[32,'Space'],[257,'Enter'],[258,'Tab'],[259,'Backspace'],[260,'Insert'],[261,'Delete'],
        [263,'Left'],[262,'Right'],[265,'Up'],[264,'Down'],
        ...Array.from({length:26},(_,i)=>[65+i,String.fromCharCode(65+i)]),...Array.from({length:10},(_,i)=>[48+i,String(i)]),
        ...Array.from({length:12},(_,i)=>[290+i,`F${i+1}`]).filter(([key])=>key!==298)];
    const pads=['A / Confirm','B / Back','X','Y','Left shoulder','Right shoulder','Left trigger','Right trigger','View','Menu','Left stick click','Right stick click','D-pad up','D-pad down','D-pad left','D-pad right'];
    function install(engine,environment,panel,controller){
        if(!panel)return undefined;
        const document=environment.document;let stopped=false,initialized=false,settings=null,metadata=[],preferenceKey='';
        const changed=new Set(),bindingChanges=new Map();
        const fields=new Map(),handlers=[];
        const call=(kind,text='')=>engine.ccall('voxy_cove_preferences_action','string',['number','string'],[kind,text]);
        const add=(parent,tag,label,attrs={})=>{const e=document.createElement(tag);if(label!==null)e.textContent=label;for(const[k,v]of Object.entries(attrs))e.setAttribute(k,v);parent.append(e);return e;};
        const on=(element,event,fn)=>{element.addEventListener(event,fn);handlers.push([element,event,fn]);};
        const field=(name,label,type)=>{
            const line=add(panel,'label',null,{'class':'cove-preference-row'});add(line,'span',label);
            const input=add(line,type==='select'?'select':'input',null,{id:`cove-pref-${name}`,...(type==='select'?{}:{type})});
            fields.set(name,input);on(input,'change',()=>changed.add(name));return input;
        };
        add(panel,'h4','Comfort and controls');
        for(const[name,label,min,max,step]of numbers){const input=field(name,label,'select');
            for(let v=min;v<=max+1e-8;v+=step){const value=Number(v.toFixed(2));add(input,'option',`${value.toFixed(2)}${name.includes('Sensitivity')?' ×':''}`,{value:String(value)});}}
        const scale=field('textScale','Text size','select');for(const[v,label]of[[1,'Standard'],[1.25,'Larger · 125%'],[1.5,'Largest · 150%']])add(scale,'option',label,{value:String(v)});
        for(const[name,label]of bools)field(name,label,'checkbox');
        add(panel,'p','English is installed. Preferences stay on this device; expedition saves are separate.',{'class':'salvage-note'});
        const actionRow=add(panel,'label','Action to rebind');
        const action=add(actionRow,'select',null,{id:'cove-binding-action'});
        const bindingFields=new Map();
        for(const[name,label,options]of[
            ['key','Primary key',keys],['modifiers','Primary modifiers',[[0,'None'],[1,'Shift'],[2,'Control'],[4,'Alt'],[3,'Control + Shift'],[5,'Alt + Shift'],[6,'Control + Alt'],[7,'Control + Alt + Shift']]],
            ['alternate','Alternate key',keys],['alternateModifiers','Alternate modifiers',[[0,'None'],[1,'Shift'],[2,'Control'],[4,'Alt'],[3,'Control + Shift'],[5,'Alt + Shift'],[6,'Control + Alt'],[7,'Control + Alt + Shift']]],
            ['pad','Controller button',[[-1,'Unbound'],...pads.map((label,i)=>[i,label])]],
        ]){
            const row=add(panel,'label',null,{'class':'cove-preference-row'});add(row,'span',label);
            const input=add(row,'select',null,{id:`cove-binding-${name}`});for(const[value,label]of options)add(input,'option',label,{value:String(value)});bindingFields.set(name,input);on(input,'change',()=>{const draft=bindingChanges.get(action.value)||{};draft[name]=Number(input.value);bindingChanges.set(action.value,draft);});
        }
        add(panel,'p','Menu navigation, Confirm and Back always remain available. Apply reports conflicting controls without changing your current setup.',{'class':'salvage-note'});
        const actions=add(panel,'div',null,{'class':'cove-menu-actions'});
        const apply=add(actions,'button','Apply settings',{type:'button',id:'cove-preferences-apply'});
        const reset=add(actions,'button','Reset controls and comfort',{type:'button',id:'cove-preferences-reset'});
        const status=add(panel,'p','Loading current settings…',{role:'status','aria-live':'polite',id:'cove-preferences-status'});
        const style=()=>{
            const target=document.documentElement||document.body;
            target.style?.setProperty?.('--cove-ui-scale',String(settings?.textScale||1));
            target.setAttribute('data-cove-contrast',String(Boolean(settings?.highContrast)));
        };
        const binding=()=>settings?.bindings?.find(row=>row.action===action.value);
        const loadBinding=()=>{const stored=binding();if(!stored)return;const row={...stored,...bindingChanges.get(action.value)};for(const[name,input]of bindingFields){
            // Preserve installed keys that are not in the common selector list.
            if(!Array.from(input.options).some(option=>option.value===String(row[name])))add(input,'option',`Key ${row[name]}`,{value:String(row[name])});
            for(const option of input.options)if((name==='key'||name==='alternate')&&option.value==='291')option.disabled=!['camera_menu','tools_menu'].includes(action.value);
            input.value=String(row[name]);
        }};
        const render=()=>{
            if(!settings)return;
            for(const[name,input]of fields){if(input.type==='checkbox')input.checked=Boolean(settings[name]);else {if(!Array.from(input.options).some(option=>option.value===String(settings[name])))add(input,'option',String(settings[name]),{value:String(settings[name])});input.value=String(settings[name]);}}
            const selected=action.value;action.replaceChildren();
            for(const row of settings.bindings){const info=metadata.find(value=>value.id===row.action);add(action,'option',info?.label||row.action,{value:row.action});}
            action.value=settings.bindings.some(row=>row.action===selected)?selected:settings.bindings[0]?.action||'';
            loadBinding();style();
        };
        const fetch=()=>{const text=call(1);if(typeof text!=='string'||text.length>maximumBytes)throw Error('Settings are unavailable.');const value=JSON.parse(text);
            if(value.version!==1||!Array.isArray(value.bindings))throw Error('Settings are unavailable.');return value;};
        const remember=()=>{try{environment.localStorage?.setItem(storageKey,JSON.stringify(settings));return true;}catch{return false;}};
        on(action,'change',loadBinding);
        on(apply,'click',()=>{
            if(stopped||apply.disabled||!settings)return;
            try{
                // Merge only this form's edits into current core settings so a
                // tutorial/camera toggle elsewhere cannot be overwritten.
                const next=fetch();
                for(const name of changed){const input=fields.get(name);next[name]=input.type==='checkbox'?Boolean(input.checked):Number(input.value);}
                for(const [id,patch]of bindingChanges){const row=next.bindings.find(value=>value.action===id);if(row)Object.assign(row,patch);}
                const result=call(2,JSON.stringify(next));
                if(result!=='ok'){status.textContent=result||'Those settings could not be applied.';return;}
                settings=fetch();changed.clear();bindingChanges.clear();render();status.textContent=remember()?'Settings applied and remembered.':'Settings applied for this visit. Device storage is unavailable.';
            }catch(error){status.textContent=error.message||'Those settings could not be applied.';}
        });
        on(reset,'click',()=>{
            if(stopped||reset.disabled)return;
            try{const result=call(3);if(result!=='ok'){status.textContent=result||'Settings could not be reset.';return;}
                settings=fetch();changed.clear();bindingChanges.clear();render();status.textContent=remember()?'Default settings restored.':'Defaults restored for this visit.';
            }catch(error){status.textContent=error.message||'Settings could not be reset.';}
        });
        return {refresh(){if(!settings||changed.size||bindingChanges.size)return;try{settings=fetch();render();}catch{}},tick(state){
            if(stopped)return;
            const enabled=Boolean(state.ready&&!state.failed&&state.session?.admissionOpen!==false);
            if(!initialized&&enabled){
                initialized=true;
                try{
                    let stored;try{stored=environment.localStorage?.getItem(storageKey);}catch{}
                    if(stored&&stored.length<=maximumBytes){const result=call(2,stored);if(result!=='ok')status.textContent='Saved settings were refused. Current controls are unchanged.';}
                    settings=fetch();metadata=state.ui?.actions||[];render();
                    if(status.textContent==='Loading current settings…')status.textContent='Current settings loaded.';
                }catch(error){status.textContent=error.message||'Settings are unavailable.';}
            }
            if(settings&&metadata.length===0&&state.ui?.actions?.length){metadata=state.ui.actions;
                for(const option of action.options){const row=metadata.find(value=>value.id===option.value);if(row)option.textContent=row.label;}}
            const nextKey=JSON.stringify([state.ui?.preferenceMessage,...[...numbers.map(row=>row[0]),...bools.map(row=>row[0]),'textScale'].map(name=>state.ui?.[name])]);
            if(settings&&nextKey!==preferenceKey){preferenceKey=nextKey;try{const current=fetch();
                // Keep an unfinished form and its focus stable; Apply merges it.
                if(!changed.size&&!bindingChanges.size){settings=current;render();}
                else {const before=settings;settings=current;style();settings=before;}
            }catch{}}
            for(const element of [...fields.values(),action,...bindingFields.values(),apply,reset])element.disabled=!enabled||!settings;
        },cleanup(){stopped=true;for(const[element,event,handler]of handlers)element.removeEventListener(event,handler);}};
    }
    return {install,storageKey};
});
