// Small DOM model for controller ownership tests. This is not browser layout
// evidence: visibility, event routing and focus transitions are explicit here.
export function testDOM(){
    class Event {
        constructor(type,properties={}){this.type=type;Object.assign(this,properties);}
        preventDefault(){this.defaultPrevented=true;}
        stopPropagation(){this.stopped=true;}
    }
    class Element {
        constructor(tag){this.tagName=tag.toUpperCase();this.children=[];this.dataset={};this.listeners=new Map();this.attributes=new Map();this.hidden=false;this.disabled=false;this.inert=false;this.open=false;this.value='';this.textContent='';this.style={};}
        get options(){return this.children.filter(element=>element.tagName==='OPTION');}
        addEventListener(type,handler){if(!this.listeners.has(type))this.listeners.set(type,new Set());this.listeners.get(type).add(handler);}
        removeEventListener(type,handler){this.listeners.get(type)?.delete(handler);}
        append(element){element.parentElement=this;this.children.push(element);}
        replaceChildren(){for(const child of this.children)child.parentElement=null;this.children=[];}
        remove(){if(this.parentElement){this.parentElement.children=this.parentElement.children.filter(child=>child!==this);this.parentElement=null;}}
        contains(element){return element===this||this.children.some(child=>child.contains(element));}
        setAttribute(key,value){
            this.attributes.set(key,String(value));
            if(key.startsWith('data-'))this.dataset[key.slice(5).replace(/-([a-z])/g,(_,c)=>c.toUpperCase())]=String(value);
            else if(key==='hidden')this.hidden=true;
            else this[key==='class'?'className':key]=String(value);
        }
        getAttribute(key){return this.attributes.get(key)??null;}
        matches(selector){return selector.split(',').some(part=>{
            const s=part.trim();
            if(s.startsWith('#'))return this.id===s.slice(1);
            if(s.startsWith('[contenteditable'))return this.contenteditable==='true';
            if(s.startsWith('[data-'))return this.attributes.has(s.slice(1,-1));
            const tag=s.match(/^[a-z]+/)?.[0]?.toUpperCase();if(this.tagName!==tag)return false;
            if(s.includes('[href]')&&!this.href)return false;
            if(s.includes('[open]')&&!this.open)return false;
            if(s.includes(':not([type="file"])')&&this.type==='file')return false;
            if(s.includes(':not([type="button"])')&&this.type==='button')return false;
            return true;
        });}
        querySelectorAll(selector){const result=[];for(const child of this.children){if(child.matches(selector))result.push(child);result.push(...child.querySelectorAll(selector));}return result;}
        querySelector(selector){return this.querySelectorAll(selector)[0]||null;}
        closest(selector){for(let at=this;at;at=at.parentElement)if(at.matches(selector))return at;return null;}
        getClientRects(){return this.tagName==='DIALOG'&&!this.open?[]:[{}];}
        focus(){document.activeElement=this;++this.focusCount;}
        focusCount=0;
        scrollIntoView(){++this.scrollCount;}
        scrollCount=0;
        dispatchEvent(event){event.target??=this;for(const handler of this.listeners.get(event.type)||[])handler(event);if(event.bubbles&&!event.stopped)this.parentElement?.dispatchEvent(event);return !event.defaultPrevented;}
        click(){if(this.disabled)return;this.dispatchEvent(new Event('click',{bubbles:true}));if(this.tagName==='SUMMARY')this.parentElement.open=!this.parentElement.open;}
        showModal(){this.open=true;}
        close(){this.open=false;}
    }
    const document=new Element('document');document.body=new Element('body');document.append(document.body);document.activeElement=document.body;
    document.createElement=tag=>new Element(tag);document.getElementById=id=>document.querySelector('#'+id);document.hidden=false;document.focused=true;document.hasFocus=()=>document.focused;
    const events=new Map();
    const environment={document,Event,getComputedStyle:element=>({display:element.style.display||'block',visibility:element.style.visibility||'visible'}),
        addEventListener(type,handler){if(!events.has(type))events.set(type,new Set());events.get(type).add(handler);},
        removeEventListener(type,handler){events.get(type)?.delete(handler);},
        emit(type){for(const handler of events.get(type)||[])handler(new Event(type));},events};
    const add=(tag,id,parent=document.body)=>{const element=document.createElement(tag);if(id)element.id=id;parent.append(element);return element;};
    return {document,environment,add,Event};
}
