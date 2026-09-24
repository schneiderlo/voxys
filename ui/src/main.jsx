export {installShared} from './shared.js';
import {render} from 'preact';
import {useEffect,useLayoutEffect,useRef,useState} from 'preact/hooks';
import {Blocks,Footprints,RotateCw,Undo2,Trash2,ArrowUp,ArrowDown,Save,Menu,HelpCircle,X,Check,ChevronRight,Settings2,Plus,TriangleAlert,Hand,ChevronLeft,Search,Palette,Bike,Compass,House,Armchair} from 'lucide-preact';
import {createBridge} from './bridge.js';
import './style.css';

const names=['Foundation','Floor','Wall','Open doorway','Flat roof','Stairs','Beam','Brick 1 × 2','Brick 2 × 2','Brick 2 × 4','Bed','Chest','Workbench','Pier','Hinged door'];
export const order=[8,9,10,2,3,5,4,15,6,7,1,14,11,12,13];
const colours=[['Original',0,'#d7cbb0'],['Coral',0xe57b66,'#e57b66'],['Sunflower',0xe9bd55,'#e9bd55'],['Sage',0x86a789,'#86a789'],['Lagoon',0x76a9b0,'#76a9b0'],['Cloud',0xf1eee4,'#f1eee4'],['Ink',0x414d50,'#414d50']];
const text=value=>typeof value==='string'?value.slice(0,1000):'';
const imageFor=id=>`adventure_piece_${String(id).padStart(2,'0')}.svg`;
function Button({icon:Icon,children,shortcut,className='',...props}){
    return <button type="button" class={`bb-button ${className}`} {...props}>{Icon&&<Icon size={19} strokeWidth={1.8} aria-hidden="true"/>}{children&&<span>{children}</span>}{shortcut&&<kbd>{shortcut}</kbd>}</button>;
}
// Small vector bricks: static geometry, CSS colour, no canvas or animation loop.
function PlasticBrick({original=false}){
    return <svg class="bb-plastic-brick" viewBox="0 0 88 66" aria-hidden="true">
        <path d="M8 28 40 13 80 29 48 46Z" fill="var(--swatch)"/>
        <path d="M8 28 48 46 48 61 8 43Z" fill={original?'#a5784f':'var(--swatch)'}/>
        <path d="M48 46 80 29 80 44 48 61Z" fill={original?'#627052':'var(--swatch)'}/>
        <path d="M8 28 48 46 48 61 8 43Z" fill="#000" opacity=".13"/>
        <path d="M48 46 80 29 80 44 48 61Z" fill="#000" opacity=".27"/>
        <path d="M8 28 40 13 80 29 48 46Z" fill="#fff" opacity=".14"/>
        {[{x:34,y:25},{x:55,y:34}].map(({x,y})=><g key={x}>
            <path d={`M${x-10} ${y-7}v7a10 5 0 0 0 20 0v-7`} fill="var(--swatch)"/>
            <path d={`M${x-10} ${y-7}v7a10 5 0 0 0 20 0v-7`} fill="#000" opacity=".12"/>
            <ellipse cx={x} cy={y-7} rx="10" ry="5" fill="var(--swatch)" stroke="#ffffff85" strokeWidth="1"/>
            <ellipse cx={x} cy={y-8} rx="7" ry="3" fill="#fff" opacity=".12"/>
        </g>)}
        <path d="M9 28 48 45 79 29M48 46v14" fill="none" stroke="#fff" strokeOpacity=".4"/>
    </svg>;
}
function App({engine,environment}){
    const [state,setState]=useState(null),[popup,setPopup]=useState(null),[toast,setToast]=useState(''),[search,setSearch]=useState(''),[hoverColour,setHoverColour]=useState(null);
    const bridge=useRef(null),root=useRef(null),dialog=useRef(null),bar=useRef(null),previousMode=useRef(null),lastStatus=useRef(null),wheel=useRef(0);
    useEffect(()=>{const owner=createBridge(engine,environment,setState);bridge.current=owner;return ()=>{owner.cleanup();bridge.current=null;};},[]);
    const modal=state&&!state.failed&&!['build','explore'].includes(state.mode),busy=!state||state.failed||state.pending;
    const act=(action,value=0,token=null,returnFocus=true)=>{const accepted=bridge.current?.action(action,value,token);if(accepted&&returnFocus)bridge.current.worldFocus();return accepted;};
    useLayoutEffect(()=>{
        if(!state||state.failed)return;
        if(previousMode.current!==state.mode){
            setPopup(null);setSearch('');
            if(modal)dialog.current?.querySelector('button:not(:disabled)')?.focus();
            else if(previousMode.current&&!['build','explore'].includes(previousMode.current))bridge.current?.worldFocus();
            previousMode.current=state.mode;
        }
    },[state?.mode]);
    useLayoutEffect(()=>{if(modal)dialog.current?.querySelector(`[data-row="${state.menuSelected}"]`)?.focus({preventScroll:true});},[state?.menuSelected,state?.menuToken]);
    useLayoutEffect(()=>{bar.current?.querySelector('[aria-pressed="true"]')?.scrollIntoView?.({block:'nearest',inline:'nearest',behavior:'instant'});},[state?.piece,state?.mode]);
    useEffect(()=>{
        if(!state||state.failed)return;
        const previous=lastStatus.current;
        const changed=previous!==null&&(previous.status!==state.status||previous.event!==state.statusEvent);
        lastStatus.current={status:state.status,event:state.statusEvent};
        if(!changed)return;
        setToast(text(state.status));const timer=environment.setTimeout(()=>setToast(''),3000);return ()=>environment.clearTimeout(timer);
    },[state?.status,state?.statusEvent]);
    useEffect(()=>{
        if(!popup)return;
        const dismiss=e=>{if(!root.current?.querySelector('.bb-popup')?.contains(e.target)&&!e.target.closest?.('[data-popup-toggle]'))setPopup(null);};
        environment.document.addEventListener('pointerdown',dismiss);
        return ()=>environment.document.removeEventListener('pointerdown',dismiss);
    },[popup]);
    const keyDown=event=>{
        event.stopPropagation();
        if(event.key==='Escape'){event.preventDefault();if(event.repeat)return;if(popup){setPopup(null);root.current?.querySelector(`[data-popup-toggle="${popup}"]`)?.focus();}else act(modal?20:31);return;}
        if(event.repeat&&(event.key==='Enter'||event.key===' '))event.preventDefault();
        if(event.key==='Tab'&&modal){
            const items=[...dialog.current.querySelectorAll('button:not(:disabled),a[href],input:not(:disabled)')];if(!items.length)return;
            const first=items[0],last=items.at(-1),active=environment.document.activeElement;
            if(event.shiftKey&&(active===first||!dialog.current.contains(active))){event.preventDefault();last.focus();}
            else if(!event.shiftKey&&(active===last||!dialog.current.contains(active))){event.preventDefault();first.focus();}
        }
    };
    const cycle=event=>{
        event.preventDefault();event.stopPropagation();if(busy||event.ctrlKey||event.deltaY===0)return;
        wheel.current+=event.deltaY*(event.deltaMode===1?40:event.deltaMode===2?800:1);
        if(Math.abs(wheel.current)<50)return;
        const next=order[(order.indexOf(state.piece)+(wheel.current>0?1:order.length-1))%order.length];wheel.current=0;act(2,next);
    };
    const navigatePieces=event=>{
        if(!['ArrowLeft','ArrowRight','Home','End'].includes(event.key))return;
        event.preventDefault();event.stopPropagation();
        const buttons=[...bar.current.querySelectorAll('button:not(:disabled)')];
        if(!buttons.length)return;
        const current=buttons.indexOf(environment.document.activeElement);
        const index=event.key==='Home'?0:event.key==='End'?buttons.length-1:
            (current+(event.key==='ArrowRight'?1:buttons.length-1))%buttons.length;
        buttons[index].focus({preventScroll:true});
        buttons[index].scrollIntoView?.({block:'nearest',inline:'nearest'});
    };
    const selectedColour=colours.find(c=>c[1]===state?.paint)||colours[0];
    const saved=state?.dirty===false&&state?.saveStatus?.startsWith('Saved'),saveLabel=state?.saveStatus?.startsWith('Saving')?'Saving…':saved?'Saved':'Save';
    const cannonLabel=state?.cannon?.nearby===false?'Visit cannon':'Cannon';
    const toggle=which=>{setHoverColour(null);setPopup(popup===which?null:which);};
    useLayoutEffect(()=>{
        if(popup)root.current?.querySelector('.bb-popup button:not(:disabled)')?.focus({preventScroll:true});
    },[popup]);
    const rows=(state?.rows||[]).slice(0,64).map((row,index)=>({row,index})).filter(({row})=>
        state?.mode!=='catalog'||!search.trim()||text(row.label).toLocaleLowerCase().includes(search.trim().toLocaleLowerCase()));
    const scrollPieces=direction=>bar.current?.scrollBy({left:direction*240,behavior:state?.reducedMotion||environment.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches?'instant':'smooth'});
    const modalRow=(row,index)=>{
        const Icon=/Save/.test(row.label)?Save:/controls|settings|Reset|sensitivity|motion|Text|Contrast|look|Invert|deadzone/i.test(row.label)?Settings2:/play|help/i.test(row.label)?HelpCircle:ChevronRight;
        return <Button key={row.intent} icon={Icon} disabled={busy||!row.enabled} data-row={index} aria-current={state.menuSelected===index?'true':undefined}
            onFocus={()=>{if(state.menuSelected!==index)bridge.current?.action(26,row.intent,state.menuToken);}}
            onClick={()=>act(10,row.intent,state.menuToken,false)}>{text(row.label)}{row.detail&&<small>{text(row.detail)}</small>}</Button>;
    };
    return <div ref={root} id="build-ui" data-voxy-ui="true" class="bb-root" aria-label="Building controls"
        style={{'--bb-scale':[1,1.25,1.5].includes(state?.textScale)?state.textScale:1}}
        data-contrast={state?.highContrast===true} data-motion={state?.reducedMotion===true?'reduced':'normal'} data-mode={state?.mode||'loading'}
        onPointerDown={event=>event.stopPropagation()} onKeyDown={keyDown} onKeyUp={event=>event.stopPropagation()}
        onFocusIn={()=>bridge.current?.own(true)} onFocusOut={event=>{if(!root.current?.contains(event.relatedTarget))bridge.current?.own(false);}}>
        <nav inert={modal||undefined} class="bb-top-actions" aria-label="Game controls">
            {!modal&&!state?.cannon?.active&&<Button icon={Bike} shortcut="M" aria-label={state?.riding?'Get off motorbike':'Motorbike'} title={state?.riding?'Get off · M':'Motorbike · M'} disabled={busy} onClick={()=>act(32)}>{state?.riding?'Get off':'Motorbike'}</Button>}
            {!modal&&state?.cannon?.available&&<Button icon={Compass} shortcut="C" title={state.cannon.active?'Leave cannon · C':`${cannonLabel} · C`} aria-label={state.cannon.active?'Leave cannon':cannonLabel} disabled={busy||state.cannon?.wallBusy||state.cannon?.awaitingHit} onClick={()=>act(33)}>{state.cannon.active?'Leave cannon':cannonLabel}</Button>}
            <Button icon={saved?Check:Save} className="bb-save" data-saved={saved} aria-label="Save build" disabled={busy} onClick={()=>act(8)} title={text(state?.saveStatus)}>{saveLabel}</Button>
            <Button icon={Menu} aria-label="Open menu" title="Menu" disabled={busy||modal} onClick={()=>act(31)} className="bb-icon"/>
        </nav>
        {state?.failed?<section class="bb-error" role="alert"><TriangleAlert/><strong>Building controls are unavailable.</strong><p>{text(state.message)}</p><p>Reload to try again. Saved builds are kept.</p></section>:
        !state?<div class="bb-loading" role="status">Opening your brick box…</div>:<>
            <div class="bb-toast" role="status">{text(state.saveFailure)||toast}</div>
            {!modal&&state.mode==='build'&&<section class="bb-dock" aria-label="Brick hotbar">
                <div class="bb-hotbar-row">
                    <Button icon={ChevronLeft} className="bb-icon bb-scroll" aria-label="Scroll pieces left" onClick={()=>scrollPieces(-1)}/>
                    <div class="bb-hotbar" ref={bar} role="group" aria-label="Choose a piece — scroll to cycle" onWheel={cycle} onKeyDown={navigatePieces}>
                        {order.map(id=><button type="button" key={id} class="bb-piece" aria-label={names[id-1]} title={names[id-1]} aria-pressed={state.piece===id} disabled={busy} onClick={()=>act(2,id)}><img src={imageFor(id)} alt="" draggable={false}/><span>{names[id-1].replace("Brick ","").replace("Open doorway","Doorway").replace("Hinged door","Door")}</span>{state.piece===id&&<Check class="bb-piece-check" size={12}/>}</button>)}
                    </div>
                    <Button icon={ChevronRight} className="bb-icon bb-scroll" aria-label="Scroll pieces right" onClick={()=>scrollPieces(1)}/>
                </div>
                <div class="bb-dock-footer">
                    <Button icon={Footprints} className="bb-icon bb-explore" title="Explore · B" aria-label="Walk around" disabled={busy} onClick={()=>act(22)}/>
                    <Button icon={RotateCw} className="bb-icon" aria-label="Rotate piece" title="Rotate · R" disabled={busy} onClick={()=>act(3)}/>
                    <Button icon={Undo2} className="bb-icon" aria-label="Undo last piece" title="Undo · Ctrl Z" disabled={busy||!state.canUndo} onClick={()=>act(6)}/>
                    <Button icon={Blocks} className="bb-icon" aria-label="All pieces" title="All pieces · Tab" disabled={busy} onClick={()=>act(23)}/>
                    <div class="bb-finish-tools">
                        <button type="button" class="bb-button bb-icon bb-colour-toggle" title={`Colour · ${selectedColour[0]}`} data-popup-toggle="colour" aria-label={`Choose colour · ${selectedColour[0]}`} aria-expanded={popup==='colour'} aria-controls="bb-colour-popup" disabled={busy} onClick={()=>toggle('colour')}><span class="bb-colour-dot" style={{background:selectedColour[2]}}/></button>
                        <Button icon={Settings2} className="bb-icon" data-popup-toggle="tools" aria-label="Building tools" title="More building tools" aria-expanded={popup==='tools'} aria-controls="bb-tools-popup" disabled={busy} onClick={()=>toggle('tools')}/>
                    </div>
                </div>
                <div class="bb-aim-hint" role="status"><strong>{names[state.piece-1]}</strong><span data-valid={state.valid}>{state.valid?'Click to place':text(state.previewReason)||'Aim at a surface'}</span></div>
                {state.swimming&&<div class="bb-hints"><span>Space · rise</span><span>X · dive</span><span>Right-drag · steer</span></div>}
                {popup==='colour'&&<section id="bb-colour-popup" class="bb-popup bb-colours" aria-label="Colour palette">
                    <Button icon={X} className="bb-icon bb-colour-close" aria-label="Close colour palette" onClick={()=>{setPopup(null);root.current?.querySelector('[data-popup-toggle="colour"]')?.focus();}}/>
                    <div class="bb-colour-caption" aria-hidden="true"><Palette size={20}/><div><small>BRICK COLOUR</small><strong>{hoverColour||selectedColour[0]}</strong></div></div>
                    <div class="bb-colour-orbit" role="group" aria-label="Colour for new pieces">
                        {colours.map(([name,value,colour])=><button type="button" key={value} class="bb-swatch" data-original={value===0} style={{'--swatch':colour}}
                            aria-label={`${name} colour`} title={name} aria-pressed={(state.paint||0)===value} disabled={busy||!state.colourAvailable}
                            onPointerEnter={()=>setHoverColour(name)} onPointerLeave={()=>setHoverColour(null)} onFocus={()=>setHoverColour(name)} onBlur={()=>setHoverColour(null)}
                            onClick={()=>{if(act(30,value)){setPopup(null);setHoverColour(null);}}}><PlasticBrick original={value===0}/><span class="bb-swatch-name">{name}</span>{(state.paint||0)===value&&<Check class="bb-paint-check" size={14}/>}</button>)}
                    </div>
                    <small class="bb-paint-note">For your next bricks</small>
                    {!state.colourAvailable&&<small class="bb-colour-unavailable">Colour unavailable for this piece</small>}
                </section>}
                {popup==='tools'&&<section id="bb-tools-popup" class="bb-popup bb-tool-menu" aria-label="Building tools">
                    <div class="bb-popup-title"><div><strong>Building tools</strong></div><Button icon={X} className="bb-icon" aria-label="Close building tools" onClick={()=>{setPopup(null);root.current?.querySelector('[data-popup-toggle="tools"]')?.focus();}}/></div>
                    <Button icon={RotateCw} shortcut="R" disabled={busy} onClick={()=>act(3)}>Rotate</Button>
                    <Button icon={Undo2} shortcut="Ctrl Z" disabled={busy||!state.canUndo} onClick={()=>act(6)}>Undo last piece</Button>
                    <Button icon={Trash2} disabled={busy||!state.canRemove} onClick={()=>act(5)}>Remove aimed piece</Button>
                    <div class="bb-height"><Button icon={ArrowDown} disabled={busy} onClick={()=>act(12,-1)}>Lower</Button><Button icon={ArrowUp} disabled={busy} onClick={()=>act(12,1)}>Raise</Button></div>
                    <Button icon={Blocks} shortcut="Tab" disabled={busy} onClick={()=>act(23)}>All pieces</Button>
                    <Button icon={HelpCircle} disabled={busy} onClick={()=>act(29,1)}>How to build</Button>
                    <small>{Number(state.parts)||0} / 1,024 pieces</small>
                </section>}
            </section>}
            {!modal&&state.cannon?.active&&<section class="bb-walk-dock" aria-label="Cannon controls">
                <span>{state.cannon.inspectingWall?'Wall close-up · A/D or W/S to return to aiming':'A/D · turn　 W/S · elevation'}</span>
                <Button shortcut="Space" disabled={busy||!state.cannon.ready||state.cannon.awaitingHit} onClick={()=>act(34)}>Fire</Button>
                <Button disabled={busy||(!state.cannon.wallReleased&&!state.cannon.wallFailed)} onClick={()=>act(36)}>Rebuild wall</Button>
                <span role="status">{state.cannon.wallFailed?(text(state.cannon.wallMessage)||'Wall could not settle · rebuild to retry'):state.cannon.wallBusy?'Let the pieces settle':state.cannon.awaitingHit?'Shot away…':!state.cannon.ready?(text(state.cannon.error)||'Preparing cannon…'):state.cannon.wallReleased&&state.cannon.impacts>0?'Hit · bricks freed':'Aim at the stone wall · C: leave'}</span>
                {state.cannon.wallReleased&&<span>Damage lasts this session · Rebuild to save</span>}
            </section>}
            {!modal&&state.riding&&<section class="bb-walk-dock" aria-label="Motorbike controls"><span>W/S · drive &amp; reverse　 A/D · steer　 Space · brake</span></section>}
            {!modal&&!state.riding&&!state.cannon?.active&&state.mode==='explore'&&<section class="bb-walk-dock bb-throw-dock">{state.swimming?<span>Space · rise　 X · dive　 Right-drag · steer</span>:<Button icon={Hand} disabled={busy} onClick={()=>act(7)}>{text(state.interaction)||'Use nearby'}</Button>}<Button icon={Plus} shortcut="B" disabled={busy} onClick={()=>act(21)}>Build</Button></section>}
            {modal&&<div class={`bb-modal-shade ${state.mode==='catalog'?'bb-catalog-shade':''}`} onClick={event=>{if(event.target===event.currentTarget)act(20);}}><section ref={dialog} role="dialog" aria-modal="true" aria-labelledby="bb-dialog-title" class={`bb-dialog ${state.mode==='catalog'?'bb-catalog':''}`}>
                <div class="bb-dialog-head"><h1 id="bb-dialog-title">{state.mode==='pause'?'Paused':state.mode==='catalog'?'Parts box':text(state.menuTitle)||'Your brick box'}</h1>
                    {state.mode==='catalog'&&<label class="bb-search"><Search size={16}/><input type="search" aria-label="Search pieces in this category" placeholder="Find a piece…" value={search} onInput={event=>setSearch(event.currentTarget.value)}/></label>}
                    <Button icon={X} className="bb-icon" aria-label="Close menu" disabled={busy} onClick={()=>act(20)}/>
                </div>
                <p class="bb-dialog-intro">{['pause','catalog'].includes(state.mode)?'':text(state.menuText)}</p>
                {state.mode==='catalog'&&<div class="bb-filters" role="group" aria-label="Piece categories">{['Structure','Bricks','Furniture'].map((name,i)=><Button key={name} icon={[House,Blocks,Armchair][i]} aria-pressed={state.catalogCategory===i} disabled={busy} onClick={()=>{setSearch('');act(13,i+1,null,false);}}>{name}</Button>)}</div>}
                <div class="bb-dialog-rows">{rows.map(({row,index:i})=>state.mode==='catalog'&&row.pieceKind?<button type="button" class="bb-catalog-piece" key={row.intent} aria-label={text(row.label)} aria-pressed={state.piece===row.pieceKind} disabled={busy||!row.enabled} data-row={i} aria-current={state.menuSelected===i?'true':undefined}
                    onFocus={()=>{if(state.menuSelected!==i)bridge.current?.action(26,row.intent,state.menuToken);}} onClick={()=>act(10,row.intent,state.menuToken)}><span class="bb-piece-plinth"><img src={imageFor(row.pieceKind)} alt="" draggable={false}/>{state.piece===row.pieceKind&&<Check size={13} class="bb-catalog-check" aria-hidden="true"/>}</span><span>{text(row.label)}</span>{!row.enabled&&<small>Unavailable</small>}</button>:modalRow(row,i))}</div>
                {state.mode==='catalog'&&rows.length===0&&<div class="bb-empty"><Search size={28}/><strong>No pieces found</strong><p>Try another name or category.</p><Button onClick={()=>{setSearch('');root.current?.querySelector('input[type="search"]')?.focus();}}>Clear search</Button></div>}
                {state.mode!=='catalog'&&<p class="bb-dialog-status" role="status">{text(state.mode==='settings'?state.preferencesStatus:state.saveStatus)}</p>}
                {state.mode==='pause'&&<a class="bb-new-world" href="?experience=build&new=1">Start a fresh build <ChevronRight size={14}/></a>}
            </section></div>}
        </>}
    </div>;
}
export function install(engine,environment=globalThis){
    if(!engine?._adventure_action||!environment.document)return;
    const host=environment.document.createElement('div');environment.document.body.append(host);
    const hadAdventure=environment.document.body.classList.contains('voxy-adventure');
    environment.document.body.classList.add('voxy-adventure','voxy-build');
    render(<App engine={engine} environment={environment}/>,host);
    return {cleanup(){render(null,host);host.remove();environment.document.body.classList.remove('voxy-build');if(!hadAdventure)environment.document.body.classList.remove('voxy-adventure');}};
}
