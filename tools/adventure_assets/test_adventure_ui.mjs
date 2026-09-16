import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {testDOM} from '../../scripts/cove_ui_test_dom.mjs';
const require=createRequire(import.meta.url),api=require('../../web/adventure_ui.js');
function fixture(before){
    const f=testDOM();let serial=0;f.timers=new Map();
    f.environment.setInterval=(fn,ms)=>{assert.equal(ms,100);f.timers.set(++serial,fn);return serial;};
    f.environment.clearInterval=id=>f.timers.delete(id);
    f.state={ready:true,build:false,piece:1,mode:'explore',menuToken:1,menuSelected:0,observation:'1',status:'Choose a home site.',
        health:100,combatLabel:'Explore the trail.',staffEquipped:false,attackReady:true,dodgeReady:true,
        valid:false,wood:90,stone:80,scrap:16,rows:[],saveStatus:'Unsaved changes',dirty:true};
    f.actions=[];f.refuse=false;f.readError=false;
    f.engine={_get_adventure_state_json(){if(f.readError)throw Error();return JSON.stringify(f.state);},
        // The installed EMSCRIPTEN_KEEPALIVE adventure_action export is void.
        UTF8ToString:v=>v,_adventure_action(a,v){f.actions.push([a,v]);return f.refuse?0:undefined;}};
    f.add('canvas','voxy-canvas');before?.(f);f.ui=api.install(f.engine,f.environment);
    assert.deepEqual(f.actions,[[19,1]],'host ownership attaches once');f.actions=[];
    f.el=id=>f.document.getElementById(id);f.tick=()=>f.ui.refresh();
    f.mode=(mode,rows=[])=>{Object.assign(f.state,{mode,build:mode==='build'||mode==='catalog',rows});++f.state.menuToken;f.tick();};
    f.rows=()=>f.el('adventure-menu-rows').querySelectorAll('button');return f;
}
const row=(intent,label='Continue',enabled=true,extra={})=>({intent,label,enabled,...extra});
function focusEvents(f){
    // Opt into browser focus events for ownership/selection tests without
    // presenting this small DOM model as a layout or native-input simulation.
    const install=element=>{
        const focus=element.focus;
        element.focus=function(...args){
            if(f.document.activeElement===this)return;
            const before=f.document.activeElement;
            before?.dispatchEvent(new f.Event('focusout',{bubbles:true,relatedTarget:this}));
            focus.apply(this,args);this.dispatchEvent(new f.Event('focus'));
            this.dispatchEvent(new f.Event('focusin',{bubbles:true,relatedTarget:before}));
        };
        return element;
    };
    const create=f.document.createElement;
    f.document.createElement=tag=>install(create(tag));install(f.document.getElementById('voxy-canvas'));
}
test('Explore has a sparse play bar; polling never runs gameplay commands',()=>{
    const f=fixture();assert(f.el('adventure-builder').hidden);assert(f.el('adventure-menu-panel').hidden);
    assert(!f.el('adventure-playbar').hidden);assert(f.el('adventure-stock').hidden);
    f.tick();f.tick();assert.deepEqual(f.actions,[]);assert.equal(f.el('adventure-stock').textContent,'Wood 90 · Stone 80 · Scrap 16');
    f.ui.cleanup();assert.deepEqual(f.actions,[[19,0]]);assert.equal(f.timers.size,0);assert.equal(f.el('adventure-ui'),null);
});
test('inert DOM observation requires local opt-in and shares only the validated UI read',()=>{
    for(const href of [undefined,'http://127.0.0.1:42752/','http://localhost/?adventureObserve=0',
        'https://example.com/?adventureObserve=1','https://localhost.example.com/?adventureObserve=1',
        'file:///tmp/index.html?adventureObserve=1']){
        const f=fixture(f=>{if(href)f.environment.location={href};});
        assert.equal(f.el('adventure-observation'),null,href);f.ui.cleanup();
    }
    for(const host of ['localhost','127.0.0.1','[::1]']){
        let reads=0;const f=fixture(f=>{
            f.environment.location={href:`http://${host}:42752/?experience=adventure&adventureObserve=1`};
            const read=f.engine._get_adventure_state_json;
            f.engine._get_adventure_state_json=()=>{++reads;return read();};
        });
        const node=f.el('adventure-observation');assert(node);assert.equal(node.tagName,'SCRIPT');
        assert.equal(node.getAttribute('type'),'application/json');assert.deepEqual(JSON.parse(node.textContent),f.state);
        assert.equal(reads,1,'observation does not reread the runtime');
        f.state.player={x:12,y:-3,z:45};f.state.costText='</script><script>bad()</script>';
        assert.equal(JSON.parse(node.textContent).player,undefined,'no extra observation timer');
        f.tick();assert.equal(reads,2);assert.deepEqual(JSON.parse(node.textContent),f.state);
        assert.equal(node.children.length,0,'runtime strings remain inert text');assert.deepEqual(f.actions,[]);
        f.state.mode='invalid';f.tick();assert.equal(node.textContent,'null','invalid snapshots cannot leave a stale accepted observation');
        f.state.mode='explore';f.tick();assert.deepEqual(JSON.parse(node.textContent),f.state);
        f.ui.cleanup();assert.equal(f.el('adventure-observation'),null);const after=reads;f.tick();assert.equal(reads,after);
    }
});
test('named play actions enter explicit modes and do not fabricate accepted state',()=>{
    const f=fixture();f.el('adventure-build').click();f.el('adventure-bag').click();f.el('adventure-journal').click();
    assert.deepEqual(f.actions,[[21,0],[24,0],[16,0]]);assert.equal(f.state.build,false);assert(f.el('adventure-builder').hidden);
    f.ui.cleanup();
});
test('Use releases world focus for same-mode actions and later dialogue still receives focus',()=>{
    const f=fixture(focusEvents);const use=f.el('adventure-interact');
    use.focus();use.click();assert.deepEqual(f.actions,[[15,1],[7,0],[15,0]]);
    assert.equal(f.document.activeElement,f.el('voxy-canvas'));assert.equal(f.state.mode,'explore');
    ++f.state.observation;f.state.status='Materials gathered.';f.tick();
    assert.equal(f.document.activeElement,f.el('voxy-canvas'),'same-mode gathering must not freeze movement');
    use.focus();use.click();assert.deepEqual(f.actions.slice(-3),[[15,1],[7,0],[15,0]]);
    f.mode('dialogue',[row(91,'Speak to Moss')]);
    assert.equal(f.document.activeElement,f.rows()[0]);assert.deepEqual(f.actions.at(-1),[15,1]);
    const count=f.actions.filter(([a])=>a===7).length;use.click();
    assert.equal(f.actions.filter(([a])=>a===7).length,count,'hidden Explore controls cannot interrupt dialogue');
    f.ui.cleanup();
});
test('Open/Close door interaction waits for observation and rejects the second physical click',()=>{
    const f=fixture(focusEvents);f.state.interaction='Open door';f.tick();const use=f.el('adventure-interact');
    assert.equal(use.textContent,'E · Open door');use.focus();use.click();use.click();
    assert.deepEqual(f.actions.filter(([a])=>a===7),[[7,0]]);assert(use.disabled);
    assert.equal(f.document.activeElement,f.el('voxy-canvas'));
    f.state.interaction='Close door';++f.state.observation;f.tick();assert.equal(use.textContent,'E · Close door');assert(!use.disabled);
    use.dispatchEvent(new f.Event('click',{detail:2,bubbles:true}));assert.deepEqual(f.actions.filter(([a])=>a===7),[[7,0]]);
    use.dispatchEvent(new f.Event('click',{detail:1,bubbles:true}));assert.deepEqual(f.actions.filter(([a])=>a===7),[[7,0],[7,0]]);
    f.mode('guide',[row(971,'Return to adventure')]);use.click();assert.equal(f.actions.filter(([a])=>a===7).length,2);
    f.ui.cleanup();use.click();assert.equal(f.actions.filter(([a])=>a===7).length,2);
});
test('combat controls require accepted health, equipment, readiness and Explore mode',()=>{
    const f=fixture();const attack=f.el('adventure-attack'),dodge=f.el('adventure-dodge');
    assert.equal(f.el('adventure-health').textContent,'Health 100 / 100');
    assert.equal(f.el('adventure-health-meter').getAttribute('value'),'100');
    assert.equal(f.el('adventure-threat').textContent,'Explore the trail.');
    assert(attack.disabled);assert(!dodge.disabled);attack.click();assert.deepEqual(f.actions,[]);
    f.state.staffEquipped=true;f.state.attackReady=false;f.state.dodgeReady=false;f.tick();
    attack.click();dodge.click();assert.deepEqual(f.actions,[]);
    f.state.attackReady=true;f.state.dodgeReady=true;f.mode('build');attack.click();dodge.click();assert.deepEqual(f.actions,[]);
    f.mode('dialogue',[row(91)]);attack.click();dodge.click();assert.deepEqual(f.actions,[]);
    f.mode('explore');assert(!attack.disabled&&!dodge.disabled);
    for(const health of [-1,101,NaN,50.5,undefined]){
        f.state.health=health;f.tick();assert(attack.disabled&&dodge.disabled);
        assert.equal(f.el('adventure-health').textContent,'Health —');assert(f.el('adventure-health-meter').hidden);
    }
    f.ui.cleanup();
});
test('combat dispatch immediately releases UI ownership and waits for accepted cooldown state',()=>{
    const f=fixture(focusEvents);f.state.staffEquipped=true;f.tick();
    const attack=f.el('adventure-attack');attack.focus();assert.deepEqual(f.actions,[[15,1]]);
    attack.click();assert.deepEqual(f.actions,[[15,1],[27,0],[15,0]]);
    assert.equal(f.document.activeElement,f.el('voxy-canvas'));assert(attack.disabled&&f.el('adventure-dodge').disabled);
    attack.click();f.el('adventure-dodge').click();assert.equal(f.actions.length,3,'unchanged accepted snapshot cannot dispatch again');
    assert.equal(f.state.attackReady,true,'dispatch never invents accepted cooldowns');
    ++f.state.observation;f.state.attackReady=false;f.tick();assert(attack.disabled);assert(!f.el('adventure-dodge').disabled);
    const dodge=f.el('adventure-dodge');dodge.focus();dodge.click();
    assert.deepEqual(f.actions.slice(-3),[[15,1],[28,0],[15,0]]);assert.equal(f.document.activeElement,f.el('voxy-canvas'));
    ++f.state.observation;f.state.dodgeReady=false;f.tick();assert(dodge.disabled);f.ui.cleanup();
});
test('defeat exposes safe recovery and does not change health until runtime accepts it',()=>{
    const f=fixture(focusEvents);f.state.health=0;f.state.combatLabel='Return home to recover.';f.tick();
    const recover=f.el('adventure-revive');assert(!recover.hidden&&!recover.disabled);
    assert(f.el('adventure-attack').hidden&&f.el('adventure-dodge').hidden);
    assert(f.el('adventure-exploration-actions').hidden);f.el('adventure-build').click();assert.deepEqual(f.actions,[]);
    recover.focus();recover.click();assert.deepEqual(f.actions,[[15,1],[11,0],[15,0]]);
    assert.equal(f.document.activeElement,f.el('voxy-canvas'));assert.equal(f.state.health,0);
    recover.click();assert.equal(f.actions.length,3);
    ++f.state.observation;f.state.health=100;f.tick();assert(recover.hidden);assert(!f.el('adventure-exploration-actions').hidden);
    f.mode('pause',[row(100)]);recover.click();assert.equal(f.actions.filter(([a])=>a===11).length,1);f.ui.cleanup();
});
test('unavailable or refused combat leaves controls safe and returns keyboard focus',()=>{
    const f=fixture(focusEvents);f.state.staffEquipped=true;f.tick();f.refuse=true;
    const attack=f.el('adventure-attack');attack.focus();attack.click();
    assert.equal(f.document.activeElement,f.el('voxy-canvas'));assert.equal(f.state.health,100);
    assert.deepEqual(f.actions,[[15,1],[27,0],[15,0]]);
    f.mode('build');f.readError=true;f.tick();
    assert(!f.el('adventure-playbar').hidden);assert(!f.el('adventure-status').hidden);assert(f.el('adventure-combat').hidden);
    assert(f.el('adventure-attack').disabled&&f.el('adventure-dodge').disabled&&f.el('adventure-revive').disabled);
    const count=f.actions.length;attack.click();assert.equal(f.actions.length,count);f.ui.cleanup();
});
test('Build only enables placement for an authoritative valid preview',()=>{
    const f=fixture();f.el('adventure-place').click();assert.deepEqual(f.actions,[]);
    f.mode('build');f.state.costText='4 wood';f.state.previewReason='Out of reach';f.tick();
    assert(f.el('adventure-playbar').hidden);assert(!f.el('adventure-stock').hidden);
    f.el('adventure-place').click();assert.deepEqual(f.actions,[]);assert.equal(f.el('adventure-placement').textContent,'! Out of reach');
    f.state.valid=true;f.tick();for(const id of ['place','rotate','lower','raise','remove','undo','done'])f.el('adventure-'+id).click();
    assert.deepEqual(f.actions,[[4,0],[3,0],[12,-1],[12,1],[5,0],[6,0],[22,0]]);assert.equal(f.state.wood,90);f.ui.cleanup();
});
test('Hinged door15 uses the installed picture and paid authoritative catalog intent',()=>{
    const f=fixture();f.mode('catalog',[row(981,'Hinged door',true,{pieceKind:15,blueprintKind:0,detail:'6 wood / 2 scrap'})]);
    const door=f.rows()[0];assert.equal(door.querySelector('img').getAttribute('src'),'adventure_piece_15.svg');
    assert.equal(door.querySelector('small').textContent,'6 wood / 2 scrap');door.click();door.click();assert.deepEqual(f.actions,[[10,981]]);
    Object.assign(f.state,{piece:15,blueprintKind:0,selected:'Hinged door',costText:'6 wood / 2 scrap',valid:false});f.mode('build');
    assert.equal(f.el('adventure-selected').textContent,'Hinged door');assert.equal(f.el('adventure-piece-image').getAttribute('src'),'adventure_piece_15.svg');
    assert.equal(f.el('adventure-place').textContent,'Place');assert(f.el('adventure-place').disabled);
    assert.equal(f.state.wood,90);assert.equal(f.state.scrap,16);door.click();assert.deepEqual(f.actions,[[10,981]]);
    f.state.piece=16;f.tick();assert(f.el('adventure-place').disabled);assert(f.el('adventure-builder').hidden);f.ui.cleanup();
});
test('starter room selection is a paid preview and shared menu intent',()=>{
    const f=fixture();f.mode('catalog',[row(700,'Starter room',true,{pieceKind:0,detail:'70 wood · 16 stone · 8 scrap'})]);
    const blueprint=f.rows()[0];assert.equal(blueprint.className,'adventure-blueprint');
    assert.equal(blueprint.querySelector('small').textContent,'70 wood · 16 stone · 8 scrap');
    assert.equal(blueprint.querySelector('img'),null,'room is not mislabeled with an individual part picture');
    blueprint.click();blueprint.click();assert.deepEqual(f.actions,[[10,700]]);
    Object.assign(f.state,{piece:0,selected:'Starter room',costText:'70 wood · 16 stone · 8 scrap'});f.mode('build');
    blueprint.click();assert.deepEqual(f.actions,[[10,700]],'old catalog button cannot select a replacement menu command');
    assert.equal(f.el('adventure-place').textContent,'Place room');assert.equal(f.el('adventure-cost').textContent,f.state.costText);
    assert(f.el('adventure-piece-image').hidden);f.el('adventure-place').click();assert.equal(f.actions.length,1);
    f.state.valid=true;f.tick();f.el('adventure-place').click();assert.deepEqual(f.actions.at(-1),[4,0]);f.ui.cleanup();
});
test('building tray exposes Starter room without Pause and selects only a paid preview',()=>{
    const f=fixture();const starter=f.el('adventure-starter');starter.click();assert.deepEqual(f.actions,[]);
    f.mode('build');assert(!f.el('adventure-builder').hidden);assert(f.el('adventure-menu-panel').hidden);
    assert(f.el('adventure-builder').contains(starter));assert(!starter.hidden&&!starter.disabled);
    assert.equal(starter.querySelector('small').textContent,'Bed, chest & bench');
    assert.equal(starter.getAttribute('aria-pressed'),'false');starter.click();
    assert.deepEqual(f.actions,[[14,0]],'shortcut selects the existing preview, never places or spends locally');
    assert.equal(f.state.piece,1);assert.equal(f.state.wood,90);f.el('adventure-place').click();assert.equal(f.actions.length,1);
    Object.assign(f.state,{piece:0,selected:'Starter room',costText:'Actual room cost'});f.tick();
    assert.equal(starter.getAttribute('aria-pressed'),'true');assert(!f.el('adventure-starter-hint').hidden);
    assert.equal(f.el('adventure-starter-hint').textContent,'After placing, use the bed to set your home.');
    assert.equal(f.el('adventure-cost').textContent,'Actual room cost');assert(f.el('adventure-place').disabled);
    f.readError=true;f.tick();starter.click();assert.equal(f.actions.length,1);f.ui.cleanup();
});
test('paid stone step uses its Pier picture, three-piece count and captured catalog intent',()=>{
    const f=fixture();f.state.blueprintKind=0;
    f.mode('catalog',[row(910,'Wide stone step',true,{pieceKind:14,blueprintKind:2,detail:'3 Piers / 6 stone'})]);
    const old=f.rows()[0];assert.equal(old.className,'adventure-blueprint');
    assert.equal(old.querySelector('img').getAttribute('src'),'adventure_piece_14.svg');
    assert.equal(old.querySelector('span').textContent,'3-piece blueprint');
    assert.equal(old.querySelector('small').textContent,'3 Piers / 6 stone');
    assert(!old.textContent.includes('Room blueprint'));assert(!old.textContent.includes('Bed, chest & bench'));
    old.click();old.click();assert.deepEqual(f.actions,[[10,910]],'selection uses only the published command');
    assert.equal(f.state.piece,1);assert.equal(f.state.stone,80,'selection cannot invent paid placement');
    Object.assign(f.state,{piece:14,blueprintKind:2,selected:'Wide stone step',costText:'6 stone',valid:true});
    f.mode('build');old.click();assert.deepEqual(f.actions,[[10,910]],'old catalog command cannot fire after selection');
    assert.equal(f.el('adventure-selected').textContent,'Wide stone step');
    assert.equal(f.el('adventure-blueprint-count').textContent,'3 pieces');assert(!f.el('adventure-blueprint-count').hidden);
    assert.equal(f.el('adventure-piece-image').getAttribute('src'),'adventure_piece_14.svg');assert(!f.el('adventure-piece-image').hidden);
    assert.equal(f.el('adventure-place').textContent,'Place step');assert.equal(f.el('adventure-cost').textContent,'6 stone');
    assert(f.el('adventure-starter-hint').hidden);assert(f.el('adventure-starter').querySelector('small').hidden);
    assert.equal(f.el('adventure-starter').getAttribute('aria-pressed'),'false');
    f.el('adventure-place').click();assert.deepEqual(f.actions.at(-1),[4,0]);assert.equal(f.state.stone,80);
    Object.assign(f.state,{piece:14,blueprintKind:0,selected:'Pier',costText:'2 stone'});f.tick();
    assert.equal(f.el('adventure-place').textContent,'Place');assert(f.el('adventure-blueprint-count').hidden);
    assert(!f.el('adventure-starter').querySelector('small').hidden);
    f.el('adventure-starter').click();assert.deepEqual(f.actions.at(-1),[14,0]);f.ui.cleanup();
});
test('locked blueprint details remain readable while old and disabled commands cannot activate',()=>{
    const f=fixture();f.mode('catalog',[row(911,'Wide stone step',true,{pieceKind:14,blueprintKind:2,detail:'3 Piers / 6 stone'})]);
    const old=f.rows()[0];
    f.mode('catalog',[row(912,'Wide stone step',false,{pieceKind:14,blueprintKind:2,detail:'Finish The Surveyor’s Notes with Moss.'}),
        row(913,'Close',true,{pieceKind:0,blueprintKind:0})]);
    const locked=f.rows()[0];assert(locked.disabled);
    assert.equal(locked.querySelector('img').getAttribute('src'),'adventure_piece_14.svg');
    assert.equal(locked.querySelector('small').textContent,'Finish The Surveyor’s Notes with Moss.');
    old.click();locked.click();assert.deepEqual(f.actions,[]);
    assert(!f.rows()[1].disabled,'generic menu rows need no part or blueprint thumbnail');
    assert.equal(f.rows()[1].querySelector('img'),null);assert(!f.rows()[1].textContent.includes('Room blueprint'));
    f.rows()[1].click();assert.deepEqual(f.actions,[[10,913]]);f.ui.cleanup();
});
test('explicit unknown or mismatched blueprint metadata cannot offer a misleading placement',()=>{
    const f=fixture();f.state.valid=true;
    for(const [blueprintKind,piece]of [[3,14],[2,0],[1,14],[0,0]]){
        Object.assign(f.state,{blueprintKind,piece});f.mode('build');
        assert(f.el('adventure-place').disabled);assert(f.el('adventure-builder').hidden);
        f.el('adventure-place').click();assert.deepEqual(f.actions,[]);
    }
    Object.assign(f.state,{blueprintKind:0,piece:1});
    f.mode('catalog',[row(920,'Unknown',true,{pieceKind:14,blueprintKind:3}),row(921,'Wrong thumbnail',true,{pieceKind:12,blueprintKind:2})]);
    for(const b of f.rows()){assert(b.disabled);b.click();}assert.deepEqual(f.actions,[]);f.ui.cleanup();
});
test('catalog categories and actual piece thumbnail IDs match installed piece kinds',()=>{
    const f=fixture();f.state.catalogCategory=2;f.mode('catalog',[row(811,'Bed',true,{pieceKind:11}),row(812,'Chest',true,{pieceKind:12})]);
    assert(f.el('adventure-builder').hidden);assert(!f.el('adventure-menu-panel').hidden);
    assert.equal(f.rows()[1].querySelector('img').getAttribute('src'),'adventure_piece_12.svg');
    assert.equal(f.el('adventure-category-2').getAttribute('aria-pressed'),'true');
    f.el('adventure-category-0').click();assert.deepEqual(f.actions,[[13,1]]);f.ui.cleanup();
});
test('pending row activation cannot Accept then Complete on a double click',()=>{
    const f=fixture();f.mode('dialogue',[row(91,'Accept quest')]);const old=f.rows()[0];old.click();old.click();f.tick();old.click();
    assert.deepEqual(f.actions,[[10,91]]);assert(f.rows()[0].disabled);
    f.state.rows=[row(92,'Complete quest')];++f.state.menuToken;++f.state.observation;f.tick();
    old.click();assert.deepEqual(f.actions,[[10,91]],'detached captured command does not target replacement row');
    f.rows()[0].click();assert.deepEqual(f.actions,[[10,91],[10,92]]);f.ui.cleanup();
});
test('unchanged polling preserves row focus; publishing new choices focuses only on mode entry',()=>{
    const f=fixture();f.mode('dialogue',[row(71),row(72,'Leave')]);const selected=f.rows()[1];selected.focus();const count=selected.focusCount;
    f.tick();f.tick();assert.equal(f.rows()[1],selected);assert.equal(selected.focusCount,count);assert.equal(f.document.activeElement,selected);
    f.mode('explore');assert.equal(f.document.activeElement,f.el('voxy-canvas'));f.ui.cleanup();
});
test('disabled or malformed rows never activate and all 64 bounded choices remain reachable',()=>{
    const f=fixture();f.mode('chest',Array.from({length:80},(_,i)=>row(i+1,'Item '+i,i!==3)));
    assert.equal(f.rows().length,64);f.rows()[3].click();assert.deepEqual(f.actions,[]);f.rows()[63].click();assert.deepEqual(f.actions,[[10,64]]);
    f.mode('dialogue',[row(-1,'Bad'),row(2147483648,'Overflow'),{label:'No intent',enabled:true}]);
    for(const b of f.rows()){assert(b.disabled);b.click();}assert.equal(f.actions.length,1);f.ui.cleanup();
});
test('Journal displays the authoritative objective and Dialogue isolates its text',()=>{
    const f=fixture();f.state.quest={phase:'active',objective:'Build a sheltered bed and use it.'};f.mode('journal',[row(5,'Return')]);
    assert.equal(f.el('adventure-dialogue-text').textContent,f.state.quest.objective);
    f.state.menuText='Welcome. You can build a home nearby.';f.state.menuTitle='Moss';f.state.dialogue={role:'Builder'};
    f.mode('dialogue',[row(6,'Tell me more')]);assert.equal(f.el('adventure-menu-title').textContent,'Moss');
    assert.equal(f.el('adventure-dialogue-role').textContent,'Builder');assert(f.el('adventure-quest').hidden);assert(f.el('adventure-playbar').hidden);f.ui.cleanup();
});
test('Guide is one focused sheet with literal runtime text and captured navigation intents',()=>{
    const f=fixture();
    f.state.menuTitle='Build on uneven ground';f.state.menuText='Choose a Pier.\n\n<b>Raise</b> the preview until it is supported.';
    f.state.menuStatus='Tip 2 of 4';f.mode('guide',[row(931,'Next tip'),row(932,'Previous tip'),row(933,'All topics')]);
    assert.equal(f.el('adventure-ui').dataset.mode,'guide');assert(!f.el('adventure-menu-panel').hidden);
    for(const id of ['playbar','builder','quest','stock','compass','categories','more','save-row'])assert(f.el('adventure-'+id).hidden,id);
    assert.equal(f.el('adventure-menu-title').textContent,f.state.menuTitle);
    assert.equal(f.el('adventure-dialogue-text').textContent,f.state.menuText);
    assert.equal(f.el('adventure-dialogue-text').children.length,0);
    assert.equal(f.el('adventure-menu-status').textContent,'Tip 2 of 4');
    const next=f.rows()[0];assert.equal(f.document.activeElement,next);next.click();next.click();assert.deepEqual(f.actions,[[10,931]]);
    f.state.rows=[row(934,'Return to building')];++f.state.menuToken;++f.state.observation;f.tick();
    next.click();assert.deepEqual(f.actions,[[10,931]],'an old guide choice cannot activate a new page');
    f.rows()[0].dispatchEvent(new f.Event('click',{bubbles:true,detail:2}));assert.deepEqual(f.actions,[[10,931]]);
    f.rows()[0].click();assert.deepEqual(f.actions,[[10,931],[10,934]]);f.ui.cleanup();
});
test('Building help and How to play use guarded topic entry commands and preserve world links',()=>{
    const f=fixture(),building=f.el('adventure-building-help'),general=f.el('adventure-how-to-play');
    building.click();general.click();assert.deepEqual(f.actions,[],'hidden entry points do not dispatch');
    f.mode('build');assert(f.el('adventure-builder').contains(building));building.click();building.click();
    assert.deepEqual(f.actions,[[29,1]]);assert(building.disabled);assert.equal(f.state.mode,'build','guide state comes from the runtime');
    f.mode('guide',[row(941,'Return to building')]);building.click();general.click();assert.deepEqual(f.actions,[[29,1]]);
    f.mode('pause',[row(942,'Save adventure')]);general.click();assert.deepEqual(f.actions,[[29,1]],'closed Help details stay inactive');
    f.el('adventure-more').open=true;
    general.dispatchEvent(new f.Event('click',{bubbles:true,detail:2}));assert.deepEqual(f.actions,[[29,1]]);
    general.click();general.click();assert.deepEqual(f.actions,[[29,1],[29,0]]);
    assert.equal(f.el('adventure-more').querySelectorAll('p').length,1,'only current control labels remain as static Help prose');
    assert.equal(f.el('adventure-continue').href,'?experience=adventure');
    assert.equal(f.el('adventure-new').href,'?experience=adventure&new=1');
    assert.equal(f.el('adventure-prototypes').href,'?experience=lego-world');
    assert(f.el('adventure-more').contains(f.el('adventure-recover')));f.ui.cleanup();
});
test('closing a build guide restores world focus and retains live remapped control labels',()=>{
    const f=fixture(focusEvents);Object.assign(f.state,{attackControl:'C / Right shoulder',dodgeControl:'V / Back',lookControl:'Tap right mouse to look.'});
    f.mode('build');const help=f.el('adventure-building-help');help.focus();help.click();
    f.mode('guide',[row(951,'Return to building')]);assert.equal(f.document.activeElement,f.rows()[0]);
    const before=f.el('adventure-control-help').textContent;
    assert(before.includes('Attack: C / Right shoulder.'));assert(before.includes('Dodge: V / Back.'));assert(before.includes(f.state.lookControl));
    f.el('adventure-close').click();assert.deepEqual(f.actions.filter(([action])=>action===20),[[20,0]]);
    f.mode('build');assert.equal(f.document.activeElement,f.el('voxy-canvas'));assert.deepEqual(f.actions.at(-1),[15,0]);
    assert(!f.el('adventure-builder').hidden);assert(f.el('adventure-menu-panel').hidden);
    assert.equal(f.el('adventure-control-help').textContent,before);
    assert.equal(f.el('adventure-attack').textContent,'Attack · C / Right shoulder');
    assert.equal(f.el('adventure-dodge').textContent,'Dodge · V / Back');f.ui.cleanup();
});
test('compass bearing appears only for a truly owned equipped item and a finite target',()=>{
    const f=fixture();f.state.compass={equipped:true,available:true,bearing:90,distance:120,label:'Watchtower',backpackSlot:4};f.tick();
    assert(f.el('adventure-compass').hidden);f.state.equippedUtility={kind:5,quantity:1};f.state.camera={yaw:0};f.tick();
    assert(!f.el('adventure-compass').hidden);assert.equal(f.el('adventure-compass-reading').textContent,'Watchtower · E · 120 m');
    f.state.compass.bearing=NaN;f.tick();assert(f.el('adventure-compass-arrow').hidden);f.mode('bag',[row(601,'Put compass in bag')]);
    assert(f.el('adventure-compass').hidden);f.rows()[0].click();f.rows()[0].click();assert.deepEqual(f.actions,[[10,601]]);f.ui.cleanup();
});
test('Pause contains manual save status; save is the core published immutable choice',()=>{
    const f=fixture();f.state.menuText='Save before leaving.';f.mode('pause',[row(10,'Save adventure')]);
    assert(!f.el('adventure-save-row').hidden);assert(!f.el('adventure-more').hidden);
    assert.equal(f.el('adventure-save-status').textContent,'Unsaved changes');f.rows()[0].click();assert.deepEqual(f.actions,[[10,10]]);
    assert.equal(f.state.dirty,true);f.ui.cleanup();
});
test('Escape closes the current mode and focused UI keys do not reach world input',()=>{
    const f=fixture();f.mode('dialogue',[row(3)]);const focus=new f.Event('focusin',{bubbles:true});f.rows()[0].dispatchEvent(focus);
    assert.deepEqual(f.actions,[[15,1]]);const e=new f.Event('keydown',{key:'Escape',bubbles:true});f.rows()[0].dispatchEvent(e);
    assert(e.stopped&&e.defaultPrevented);assert.deepEqual(f.actions,[[15,1],[20,0],[15,0]]);
    const enter=new f.Event('keydown',{key:'Enter',bubbles:true});f.rows()[0].dispatchEvent(enter);assert(enter.stopped);assert(!enter.defaultPrevented);f.ui.cleanup();
});
test('Tab wraps within an open sheet and polling does not steal the focus',()=>{
    const f=fixture();f.mode('dialogue',[row(3),row(4)]);f.rows()[1].focus();
    const e=new f.Event('keydown',{key:'Tab',bubbles:true});f.rows()[1].dispatchEvent(e);
    assert(e.defaultPrevented);assert.equal(f.document.activeElement,f.el('adventure-close'));f.tick();assert.equal(f.document.activeElement,f.el('adventure-close'));f.ui.cleanup();
});
test('status loss disables controls, hides directions and retains dirty-leave protection',()=>{
    const f=fixture();f.readError=true;f.tick();assert.equal(f.el('adventure-status').textContent,'Adventure status is unavailable.');
    for(const b of f.el('adventure-ui').querySelectorAll('button'))assert(b.disabled);
    const e=new f.Event('beforeunload');for(const fn of f.environment.events.get('beforeunload'))fn(e);assert(e.defaultPrevented);
    f.readError=false;f.state.dirty=false;f.tick();const clean=new f.Event('beforeunload');for(const fn of f.environment.events.get('beforeunload'))fn(clean);assert(!clean.defaultPrevented);f.ui.cleanup();
});
test('invalid snapshots fail closed, while a real-capacity world and literal text are accepted',()=>{
    const f=fixture();f.state.structures=[{parts:Array.from({length:1024},(_,i)=>({id:String(i),matrix:Array(16).fill(123.456)}))}];
    f.state.quest={objective:'<img src=x onerror=evil()> Keep your home.'};f.tick();assert(!f.el('adventure-build').disabled);
    assert.equal(f.el('adventure-objective').textContent,f.state.quest.objective);assert.equal(f.el('adventure-objective').children.length,0);
    f.state.mode='invented';f.tick();assert(f.el('adventure-build').disabled);f.ui.cleanup();
});
test('confirmed Cove shortcut is reused unchanged and restored on cleanup',()=>{
    let original,parent;const f=fixture(f=>{parent=f.add('div','legacy');original=f.add('a','cove-continue',parent);original.href='?experience=salvage-cove&world=abc';});
    assert.equal(f.el('cove-continue'),original);assert(f.el('adventure-more').contains(original));
    f.ui.cleanup();assert.equal(original.parentElement,parent);assert.equal(original.href,'?experience=salvage-cove&world=abc');
});
test('void WASM dispatch retains its pending latch and always detaches the owned HUD',()=>{
    const f=fixture();f.tick();f.tick();assert.deepEqual(f.actions,[]);
    f.mode('dialogue',[row(701,'Accept quest')]);f.rows()[0].click();f.tick();f.rows()[0].click();
    assert.deepEqual(f.actions,[[10,701]]);assert(f.rows()[0].disabled);
    f.ui.cleanup();assert.deepEqual(f.actions,[[10,701],[19,0]]);
});
test('an explicitly refused dispatch releases the pending latch without inventing acceptance',()=>{
    const f=fixture();f.mode('workbench',[row(701,'Craft compass')]);f.refuse=true;f.rows()[0].click();
    assert(!f.rows()[0].disabled);assert.deepEqual(f.actions,[[10,701]]);f.ui.cleanup();
});
test('piece and crafting details use exact authoritative costs as literal text',()=>{
    const f=fixture();f.mode('catalog',[row(701,'Bed',true,{pieceKind:11,detail:'8 wood / 2 scrap'})]);
    assert.equal(f.rows()[0].querySelector('small').textContent,'8 wood / 2 scrap');
    f.state.rows[0].detail='<b>Cost changed</b>';f.tick();
    assert.equal(f.rows()[0].querySelector('small').textContent,'<b>Cost changed</b>');
    assert.equal(f.rows()[0].querySelector('small').children.length,0);f.ui.cleanup();
});
test('published controller selection is visible without programmatic focus overriding it',()=>{
    const f=fixture(focusEvents);f.state.menuSelected=2;
    f.mode('workbench',[row(701,'Hammer'),row(702,'Compass'),row(703,'Leave')]);
    assert.equal(f.document.activeElement,f.rows()[2]);assert.deepEqual(f.actions,[[15,1]]);
    f.state.menuSelected=1;++f.state.observation;f.tick();
    assert.equal(f.document.activeElement,f.rows()[1]);assert.equal(f.rows()[1].getAttribute('aria-current'),'true');
    assert(f.rows()[1].scrollCount>0);assert.equal(f.actions.filter(([a])=>a===26).length,0);
    f.rows()[0].focus();assert.deepEqual(f.actions.at(-1),[26,701]);
    f.tick();assert.equal(f.document.activeElement,f.rows()[0],'unchanged polling does not steal user focus');
    f.state.menuSelected=0;f.tick();assert.equal(f.document.activeElement,f.rows()[0]);
    const old=f.rows()[0];f.state.rows=[row(801,'Replacement')];++f.state.menuToken;f.tick();
    const count=f.actions.filter(([a])=>a===26).length;old.focus();
    assert.equal(f.actions.filter(([a])=>a===26).length,count,'detached focus handlers cannot select a replacement choice');f.ui.cleanup();
});
test('a replaced reply keeps keyboard focus and ignores physical double-click or held Enter',()=>{
    const f=fixture(focusEvents);f.mode('dialogue',[row(701,'Accept quest')]);f.rows()[0].click();
    f.state.rows=[row(801,'Complete quest')];++f.state.menuToken;++f.state.observation;f.tick();
    assert.equal(f.document.activeElement,f.rows()[0]);
    f.rows()[0].dispatchEvent(new f.Event('click',{bubbles:true,detail:2}));
    assert.deepEqual(f.actions.filter(([a])=>a===10),[[10,701]]);
    const held=new f.Event('keydown',{key:'Enter',repeat:true,bubbles:true});f.rows()[0].dispatchEvent(held);
    assert(held.defaultPrevented&&held.stopped);
    f.rows()[0].dispatchEvent(new f.Event('click',{bubbles:true,detail:1}));
    assert.deepEqual(f.actions.filter(([a])=>a===10),[[10,701],[10,801]]);f.ui.cleanup();
});
test('entering building returns hidden play-button focus and input ownership to the world',()=>{
    const f=fixture(focusEvents);f.el('adventure-build').focus();f.el('adventure-build').click();
    assert.deepEqual(f.actions,[[15,1],[21,0]]);f.mode('build');
    assert.equal(f.document.activeElement,f.el('voxy-canvas'));assert.deepEqual(f.actions.at(-1),[15,0]);f.ui.cleanup();
});
test('Pause Tab order includes Help summary and open world links while closed details stay hidden',()=>{
    const f=fixture();f.mode('pause',[row(701,'Save')]);const summary=f.el('adventure-more').querySelector('summary');
    f.rows()[0].focus();let event=new f.Event('keydown',{key:'Tab',bubbles:true});f.rows()[0].dispatchEvent(event);
    assert(!event.defaultPrevented,'Help summary follows the last choice');
    summary.focus();event=new f.Event('keydown',{key:'Tab',bubbles:true});summary.dispatchEvent(event);
    assert(event.defaultPrevented);assert.equal(f.document.activeElement,f.el('adventure-close'));
    const before=f.actions.length;f.el('adventure-recover').click();assert.equal(f.actions.length,before);
    f.el('adventure-more').open=true;summary.focus();event=new f.Event('keydown',{key:'Tab',bubbles:true});summary.dispatchEvent(event);
    assert(!event.defaultPrevented,'open Help links remain reachable');
    const last=f.el('adventure-prototypes');last.focus();event=new f.Event('keydown',{key:'Tab',bubbles:true});last.dispatchEvent(event);
    assert(event.defaultPrevented);assert.equal(f.document.activeElement,f.el('adventure-close'));
    event=new f.Event('keydown',{key:'Tab',shiftKey:true,bubbles:true});f.el('adventure-close').dispatchEvent(event);
    assert(event.defaultPrevented);assert.equal(f.document.activeElement,last);f.ui.cleanup();
});

test('trail compass cycles a runtime destination without fabricating a new bearing',()=>{
    const f=fixture();f.state.equippedUtility={kind:5,quantity:1};
    f.state.compass={equipped:true,available:true,target:'signal-terrace',label:'Signal Terrace',bearing:90,distance:84};
    f.state.camera={yaw:0};f.tick();
    assert.equal(f.el('adventure-compass-target').textContent,'Next destination');
    f.el('adventure-compass-target').click();assert.deepEqual(f.actions,[[18,0]]);
    assert.equal(f.el('adventure-compass-reading').textContent,'Signal Terrace · E · 84 m');
    f.state.compass={...f.state.compass,target:'survey-overlook',label:'Survey Overlook',bearing:0,distance:140};f.tick();
    assert.equal(f.el('adventure-compass-reading').textContent,'Survey Overlook · N · 140 m');f.ui.cleanup();
});
test('trail journal keeps selection separate from NPC turn-in and displays core-owned guidance',()=>{
    const f=fixture();f.state.quest={objective:'Take the relay core from storage.'};
    f.state.menuTitle='Restore the relay';f.state.menuText='Make a field home within 25 m of the beacon.';
    f.mode('journal',[row(900,'Prepare for the trail',true,{detail:'Completed'}),row(901,'Track Meadow beacon')]);
    assert.equal(f.el('adventure-menu-title').textContent,'Restore the relay');
    assert.equal(f.el('adventure-dialogue-text').textContent,'Make a field home within 25 m of the beacon.');
    f.rows()[1].click();assert.deepEqual(f.actions,[[10,901]]);
    assert.equal(f.state.quest.objective,'Take the relay core from storage.');f.ui.cleanup();
});

test('choosing a compass destination releases keyboard focus for walking',()=>{
    const f=fixture(focusEvents);f.state.equippedUtility={kind:5,quantity:1};
    f.state.compass={equipped:true,available:true,target:'relay',label:'Beacon',bearing:0,distance:80};f.tick();
    f.el('adventure-compass-target').focus();f.el('adventure-compass-target').click();
    assert.equal(f.document.activeElement,f.el('voxy-canvas'));
    assert.deepEqual(f.actions,[[15,1],[18,0],[15,0]]);f.ui.cleanup();
});

test('settings and binding modes reuse one focused sheet with published choices and core status',()=>{
    const f=fixture();let intent=1100;
    for(const mode of ['settings','controls','combat-binding','binding-choice']){
        f.state.menuTitle='Current settings';f.state.preferencesStatus='Settings work for this visit.';f.state.menuStatus='Previous settings status';
        f.mode(mode,[row(++intent,'Change control',true,{pieceKind:0,blueprintKind:0})]);
        assert(!f.el('adventure-menu-panel').hidden);assert(f.el('adventure-playbar').hidden);assert(f.el('adventure-builder').hidden);
        assert.equal(f.el('adventure-menu-title').textContent,'Current settings');
        assert(!f.el('adventure-preferences-status').hidden);
        assert(f.el('adventure-menu-status').hidden,'settings use one current status instead of a duplicate stale message');
        assert.equal(f.el('adventure-preferences-status').textContent,'Settings work for this visit.');
        const current=f.rows()[0];current.click();current.click();assert.deepEqual(f.actions.at(-1),[10,intent]);
        assert.equal(f.document.querySelectorAll('[data-voxy-ui]').length,1);
    }
    assert.equal(f.actions.filter(([action])=>action===10).length,4);
    f.mode('explore');assert(f.el('adventure-preferences-status').hidden);f.ui.cleanup();
});

test('combat and look help describe the current core bindings and preserve action focus handling',()=>{
    const f=fixture(focusEvents);f.state.staffEquipped=true;
    Object.assign(f.state,{attackControl:'F / Right trigger',dodgeControl:'G / Left shoulder',lookControl:'Press right mouse to toggle look.'});f.tick();
    assert.equal(f.el('adventure-attack').textContent,'Attack · F / Right trigger');
    assert.equal(f.el('adventure-dodge').textContent,'Dodge · G / Left shoulder');
    assert.equal(f.el('adventure-control-help').textContent,
        'Press right mouse to toggle look. Attack: F / Right trigger. Dodge: G / Left shoulder.');
    f.el('adventure-attack').focus();f.el('adventure-attack').click();
    assert.deepEqual(f.actions,[[15,1],[27,0],[15,0]]);assert.equal(f.document.activeElement,f.el('voxy-canvas'));
    ++f.state.observation;f.state.attackControl='<b>New key</b>';f.tick();
    assert.equal(f.el('adventure-attack').textContent,'Attack · <b>New key</b>');assert.equal(f.el('adventure-attack').children.length,0);
    delete f.state.attackControl;delete f.state.dodgeControl;delete f.state.lookControl;f.tick();
    assert.equal(f.el('adventure-attack').textContent,'Attack');assert.equal(f.el('adventure-dodge').textContent,'Dodge');
    assert(!f.el('adventure-control-help').textContent.includes('Q'));
    assert(!f.el('adventure-control-help').textContent.includes('right mouse'));f.ui.cleanup();
});

test('preferences bridge starts once after readiness, receives accepted observations and is disposed once',()=>{
    const seen=[];let installed=0,cleaned=0;
    const f=fixture(f=>{
        f.state.ready=false;f.environment.VoxyAdventurePreferences={install(engine,environment){
            assert.equal(engine,f.engine);assert.equal(environment,f.environment);++installed;
            return {tick:state=>seen.push(structuredClone(state)),cleanup:()=>++cleaned};
        }};
    });
    assert.equal(installed,0);assert.equal(seen.length,0);
    f.state.ready=true;f.state.preferencesRevision='5';f.tick();assert.equal(installed,1);
    assert.equal(seen.length,1);assert.deepEqual(seen[0],f.state);
    f.state.preferencesRevision='6';f.tick();assert.equal(installed,1);assert.equal(seen.at(-1).preferencesRevision,'6');
    f.readError=true;f.tick();assert.equal(seen.length,2,'invalid observer data never reaches preference transport');
    f.ui.cleanup();f.ui.cleanup();f.tick();assert.equal(cleaned,1);assert.equal(seen.length,2);
});

test('real preference bridge saves a settings menu edit from runtime snapshots without top-level ready',()=>{
    const preferences=require('../../web/adventure_preferences.js');
    const stored=new Map(),calls=[],reads=[],writes=[];
    let canonical={version:1,textScale:1};
    const f=fixture(f=>{
        delete f.state.ready;
        Object.assign(f.state,{preferencesRevision:'1',preferencesStatus:'Default settings.',quest:{ready:false}});
        f.environment.VoxyAdventurePreferences=preferences;
        f.environment.localStorage={
            getItem(key){reads.push(key);return stored.get(key)??null;},
            setItem(key,value){writes.push([key,value]);stored.set(key,value);},
        };
        f.engine.ccall=(name,result,types,args)=>{
            assert.equal(name,'adventure_preferences_action');assert.equal(result,'string');
            assert.deepEqual(types,['number','string']);const [kind,message]=args;calls.push(kind);
            if(kind===1)return JSON.stringify(canonical);
            if(kind===4){f.state.preferencesStatus='Settings saved on this device.';return 'ok';}
            if(kind===5){f.state.preferencesStatus=message;return 'ok';}
            assert.fail('An empty settings store cannot issue apply/reset commands.');
        };
        const dispatch=f.engine._adventure_action;
        f.engine._adventure_action=(action,value)=>{
            const result=dispatch(action,value);
            if(action===10&&value===901){
                canonical={version:1,textScale:1.25};f.state.preferencesRevision='2';
                f.state.preferencesStatus='Settings apply for this visit.';
            }
            return result;
        };
    });
    assert.deepEqual(reads,[preferences.storageKey]);assert.deepEqual(calls,[1]);assert.deepEqual(writes,[]);
    f.mode('settings',[row(901,'Text size: 100%')]);f.rows()[0].click();
    assert.deepEqual(f.actions,[[10,901]]);assert.deepEqual(writes,[[preferences.storageKey,JSON.stringify(canonical)]]);
    assert.equal(f.state.preferencesStatus,'Settings saved on this device.');
    for(let i=0;i<10;++i)f.tick();
    assert.equal(f.el('adventure-preferences-status').textContent,'Settings saved on this device.');
    assert.deepEqual(calls,[1,1,4]);assert.equal(writes.length,1,'steady observations do not repeat storage writes');
    f.ui.cleanup();f.tick();assert.deepEqual(calls,[1,1,4]);
});

test('creative entry hides chores and combat while retaining build, save and comfort controls',()=>{
    const f=fixture(f=>Object.assign(f.state,{creative:true,mode:'build',build:true,piece:10,costText:'Unlimited pieces',valid:true,parts:0}));
    assert(!f.el('adventure-builder').hidden);assert(f.el('adventure-stock').hidden);assert(f.el('adventure-starter').hidden);
    assert.equal(f.el('adventure-cost').textContent,'Unlimited pieces');assert(!f.el('adventure-place').disabled);
    f.state.parts=1;f.state.status='Placed.';f.state.previewReason='Building pieces overlap';f.state.valid=false;f.tick();
    assert.equal(f.el('adventure-build-count').textContent,'1 / 1,024 pieces placed');
    assert.equal(f.el('adventure-build-feedback').textContent,'Last action: Placed.');
    assert.equal(f.el('adventure-ui').getAttribute('aria-label'),'Building controls');
    f.mode('explore');
    for(const id of ['adventure-quest','adventure-combat','adventure-stock','adventure-bag','adventure-journal','adventure-compass'])assert(f.el(id).hidden,id);
    assert(!f.el('adventure-build').hidden);assert(!f.el('adventure-menu').hidden);
    f.mode('pause');assert(!f.el('adventure-save-row').hidden);
    assert.equal(f.el('adventure-new').getAttribute('href'),'?experience=build&new=1');
    assert.equal(f.el('adventure-continue').getAttribute('href'),'?experience=build');f.ui.cleanup();
});
