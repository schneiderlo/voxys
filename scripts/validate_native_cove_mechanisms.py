#!/usr/bin/env python3
"""One bounded real-controls Cove mechanism journey; read-only evidence, no images.

Fresh world: normal/reverse throttle, two free propeller settings refits, real
hook/reel/pay/release, workshop/pause freeze, one save and process restart.
Use an integrated native binary; no action injection or save/state setters.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import signal
import struct
import subprocess
import time

from validate_native_cove_delivery import Controls
from validate_native_cove_saves import archive

WIDTH, HEIGHT = 960, 540
TAU = 2 * math.pi
RADIUS = .28
TOLERANCE = 3e-4  # JSON float formatting; far below one 60 Hz rotor step.


def phase_error(actual, expected):
    return math.atan2(math.sin(actual - expected), math.cos(actual - expected))


def saved_owned_design(payload):
    """Read exact accepted part/connection bytes from this journey's schema4 save.

    Frozen protocol field order is in cove_save.cpp and session_save.cpp. This
    reads bounded byte slices, never deserializes objects into the running game.
    Authority epoch/leases may change on restore; owned part IDs, health, paint,
    settings, provenance and connections must remain byte-for-byte identical.
    """
    assert payload[:4] == b'SVCE' and int.from_bytes(payload[4:8], 'little') == 4
    assert hashlib.sha256(payload[:-32]).digest() == payload[-32:]
    at = 441  # Header + frozen CovePhysicalSave + harbor state in schema4.
    def take(size):
        nonlocal at
        assert 0 <= size <= 4 * 1024 * 1024 and at + size <= len(payload) - 32
        result = payload[at:at + size]; at += size
        return result
    def count(maximum):
        n = int.from_bytes(take(4), 'little'); assert n <= maximum
        return n
    recovery = [hashlib.sha256(take(count(131072))).hexdigest() for _ in range(count(4))]
    take(48)  # Control part and player root durable IDs.
    roots = count(32); assert roots > 0; take(88 * roots)
    session = take(count(4 * 1024 * 1024))
    assert session[:4] == b'SVSC' and 1 <= int.from_bytes(session[4:8], 'little') <= 3
    assert hashlib.sha256(session[:-32]).digest() == session[-32:]
    at = 8 + 4 + 68  # Envelope, logical schema, content key/digest/profile.
    def get(size):
        nonlocal at
        assert 0 <= size <= 4 * 1024 * 1024 and at + size <= len(session) - 32
        result = session[at:at + size]; at += size
        return result
    def u32(maximum):
        n = int.from_bytes(get(4), 'little'); assert n <= maximum
        return n
    world = get(16).hex(); get(48)  # Caller IDs.
    get(8)  # Authority epoch intentionally changes on recovery.
    get(8); get(16)  # Allocator lease frontier and session revision/tick can advance on restore.
    inventory = struct.unpack('<QQ', get(16))
    get(64)  # Frozen SessionLimits field widths; no native struct padding.
    assert get(1) == b'\1'
    builds = []
    for _ in range(u32(32)):
        identity, revision, owner = get(24), get(8), get(24)
        lease = get(1); assert lease in (b'\0', b'\1')
        if lease == b'\1': get(40)
        parts = [get(130) for _ in range(u32(256))]
        connections = [get(136) for _ in range(u32(1024))]
        assert parts and all(p[:16].hex() == world for p in parts)
        builds.append({'id': identity.hex(), 'revision': revision.hex(), 'owner': owner.hex(),
                       'partIds': [p[:24].hex() for p in parts],
                       'partsSha256': hashlib.sha256(b''.join(parts)).hexdigest(),
                       'connectionsSha256': hashlib.sha256(b''.join(connections)).hexdigest(),
                       'parts': len(parts), 'connections': len(connections)})
    assert len(builds) == 1 and builds[0]['parts'] == 11
    return {'world': world, 'inventory': list(inventory),
            'builds': builds, 'recoveryDesignDigests': recovery}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--storage-root', type=Path, required=True)
    args = parser.parse_args()
    args.binary = args.binary.resolve(strict=True)
    args.output = args.output.resolve(); args.storage_root = args.storage_root.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    args.storage_root.mkdir(mode=0o700, parents=True, exist_ok=False)
    report = {'status': 'running', 'kind': 'Native real-control mechanism motion and exact restart; no images',
              'startedUtc': datetime.now(timezone.utc).isoformat(), 'binary': str(args.binary),
              'binarySha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(), 'stages': [], 'checks': []}
    controls = child = stream = window = None
    index = 0; held = set(); checked_logs = set(); began = time.monotonic()

    def read():
        try: return json.loads((args.output / f'observation-{index}' / 'state.json').read_text())
        except (OSError, json.JSONDecodeError): return {}

    def persist():
        (args.output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')

    def wait(predicate, label, seconds=30):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if time.monotonic() - began > 270: raise RuntimeError('Mechanism journey exhausted its 270-second work budget; cleanup remains inside five minutes')
            if child and child.poll() is not None: raise RuntimeError(f'Game exited ({child.returncode}): {label}')
            state = read(); assert not state.get('failed'), state
            if predicate(state): return state
            time.sleep(.02)
        raise RuntimeError(label + ': ' + json.dumps(read()))

    def key(name): controls.key(window, name)
    def down(name): controls.state(window, name, True); held.add(name)
    def up(name): controls.state(window, name, False); held.discard(name)
    def mechanism(s): return s['boat']['mechanisms']
    def body(s):
        b, m = s['boat'], mechanism(s)
        return (b['physicsTicks']['incarnation'], m['incarnation'], b['buildId'], b['topologyRevision'],
                tuple(r['key'] for r in b['roots']), m['bodyIndex'], m['bodyGeneration'])

    def physical(s):
        b, m = s['boat'], mechanism(s)
        assert b['active'] and b['parts'] == 11 and b['massKg'] == 1035 and b['paidPartIds'] == [], b
        assert s['session']['inventory']['salvageMaterial'] == '48', s['session']
        assert s['session']['inventory']['specialMachinery'] == '0', s['session']
        assert s['workshop']['storedParts'] == 0 and s['workshop']['storedPartIds'] == [], s['workshop']
        assert not b['physicsTicks']['failed'] and b['physicsTicks']['supported'], b['physicsTicks']
        assert m['incarnation'] == b['physicsTicks']['incarnation'] and int(m['incarnation']) > 0, m
        assert m['bodyIndex'] > 0 and m['bodyGeneration'] > 0 and m['animatedParts'] == (0 if s['workshop']['open'] else 2), m
        assert all(math.isfinite(m[k]) for k in ('rotorRadians', 'drumRadians', 'effectiveDrive', 'ropeLength'))
        assert all(0 <= m[k] < TAU + 1e-5 for k in ('rotorRadians', 'drumRadians'))
        fixture, hud = s['assetFixture'], s['nativeHud']
        marking_bytes = int(fixture['dockMarkingGpuBytes'])
        assert 0 < marking_bytes <= 16384, fixture
        assert int(fixture['gpuReservationBytes']) == 10745384 + marking_bytes, fixture
        assert fixture['dockMarkingDraws'] == (0 if s['workshop']['open'] else 2), fixture
        assert s['terrainSurface'] == 'lego' and fixture['sceneSunShadows'] and fixture['environmentReady'], fixture
        assert fixture['presentationParts'] == 7 and 0 < int(fixture['completedSerial']) <= int(fixture['submittedSerial'])
        assert hud['enabled'] and hud['lastEncodedQuads'] > 0 and hud['bodyPixels'] >= 16 and not hud['truncated'], hud
        x,y,w,h = hud['panel']; assert 0 <= x and 0 <= y and 0 < w and 0 < h and x+w <= WIDTH and y+h <= HEIGHT

    def sample(name, predicate=lambda _: True):
        # Capture a submitted state first, then establish that exact frame/tick
        # completed. Looking only at the newest state would chase in-flight work.
        s = wait(lambda s: s.get('ready') and s.get('boat', {}).get('mechanisms')
                 and int(s.get('assetFixture', {}).get('completedSerial', 0)) > 0
                 and s['assetFixture'].get('environmentReady')
                 and mechanism(s)['animatedParts'] == (0 if s['workshop']['open'] else 2)
                 and s.get('nativeHud', {}).get('lastEncodedQuads', 0) > 0 and predicate(s), name)
        physical(s)
        frame, tick, identity = int(s['assetFixture']['submittedSerial']), int(mechanism(s)['tick']), body(s)
        completion = wait(lambda n: body(n) == identity and int(n['assetFixture']['completedSerial']) >= frame
                          and int(n['boat']['physicsTicks']['completed']) >= tick, name + ' actual completion')
        proof = {'frame': frame, 'tick': tick, 'completedFrame': int(completion['assetFixture']['completedSerial']),
                 'completedPhysicsTick': int(completion['boat']['physicsTicks']['completed']), 'body': identity}
        report['stages'].append({'name': name, 'process': index, 'state': s, 'completion': proof})
        print(name, flush=True); persist()
        return s

    def later_tick(before, ticks):
        return wait(lambda s: body(s) == body(before) and int(mechanism(s)['tick']) >= int(mechanism(before)['tick']) + ticks,
                    'owned mechanism ticks advanced')

    def rotor(name, button, expected):
        if button: down(button)
        try:
            a = sample(name + '-before', lambda s: s['player']['mode'] == 'helm' and abs(mechanism(s)['effectiveDrive']-expected) < 1e-6)
            later_tick(a, 6)
            def endpoint(s):
                dt = int(mechanism(s)['tick']) - int(mechanism(a)['tick'])
                angle = abs(phase_error(2 * TAU * expected * dt / 60, 0))
                # Whole turns can hide a frozen mesh; half turns hide reversed
                # direction. Choose a completed non-aliased endpoint instead.
                return abs(mechanism(s)['effectiveDrive']-expected) < 1e-6 and (expected == 0 or .05 < angle < math.pi-.05)
            b = sample(name + '-after', endpoint)
            assert body(a) == body(b)
            dt = int(mechanism(b)['tick']) - int(mechanism(a)['tick']); assert 6 <= dt <= 180
            delta = mechanism(b)['rotorRadians'] - mechanism(a)['rotorRadians']
            wanted = 2 * TAU * expected * dt / 60
            error = phase_error(delta, wanted)
            assert abs(error) < TOLERANCE, (name, dt, delta, wanted, error)
            report['checks'].append({'name': name, 'tickDelta': dt, 'effectiveDrive': expected,
                                     'expectedRadians': wanted, 'wrappedError': error})
            persist(); return b
        finally:
            if button: up(button)

    def hold(names, ticks):
        start = int(read()['player']['tick'])
        try:
            for name in names: down(name)
            wait(lambda s: int(s['player']['tick']) >= start + ticks, 'walking input observed')
        finally:
            for name in names: up(name)
        # The observation stream is sampled at 10 Hz. Wait for a post-release
        # sample before deriving the next direction from the player's position.
        released = int(read()['player']['tick'])
        wait(lambda s:int(s['player']['tick'])>released,'fresh released movement observation')

    def walk(target, reached=lambda _: False):
        trace=[]
        for _ in range(90):
            s=read()
            if reached(s): return
            p=s['player']['feet']; t=target(s) if callable(target) else target
            dx,dz=t[0]-p[0],t[1]-p[2]; distance=math.hypot(dx,dz)
            if distance < .3: return
            yaw=s['camera']['yaw']; forward=(dx*math.sin(yaw)+dz*math.cos(yaw))/distance
            right=(dx*math.cos(yaw)-dz*math.sin(yaw))/distance
            keys=[]
            if abs(forward)>.4: keys.append('w' if forward>0 else 's')
            if abs(right)>.4: keys.append('d' if right>0 else 'a')
            trace.append({'feet':p,'target':t,'keys':keys,'tick':s['player']['tick'],'interaction':s['player']['interaction']})
            hold(keys,max(1,min(5,int(distance/.06)-1)))
        report['failedApproach']=trace
        raise RuntimeError('Interaction waypoint was not reached: '+json.dumps(trace[-3:]))

    def board():
        near=lambda s:s['player']['interaction']=='board'
        walk([6,-49.5],near); walk([4.5,-49.5],near); walk([4.5,-53],near)
        wait(lambda s:s['player']['interaction']=='board','boarding interaction')
        key('e'); wait(lambda s:s['player']['onBoat'],'aboard')
        walk(lambda s:[s['boat']['helmPosition'][0],s['boat']['helmPosition'][2]],lambda s:s['player']['interaction']=='helm')
        wait(lambda s:s['player']['interaction']=='helm','helm interaction')
        key('e'); wait(lambda s:s['player']['mode']=='helm','helm active')

    def dock():
        key('e'); wait(lambda s:s['player']['mode']!='helm','leave helm')
        def point(s):
            x,y,z,w=s['boat']['orientation']; v=(2.1,0,.1)
            # Quaternion rotation of the authored boarding offset from helm.
            cross=(y*v[2]-z*v[1],z*v[0]-x*v[2],x*v[1]-y*v[0])
            twice=tuple(2*a for a in cross)
            second=(y*twice[2]-z*twice[1],z*twice[0]-x*twice[2],x*twice[1]-y*twice[0])
            rotated=tuple(v[k]+w*twice[k]+second[k] for k in range(3))
            h=s['boat']['helmPosition']; return [h[0]+rotated[0],h[2]+rotated[2]]
        walk(point,lambda s:s['player']['interaction']=='dock'); wait(lambda s:s['player']['interaction']=='dock','return-to-dock interaction')
        key('e'); wait(lambda s:not s['player']['onBoat'],'player on dock')
        walk([4.5,-49.5],lambda s:s['workshop']['canOpen'])
        walk([6,-49],lambda s:s['workshop']['canOpen'])
        wait(lambda s:s['workshop']['canOpen'],'workshop available on dock')

    def select_propeller():
        for _ in range(12):
            w=read()['workshop']
            if w['name']=='Propeller': return
            old=w['selected']; key('Tab')
            wait(lambda s:s['workshop']['selected']!=old,'selected next part')
        raise RuntimeError('Propeller not selectable')

    def refit(name, button, enabled, reversed_):
        key('b'); wait(lambda s:s['workshop']['open'],'workshop open'); select_propeller()
        key(button)
        wait(lambda s:s['workshop']['changed'] and s['workshop']['settings']['enabled']==enabled
             and s['workshop']['settings']['reversed']==reversed_,name+' preview')
        preview=sample(name+'-preview')
        assert preview['workshop']['configurable'] and preview['workshop']['canReverse']
        key('e'); wait(lambda s:not s['workshop']['changed'] and s['workshop']['canLaunch'],name+' kept')
        w=read()['workshop']; assert all(w[k]=='0' for k in ('charge','refund','machineryCharge','machineryRefund')),w
        launches=w['launches']; before=read()['session']['inventory']
        key('Return'); wait(lambda s:not s['workshop']['open'] and not s['workshop']['pending']
                           and s['workshop']['launches']==launches+1,name+' launched')
        launched=sample(name+'-launched')
        assert launched['session']['inventory']==before
        assert (launched['boat']['thrustLimitNewtons']>0)==enabled
        return launched

    def rope_pair(name, button, ticks, direction):
        a=sample(name+'-before',lambda s:s['tow']['attached'] and s['tow']['confirmed'] and mechanism(s)['ropeGeneration']>0)
        old=mechanism(a); handle=(old['ropeIndex'],old['ropeGeneration'])
        down(button)
        try:
            def endpoint(s):
                m=mechanism(s); delta=m['ropeLength']-old['ropeLength']
                angle=abs(phase_error(-delta/RADIUS,0))
                return (m['ropeIndex'],m['ropeGeneration'])==handle and int(m['ropeTick'])>=int(old['ropeTick'])+ticks \
                    and delta*direction>.015 and .05 < angle < math.pi-.05
            b=sample(name+'-after',endpoint)
        finally: up(button)
        wait(lambda s:s['tow']['confirmed'] and s['tow']['motor']==0,name+' motor stopped')
        new=mechanism(b)
        assert body(a)==body(b) and (new['ropeIndex'],new['ropeGeneration'])==handle
        assert b['tow']['attached'] and not b['tow']['broken']
        length=new['ropeLength']-old['ropeLength']; wanted=-length/RADIUS
        error=phase_error(new['drumRadians']-old['drumRadians'],wanted)
        assert length*direction>.015 and abs(error)<TOLERANCE,(name,length,error)
        report['checks'].append({'name':name,'ropeHandle':handle,'ropeTickBefore':old['ropeTick'],
            'ropeTickAfter':new['ropeTick'],'lengthDelta':length,'expectedRadians':wanted,'wrappedError':error})
        persist(); return b

    def frozen(name, paused=False):
        a=sample(name+'-before'); frame=int(a['assetFixture']['submittedSerial'])
        wait(lambda s:int(s['assetFixture']['completedSerial'])>=frame+3,name+' completed frames')
        b=sample(name+'-after')
        assert body(a)==body(b)
        for value in ('rotorRadians','drumRadians'): assert mechanism(a)[value]==mechanism(b)[value],(name,value)
        assert mechanism(b)['effectiveDrive']==0
        if paused: assert a['pause']==b['pause'] and a['boat']['physicsTicks']['completed']==b['boat']['physicsTicks']['completed']
        return b

    def stop():
        nonlocal child,stream
        try:
            if controls and window:
                for name in tuple(held): up(name)
            if child and child.poll() is None:
                child.send_signal(signal.SIGTERM)
                try: child.wait(timeout=15)
                except subprocess.TimeoutExpired: child.kill(); child.wait(timeout=5)
        finally:
            if stream: stream.close()
            child=stream=None
        path=args.output/f'process-{index}.log'
        if index and index not in checked_logs and path.exists():
            checked_logs.add(index)
            pattern=re.compile(r'uncaptured(?:\s+WebGPU)?(?:\s+GPU)?\s+error|WebGPU validation error|'
                               r'(?:WGPU|WebGPU)[^\n]{0,100}(?:error|validation failed)|\[(?:error|fatal)\]',re.IGNORECASE)
            errors=[line for line in path.read_text(errors='replace').splitlines() if pattern.search(line)]
            report.setdefault('processChecks',[]).append({'process':index,'errorLines':errors})
            if errors:
                message='Native process log contains GPU/runtime errors: '+' | '.join(errors[:8])
                report['status']='failed';report['error']=(report.get('error','')+'\n'+message).strip()
                raise RuntimeError(message)

    def start(world=None):
        nonlocal child,stream,window,index
        index+=1; stream=(args.output/f'process-{index}.log').open('w')
        command=[str(args.binary),'--config','salvage_cove.cfg','--uncapped','--width',str(WIDTH),'--height',str(HEIGHT),
                 '--expedition-root',str(args.storage_root),'--expedition-observe',str(args.output/f'observation-{index}')]
        if world: command+=['--expedition-world',world]
        child=subprocess.Popen(command,stdout=stream,stderr=subprocess.STDOUT,env={**os.environ,'VOXY_WINDOW_BACKEND':'x11'})
        wait(lambda _:controls.own_window(child.pid),'owned native window',60); window=controls.own_window(child.pid)
        wait(lambda s:s.get('ready') and s.get('boat',{}).get('active') and s['boat'].get('mechanisms'),'native Cove ready',60)
        if world: wait(lambda s:s.get('restore',{}).get('phase')=='ready' and s['pause']['phase']=='paused','saved world restored')

    try:
        controls=Controls(); start(); sample('fresh-world')
        board(); rotor('forward-throttle','w',1); rotor('stopped-after-forward',None,0)
        rotor('reverse-throttle','s',-1); rotor('stopped-after-reverse',None,0)
        dock(); refit('reversed-propeller','n',True,True)
        board(); rotor('reversed-setting-forward-key','w',-1); rotor('reversed-setting-stopped',None,0)
        dock(); refit('disabled-propeller','x',False,True)
        board(); rotor('disabled-setting-forward-key','w',0); rotor('disabled-setting-stopped',None,0)
        wait(lambda s:s['tow']['operable'] and s['tow']['confirmed'] and s['tow']['inRange'],'generator within hooking reach')
        key('f'); hooked=sample('generator-hooked',lambda s:s['tow']['attached'] and s['tow']['confirmed'] and mechanism(s)['ropeGeneration']>0)
        rope_pair('reel-in','q',8,-1); paid=rope_pair('pay-out','z',3,1)
        assert abs(phase_error(mechanism(paid)['drumRadians'],mechanism(hooked)['drumRadians']))>.005,'Reel/pay must leave a visible nonzero phase'
        still=sample('rope-held',lambda s:s['tow']['motor']==0 and s['tow']['confirmed'])
        # Observations and command completion can make the two short motions
        # cancel exactly. Establish a nonzero phase with real input, rather
        # than assuming different requested sample counts imply net travel.
        if abs(phase_error(mechanism(still)['drumRadians'],0))<=.005:
            rope_pair('checkpoint-reel','q',3,-1)
            still=sample('checkpoint-rope-held',lambda s:s['tow']['motor']==0 and s['tow']['confirmed'])
            assert abs(phase_error(mechanism(still)['drumRadians'],0))>.005
        later_tick(still,6); after=sample('rope-held-later')
        assert mechanism(still)['drumRadians']==mechanism(after)['drumRadians']
        key('f'); sample('rope-released',lambda s:not s['tow']['attached'] and s['tow']['confirmed'] and mechanism(s)['ropeGeneration']==0)
        dock(); key('b'); wait(lambda s:s['workshop']['open'],'final workshop open'); select_propeller()
        settings=read()['workshop']['settings']; assert not settings['enabled'] and settings['reversed']
        frozen('workshop-freeze'); key('b'); wait(lambda s:not s['workshop']['open'],'final workshop closed')
        wait(lambda s:s['pause']['canPause'],'safe pause available'); key('p')
        wait(lambda s:s['pause']['phase']=='paused','expedition paused'); paused=frozen('pause-freeze',True)
        assert abs(phase_error(mechanism(paused)['drumRadians'],0))>.005,'Checkpoint must carry nonzero ephemeral phase before restart'
        slot=args.storage_root/paused['world']; prior=archive(slot)[0]['generation'] if (slot/'current').exists() else 0
        key('F10')
        def saved_ready(s):
            if 'Expedition saved' not in controls.title(window): return False
            try:
                m,_=archive(slot); return m['generation']>prior and str(m['tick'])==s['pause']['tick']
            except (AssertionError,OSError): return False
        wait(saved_ready,'new mirrored checkpoint saved')
        meta,payload=archive(slot); owned=saved_owned_design(payload)
        (args.output/'mechanisms.svce').write_bytes(payload)
        saved=sample('mechanisms-saved'); report['savedOwnedDesign']=owned; report['savedArchive']=meta; persist()
        assert saved['boat']['joinedTick']==str(meta['tick'])
        stop(); start(saved['world']); restored=sample('mechanisms-restored-neutral')
        recovered,recovered_payload=archive(slot); restored_owned=saved_owned_design(recovered_payload)
        assert restored_owned==owned,(restored_owned,owned)
        assert recovered['generation']>meta['generation'] and recovered['tick']==meta['tick']
        assert int(restored['restore']['baseTick'])==meta['tick'] and int(restored['pause']['tick'])==meta['tick']+1
        assert restored['session']['inventory']==saved['session']['inventory']
        assert restored['boat']['buildId']==saved['boat']['buildId'] and restored['boat']['paidPartIds']==saved['boat']['paidPartIds']
        assert restored['boat']['controlPart']==saved['boat']['controlPart']
        assert [r['key'] for r in restored['boat']['roots']]==[r['key'] for r in saved['boat']['roots']]
        assert mechanism(restored)['rotorRadians']==0 and mechanism(restored)['drumRadians']==0 and mechanism(restored)['effectiveDrive']==0
        assert mechanism(restored)['ropeGeneration']==0
        report['restoredOwnedDesign']=restored_owned; report['restoredArchive']=recovered
        key('p'); wait(lambda s:s['pause']['phase']=='running','resume restored world')
        key('b'); wait(lambda s:s['workshop']['open'],'restored settings workshop'); select_propeller()
        exact=sample('disabled-reversed-settings-restored'); assert exact['workshop']['settings']==settings
        assert not exact['workshop']['changed'] and exact['workshop']['storedParts']==0
        report['status']='passed'
    except Exception as error:
        report.update(status='failed',error=str(error),lastState=read()); raise
    finally:
        try: stop()
        finally:
            if controls: controls.close()
            report['elapsedSeconds']=round(time.monotonic()-began,3); persist()


if __name__=='__main__': main()
