(function(root,factory){
    const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;else root.VoxyControllerMenu=api;
})(globalThis,function(){
    'use strict';
    const focusSelector='button, summary, input:not([type="file"]), select, a[href]';
    const isText=element=>Boolean(element&&(element.tagName==='TEXTAREA'||element.getAttribute?.('contenteditable')==='true'||(element.tagName==='INPUT'&&!['button','file','checkbox','radio','range','submit','reset','color'].includes(element.type))));
    function visible(element,scope,environment){
        if(!element||element.disabled||!scope.contains(element))return false;
        for(let at=element;at;at=at.parentElement){
            if(at.hidden||at.inert)return false;
            if(at.tagName==='DETAILS'&&!at.open&&element!==at.querySelector('summary'))return false;
            if(at===scope)break;
        }
        const style=environment.getComputedStyle?.(element);
        return style?.display!=='none'&&style?.visibility!=='hidden'&&element.getClientRects().length>0;
    }
    function install(environment=globalThis,panel=environment.document.getElementById('salvage-preview')){
        const document=environment.document,canvas=document.getElementById('voxy-canvas');
        const help=document.getElementById('salvage-controller-help');
        let enabled=false,stopped=false,owned=false,modal=null,section=null,sectionBack=null,world=null,workshop=false,pinned=null,connected=false;
        const candidates=()=>Array.from((modal?.element||section||panel).querySelectorAll(focusSelector))
            .filter(element=>visible(element,modal?.element||section||panel,environment));
        const textOwned=()=>Boolean(enabled&&isText(document.activeElement)&&visible(document.activeElement,panel,environment));
        const focused=()=>!document.hidden&&document.hasFocus?.()!==false;
        const active=()=>!stopped&&enabled&&focused()&&Boolean(owned||modal||textOwned());
        const showOwnership=()=>{
            panel.dataset.controllerMenu=String(owned);
            if(help)help.hidden=!(owned||connected);
        };
        const focus=element=>{element?.focus({preventScroll:true});element?.scrollIntoView?.({block:'nearest',inline:'nearest'});};
        const repairFocus=()=>{
            if(!owned&&!modal)return;
            if(pinned&&!pinned.moved){
                if(document.activeElement===pinned.element||document.activeElement===document.body)return;
                pinned.moved=true;
            }
            const choices=candidates();
            if(!choices.includes(document.activeElement))focus(choices[0]);
        };
        const closeSection=()=>{if(section)section.open=false;section=null;sectionBack=null;};
        const release=()=>{pinned=null;closeSection();owned=false;showOwnership();canvas?.focus({preventScroll:true});};
        const openSection=(element,onBack=null)=>{
            if(stopped||!enabled||!focused()||modal||element?.tagName!=='DETAILS'
                ||!visible(element.querySelector('summary'),panel,environment))return false;
            closeSection();section=element;sectionBack=onBack;section.open=true;pinned=null;owned=true;showOwnership();
            const choices=candidates();focus(choices.find(choice=>choice.tagName!=='SUMMARY')||choices[0]);
            return true;
        };
        const pinFocus=element=>{
            if(!owned||modal||document.activeElement!==element)return ()=>{};
            const pin={element,moved:false,world};pinned=pin;
            return restore=>{
                if(pinned!==pin)return;
                const stayed=document.activeElement===element||document.activeElement===document.body;
                pinned=null;
                if(restore&&owned&&!pin.moved&&stayed&&pin.world===world&&visible(element,panel,environment))focus(element);
                else repairFocus();
            };
        };
        const closeModal=(accepted,restore=true)=>{
            if(!modal)return;
            const previous=modal;modal=null;
            previous.element.close();previous.element.remove();
            if(restore&&enabled&&!stopped){
                if(visible(previous.returnFocus,panel,environment))focus(previous.returnFocus);
                else repairFocus();
            }
            previous.finish(accepted);
        };
        const append=(parent,tag,text,attributes={})=>{
            const element=document.createElement(tag);if(text!==null)element.textContent=text;
            for(const [key,value]of Object.entries(attributes))element.setAttribute(key,value);
            parent.append(element);return element;
        };
        const openModal=(title,finish)=>{
            if(stopped||!enabled||modal)return null;
            const element=append(document.body,'dialog',null,{'class':'cove-controller-dialog','data-voxy-ui':'','aria-labelledby':'cove-controller-dialog-title'});
            const returnFocus=document.activeElement;
            append(element,'h2',title,{id:'cove-controller-dialog-title'});
            element.addEventListener('cancel',event=>{event.preventDefault();closeModal(false);});
            // The platform's document-level callbacks must never receive text,
            // Enter or arrow actions from this owned modal. Key-up is swallowed
            // here too; the native owner transition clears prior held inputs.
            element.addEventListener('keydown',event=>{
                event.stopPropagation();
                if(event.key==='Escape'){event.preventDefault();closeModal(false);}
            });
            element.addEventListener('keyup',event=>event.stopPropagation());
            modal={element,returnFocus,finish};return element;
        };
        const confirm=({title,message,confirmLabel='Remove'})=>new Promise(resolve=>{
            const dialog=openModal(title,resolve);if(!dialog){resolve(false);return;}
            append(dialog,'p',message);
            const actions=append(dialog,'div',null,{'class':'cove-dialog-actions'});
            const cancel=append(actions,'button','Cancel',{type:'button'});
            cancel.addEventListener('click',()=>closeModal(false));
            append(actions,'button',confirmLabel,{type:'button'}).addEventListener('click',()=>closeModal(true));
            dialog.showModal();focus(cancel);
        });
        const editName=(target,validate)=>new Promise(resolve=>{
            if(target.disabled||!visible(target,panel,environment)){resolve(false);return;}
            let valueInput;
            const dialog=openModal('Name your design',accepted=>{
                if(accepted){target.value=valueInput.value.trim();target.dispatchEvent(new environment.Event('input',{bubbles:true}));}
                resolve(accepted);
            });
            if(!dialog){resolve(false);return;}
            append(dialog,'p','Choose letters with the controller, or type. The first letter replaces the current name.');
            valueInput=append(dialog,'input',null,{type:'text',maxlength:'96','aria-label':'Design name',autocomplete:'off'});
            valueInput.value=target.value;
            const error=append(dialog,'p','',{role:'status','aria-live':'polite','class':'cove-name-error'});
            const grid=append(dialog,'div',null,{'class':'cove-name-keyboard'});
            let replace=true,lower=false;
            valueInput.addEventListener('input',()=>{replace=false;error.textContent='';});
            const letters=[];
            const addKey=(label,key)=>{
                const button=append(grid,'button',label,{type:'button','data-controller-key':key});
                button.addEventListener('click',()=>{
                    if(key==='case'){
                        lower=!lower;for(const letter of letters)letter.textContent=lower?letter.dataset.controllerKey.toLowerCase():letter.dataset.controllerKey;
                        return;
                    }
                    let next=valueInput.value;
                    if(key==='clear')next='';
                    else if(key==='delete')next=replace?'':Array.from(next).slice(0,-1).join('');
                    else next=(replace?'':next)+(key==='space'?' ':lower?key.toLowerCase():key);
                    if(new TextEncoder().encode(next).length>96){error.textContent='Use a shorter name (96 bytes maximum).';return;}
                    replace=false;valueInput.value=next;error.textContent='';
                });
                return button;
            };
            for(const letter of 'ABCDEFGHIJKLMNOPQRSTUVWXYZ')letters.push(addKey(letter,letter));
            for(const digit of '0123456789')addKey(digit,digit);
            addKey('Space','space');addKey('-','-');addKey('_','_');addKey('.','.');addKey('Aa','case');addKey('Delete','delete');addKey('Clear','clear');
            const actions=append(dialog,'div',null,{'class':'cove-dialog-actions'});
            append(actions,'button','Cancel',{type:'button'}).addEventListener('click',()=>closeModal(false));
            append(actions,'button','Use name',{type:'button'}).addEventListener('click',()=>{
                if(!validate(valueInput.value.trim())){error.textContent='Enter a short name on one line.';return;}
                closeModal(true);
            });
            dialog.showModal();focus(letters[0]);
        });
        const move=(direction)=>{
            const choices=candidates();if(!choices.length)return;
            const current=document.activeElement,index=choices.indexOf(current);
            if(current?.tagName==='SELECT'&&(direction==='left'||direction==='right')){
                const options=Array.from(current.options).filter(option=>!option.disabled&&!option.hidden);
                if(options.length){
                    const at=options.findIndex(option=>option.value===current.value);
                    current.value=options[(at+options.length+(direction==='right'?1:options.length-1))%options.length].value;
                    current.dispatchEvent(new environment.Event('change',{bubbles:true}));
                }
                return;
            }
            if(current?.tagName==='SUMMARY'&&(direction==='left'||direction==='right')){
                current.parentElement.open=direction==='right';return;
            }
            // The letter keyboard has six columns. Other controls use a stable
            // reading order, so wrapping or a narrow viewport cannot trap focus.
            const step=current?.dataset?.controllerKey&&(direction==='up'||direction==='down')?6:1;
            const sign=direction==='up'||direction==='left'?-1:1;
            focus(choices[index<0?0:(index+choices.length+sign*step)%choices.length]);
        };
        const input=events=>{
            if(!enabled||stopped||!focused())return false;
            const wasActive=active();
            if(events.menu){
                if(modal)closeModal(false);
                else if(owned||textOwned())release();
                else {owned=true;showOwnership();repairFocus();}
                return true;
            }
            if(!wasActive)return false;
            if(events.back){
                if(modal)closeModal(false);
                else if(section&&sectionBack)sectionBack();
                else if(section)release();
                else {
                    const drawer=document.activeElement?.closest?.('details[open]');
                    if(drawer&&panel.contains(drawer)){drawer.open=false;focus(drawer.querySelector('summary'));}
                    else release();
                }
                return true;
            }
            if(pinned&&!pinned.moved&&(document.activeElement===pinned.element||document.activeElement===document.body)){
                const direction=['up','down','left','right'].find(key=>events[key]);
                if(!direction)return true; // Waiting Confirm cannot activate a fallback button.
                pinned.moved=true;move(direction);return true;
            }
            repairFocus();
            for(const direction of ['up','down','left','right'])if(events[direction]){move(direction);return true;}
            if(events.confirm){
                const target=document.activeElement;
                if(candidates().includes(target)){
                    if(target.id==='design-name')void editName(target,environment.VoxyDesignLibrary.validName);
                    else if(target.tagName==='SELECT')move('right');
                    else if(isText(target))return true;
                    else target.click();
                    if(owned&&!modal&&visible(target,panel,environment))focus(target);
                }
                repairFocus();
            }
            return true;
        };
        const priorInput=environment.voxyControllerMenuInput,priorActive=environment.voxyControllerMenuActive;
        environment.voxyControllerMenuInput=input;environment.voxyControllerMenuActive=active;
        const blur=()=>{pinned=null;closeModal(false,false);closeSection();owned=false;showOwnership();};
        const hidden=()=>{if(document.hidden)blur();};
        const pointer=event=>{
            if(!owned||modal||!event.target)return;
            if(!panel.contains(event.target))release();
            else if(section&&!section.contains(event.target)){section=null;sectionBack=null;}
        };
        const resized=()=>{
            if(!active())return;
            const current=document.activeElement;
            if(candidates().includes(current))focus(current);
            else repairFocus();
        };
        environment.addEventListener('resize',resized);
        environment.addEventListener('blur',blur);document.addEventListener('visibilitychange',hidden);
        document.addEventListener('pointerdown',pointer);
        return {
            active,confirm,editName,pinFocus,openSection,release,
            tick(state){
                const nextWorld=state.observation?`${state.observation.world}/${state.observation.incarnation}/${state.observation.epoch}`:state.world||null;
                const nextWorkshop=Boolean(state.workshop?.open);
                const nextEnabled=Boolean(state.active&&state.ready&&!state.failed&&state.session?.admissionOpen!==false);
                const nextConnected=Boolean(state.gamepad?.connected);
                if(!nextEnabled||(world!==null&&nextWorld!==world)||(workshop&&!nextWorkshop)||(connected&&!nextConnected)
                    ||(section&&(!section.open||!visible(section.querySelector('summary'),panel,environment)
                        ||(section.id==='salvage-camera'&&(nextWorkshop||state.characterCamera?.available!==true)))))blur();
                else if(modal&&(state.busy||state.workshop?.pending))closeModal(false);
                enabled=nextEnabled;world=nextWorld;workshop=nextWorkshop;connected=nextConnected;
                showOwnership();repairFocus();
            },
            cleanup(){
                if(stopped)return;blur();stopped=true;enabled=false;
                environment.removeEventListener('blur',blur);environment.removeEventListener('resize',resized);document.removeEventListener('visibilitychange',hidden);
                document.removeEventListener('pointerdown',pointer);
                if(environment.voxyControllerMenuInput===input)environment.voxyControllerMenuInput=priorInput;
                if(environment.voxyControllerMenuActive===active)environment.voxyControllerMenuActive=priorActive;
            }
        };
    }
    return {install,visible};
});
