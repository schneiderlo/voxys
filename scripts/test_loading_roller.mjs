// Exercise the production roller with delayed/missing browser animation frames.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const html=readFileSync(new URL('../web/index.html',import.meta.url),'utf8');
const source=html.slice(html.indexOf('        class Roller {'),html.indexOf('        // Progress tracking'));
function fixture(){
    let now=0,locks=0,nextId=0;
    const timers=new Map(),frames=new Map();
    const element=()=>({style:{},classList:{add(){},remove(){}},appendChild(){}});
    const context={performance:{now:()=>now},Math,
        document:{getElementById:element,createElement:element},
        SoundManager:class{playClick(){}playLock(){locks++;}},
        setTimeout:(f,ms)=>{const id=++nextId;timers.set(id,{f,at:now+ms});return id;},
        clearTimeout:id=>timers.delete(id),
        requestAnimationFrame:f=>{const id=++nextId;frames.set(id,f);return id;},
        cancelAnimationFrame:id=>frames.delete(id)};
    vm.runInNewContext(source+';globalThis.roller=new Roller(["Loading"]);',context);
    return {roller:context.roller,frames,locks:()=>locks,
        advance(ms,runTimers=true){now+=ms;if(runTimers)for(const [id,t] of timers){if(t.at<=now){timers.delete(id);t.f();}}}};
}
test('waiting for the engine never completes the loader prematurely',()=>{
    const f=fixture();f.roller.start();f.advance(60000);f.roller.loop();
    assert.notEqual(f.roller.state,'LOCKED');assert.equal(f.locks(),0);
});
test('landing follows elapsed time after a five-second animation-frame gap',()=>{
    const f=fixture();f.roller.start();f.roller.land();
    f.advance(5000,false);f.roller.loop();
    assert.equal(f.roller.state,'LOCKED');assert.equal(f.roller.angle,f.roller.targetAngle);
    assert.equal(f.locks(),1);
});
test('a ready engine is revealed even when animation frames stop entirely',()=>{
    const f=fixture();f.roller.start();f.roller.land();
    const lateFrame=[...f.frames.values()][0];
    f.advance(1999);assert.equal(f.roller.state,'STOPPING');
    f.advance(1);assert.equal(f.roller.state,'LOCKED');
    assert.equal(f.frames.size,0);lateFrame();f.roller.land();
    assert.equal(f.locks(),1);assert.equal(f.roller.angle,f.roller.targetAngle);
});
