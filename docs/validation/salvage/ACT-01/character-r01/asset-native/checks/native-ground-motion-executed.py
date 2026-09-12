#!/usr/bin/env python3
"""Bounded real-control native robot/camera/moving-deck journey; no images.

Ground and motion phases each leave a real durable world checkpoint. A failed
later phase can continue from that checkpoint without repeating completed work.
Only owned X11 events and read-only observations are used. No save setters,
debug actions, fabricated animation poses or gameplay-state file modifications.
"""
import argparse
import ctypes as C
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

WIDTH, HEIGHT = 960, 800


def checkpoint(slot):
    """Read and hash the actual v6 archive and frozen logical ownership rows."""
    envelope=(slot/'current').read_bytes()
    assert envelope==(slot/'mirror').read_bytes(), 'Mirrored durable generations differ'
    assert envelope[:8]==b'SVSG\1\0\0\0'
    assert hashlib.sha256(envelope[:-32]).digest()==envelope[-32:]
    payload=envelope[40:-32]
    assert len(payload)==int.from_bytes(envelope[32:40],'little')
    assert payload[:8]==b'SVCE\6\0\0\0', 'Character journey requires the actual v6 save'
    assert hashlib.sha256(payload[:-32]).digest()==payload[-32:]
    at=441  # Unchanged v1 physical fields + v2 harbor block; v3 additions follow.
    data=payload
    def take(n):
        nonlocal at
        assert 0<=n<=4*1024*1024 and at+n<=len(data)-32
        result=data[at:at+n];at+=n;return result
    def count(maximum):
        n=int.from_bytes(take(4),'little');assert n<=maximum;return n
    recovery=[hashlib.sha256(take(count(131072))).hexdigest() for _ in range(count(4))]
    take(48);roots=count(32);assert roots>0;take(88*roots)
    # This scoped first-job fixture has no parked second-job cargo records.
    assert count(4)==0, 'Unexpected additional cargo content in ACT fixture'
    character=take(47)
    profile,*fields=struct.unpack('<I5d3B',character)
    assert profile==1 and all(math.isfinite(v) for v in fields[:5]) and all(v in (0,1) for v in fields[5:])
    session=take(count(4*1024*1024))
    assert session[:4]==b'SVSC' and 1<=int.from_bytes(session[4:8],'little')<=3
    assert hashlib.sha256(session[:-32]).digest()==session[-32:]
    data=session;at=8+4+68
    world=take(16).hex();take(48+8+8+16)
    inventory=list(struct.unpack('<QQ',take(16)));take(64);assert take(1)==b'\1'
    builds=[]
    for _ in range(count(32)):
        identity,revision,owner=take(24),take(8),take(24)
        lease=take(1);assert lease in (b'\0',b'\1')
        if lease==b'\1':take(40)
        parts=[take(130) for _ in range(count(256))]
        connections=[take(136) for _ in range(count(1024))]
        assert parts and all(p[:16].hex()==world for p in parts)
        builds.append(dict(id=identity.hex(),revision=revision.hex(),owner=owner.hex(),
            partIds=[p[:24].hex() for p in parts],parts=len(parts),connections=len(connections),
            partsSha256=hashlib.sha256(b''.join(parts)).hexdigest(),
            connectionsSha256=hashlib.sha256(b''.join(connections)).hexdigest()))
    assert len(builds)==1 and builds[0]['parts']==11
    return dict(world=world,generation=int.from_bytes(envelope[24:32],'little'),
        tick=int.from_bytes(payload[8:16],'little'),schema=6,
        payloadSha256=hashlib.sha256(payload).hexdigest(),envelopeSha256=hashlib.sha256(envelope).hexdigest(),
        character=dict(worldVelocity=fields[:3],facingYaw=fields[3],distance=fields[4],
            mode='chase' if fields[5] else 'orbit',reducedMotion=bool(fields[6]),frameLoad=bool(fields[7])),
        characterBytesSha256=hashlib.sha256(character).hexdigest(),
        owned=dict(world=world,inventory=inventory,builds=builds,recoveryDesignDigests=recovery)),payload


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--storage-root',type=Path,required=True)
    parser.add_argument('--continue-from',type=Path,help='Earlier summary.json with an actual durable checkpoint')
    parser.add_argument('--phase',choices=('ground','motion','restore','all'),default='all')
    parser.add_argument('--seconds',type=int,default=420)
    args=parser.parse_args()
    if not 60<=args.seconds<=600:parser.error('seconds must be 60..600')
    args.binary=args.binary.resolve(strict=True);args.output=args.output.resolve()
    args.storage_root=args.storage_root.resolve();args.output.mkdir(parents=True,exist_ok=False)
    prior=json.loads(args.continue_from.read_text()) if args.continue_from else None
    if prior:
        assert prior.get('lastCheckpoint') and Path(prior['storageRoot'])==args.storage_root
        assert args.storage_root.is_dir()
    else:args.storage_root.mkdir(parents=True,mode=0o700,exist_ok=False)
    report=dict(status='running',kind='Native ACT robot, camera and moving-deck real controls; no images',
        startedUtc=datetime.now(timezone.utc).isoformat(),binary=str(args.binary),
        binarySha256=hashlib.sha256(args.binary.read_bytes()).hexdigest(),storageRoot=str(args.storage_root),
        phase=args.phase,stages=[],completedPhases=list(prior.get('completedPhases',[])) if prior else [])
    if prior:
        report['continues']=str(args.continue_from.resolve());report['lastCheckpoint']=prior['lastCheckpoint']
    controls=child=stream=window=None;index=0;held=set();began=time.monotonic()
    seen_clips=set();checked_processes=set()

    def persist():
        report['elapsedSeconds']=round(time.monotonic()-began,3)
        report['observedClips']=sorted(seen_clips)
        (args.output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
    def read():
        try:return json.loads((args.output/f'observation-{index}'/'state.json').read_text())
        except (OSError,json.JSONDecodeError):return {}
    def wait(predicate,label,seconds=30):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            if time.monotonic()-began>args.seconds:raise RuntimeError('Bounded action budget reached: '+label)
            if child and child.poll() is not None:raise RuntimeError(f'Owned native process exited {child.returncode}: {label}')
            state=read();assert not state.get('failed'),state
            if state.get('character',{}).get('clip'):seen_clips.add(state['character']['clip'])
            if predicate(state):return state
            time.sleep(.015)
        raise RuntimeError(label+': '+json.dumps(read()))
    def frame(s):return int(s.get('assetFixture',{}).get('submittedSerial',0))
    def fresh():
        before=frame(read());return wait(lambda s:frame(s)>=before+2,'fresh post-release observation')
    def down(name):controls.state(window,name,True);held.add(name)
    def up(name):controls.state(window,name,False);held.discard(name)
    def key(name):
        down(name)
        try:time.sleep(.20)
        finally:up(name)
        return fresh()
    def hold(names,ticks,predicate=None):
        before=int(read()['player']['tick'])
        try:
            for name in names:down(name)
            result=wait(lambda s:int(s['player']['tick'])>=before+ticks and (predicate is None or predicate(s)),
                        'actual held movement',15)
        finally:
            for name in names:up(name)
        fresh();return result
    def identity(s):
        b=s['boat'];m=b['mechanisms']
        return (b['physicsTicks']['incarnation'],b['buildId'],b['topologyRevision'],
                tuple(r['key'] for r in b['roots']),m['bodyIndex'],m['bodyGeneration'])
    def physical(s):
        b=s['boat'];f=s['assetFixture'];h=s['nativeHud']
        assert b['active'] and b['parts']==11 and b['massKg']==1035 and b['paidPartIds']==[],b
        assert s['session']['inventory']=={'salvageMaterial':'48','specialMachinery':'0'},s['session']
        assert b['physicsTicks']['supported'] and not b['physicsTicks']['failed']
        assert s['terrainSurface']=='lego' and f['sceneSunShadows'] and f['environmentReady']
        assert f['presentationParts']==9 and int(f['gpuReservationBytes'])<=16*1024*1024,f
        assert h['enabled'] and h['lastEncodedQuads']>0 and h['bodyPixels']>=16 and not h['truncated'],h
        c=s['characterCamera'];assert c['valid'] and c['actualDistance']>0 and c['actualDistance']<=12.001,c
        assert int(c['geometryTick'])==int(c['presentedTick'])>0,c
        assert s['character']['draws']==34,s['character']
        assert all(math.isfinite(v) for v in s['character']['worldVelocity']+c['eye'])
    def record(name,state=None,**extra):
        state=state or wait(lambda s:s.get('ready') and s.get('character',{}).get('draws')==34
            and s.get('characterCamera',{}).get('valid') and s['characterCamera']['geometryTick']==s['characterCamera']['presentedTick'],name)
        physical(state);stamp=frame(state);tick=int(state['characterCamera']['presentedTick']);owner=identity(state)
        completed=wait(lambda s:identity(s)==owner and int(s['assetFixture']['completedSerial'])>=stamp
            and int(s['boat']['physicsTicks']['completed'])>=tick,name+' GPU and physics completion')
        report['stages'].append(dict(name=name,process=index,state=state,
            completion=dict(frame=stamp,tick=tick,completedFrame=int(completed['assetFixture']['completedSerial']),
                completedTick=int(completed['boat']['physicsTicks']['completed']),owner=owner),**extra))
        print(name,flush=True);persist();return state
    def focus():
        controls.x.XRaiseWindow(controls.display,window)
        controls.x.XSetInputFocus(controls.display,window,2,0);controls.x.XFlush(controls.display)
        def focused(_):
            value,revert=C.c_ulong(),C.c_int()
            controls.x.XGetInputFocus(controls.display,C.byref(value),C.byref(revert));return value.value==window
        wait(focused,'owned native window focus');fresh()
    def start(world=None):
        nonlocal child,stream,window,index
        index+=1;stream=(args.output/f'process-{index}.log').open('w')
        command=[str(args.binary),'--config','salvage_cove.cfg','--uncapped','--width',str(WIDTH),'--height',str(HEIGHT),
            '--expedition-root',str(args.storage_root),'--expedition-observe',str(args.output/f'observation-{index}')]
        if world:command+=['--expedition-world',world]
        report.setdefault('commands',[]).append(command);persist()
        child=subprocess.Popen(command,stdout=stream,stderr=subprocess.STDOUT,env={**os.environ,'VOXY_WINDOW_BACKEND':'x11'})
        wait(lambda _:controls.own_window(child.pid),'owned native window',60);window=controls.own_window(child.pid)
        wait(lambda s:s.get('ready') and s.get('boat',{}).get('active') and s.get('characterCamera'),'native character ready',60)
        focus()
        if world:wait(lambda s:s.get('restore',{}).get('phase')=='ready' and s['pause']['phase']=='paused','durable world restored paused')
    def stop():
        nonlocal child,stream,window
        if not child:return
        requested=False;killed=False;exit_before=child.poll()
        try:
            for name in tuple(held):up(name)
            if child.poll() is None:
                requested=True;child.send_signal(signal.SIGTERM)
                try:child.wait(timeout=15)
                except subprocess.TimeoutExpired:killed=True;child.kill();child.wait(timeout=5)
            code=child.returncode
        finally:
            if stream:stream.close()
            child=stream=window=None
        if index not in checked_processes:
            checked_processes.add(index)
            pattern=re.compile(r'uncaptured(?:\s+WebGPU)?(?:\s+GPU)?\s+error|WebGPU validation error|'
                r'(?:WGPU|WebGPU)[^\n]{0,100}(?:error|validation failed)|\[(?:error|fatal)\]',re.I)
            lines=(args.output/f'process-{index}.log').read_text(errors='replace').splitlines()
            errors=[line for line in lines if pattern.search(line)]
            report.setdefault('processChecks',[]).append(dict(process=index,exitCode=code,exitBeforeStop=exit_before,
                requestedTermination=requested,forceKilled=killed,errorLines=errors));persist()
            assert requested and not killed and code in (0,-signal.SIGTERM) and not errors,report['processChecks'][-1]
    def select(prefix):
        menu=read()['nativeMenu'];assert menu['open'] and menu['page']=='player-camera',menu
        choices=[i for i,row in enumerate(menu['rows']) if row['label'].startswith(prefix)]
        assert len(choices)==1,(prefix,menu);target=choices[0];assert menu['rows'][target]['enabled']
        for _ in range(8):
            current=read()['nativeMenu']['selected']
            if current==target:break
            key('Down' if current<target else 'Up')
            wait(lambda s:s['nativeMenu']['selected']!=current,'camera menu selection')
        assert read()['nativeMenu']['selected']==target
        key('Return')
    def settings(s):
        c=s['characterCamera'];return {k:c[k] for k in ('mode','distance','reducedMotion','frameLoad')}
    def angle_error(a,b):return math.atan2(math.sin(a-b),math.cos(a-b))
    def direction(s,dx,dz):
        distance=math.hypot(dx,dz)
        if distance<1e-9:return []
        yaw=s['characterCamera']['yaw'];forward=(-dx*math.sin(yaw)-dz*math.cos(yaw))/distance
        right=(dx*math.cos(yaw)-dz*math.sin(yaw))/distance
        names=[]
        if abs(forward)>.4:names.append('w' if forward>0 else 's')
        if abs(right)>.4:names.append('d' if right>0 else 'a')
        return names
    def walk(target,reached=lambda _:False,seconds=35):
        end=time.monotonic()+seconds;trace=[]
        while time.monotonic()<end:
            state=read()
            if reached(state):return state
            p=state['player']['feet'];t=target(state) if callable(target) else target
            dx,dz=t[0]-p[0],t[1]-p[2];distance=math.hypot(dx,dz)
            if distance<.28:return state
            names=direction(state,dx,dz);trace.append(dict(feet=p,target=t,keys=names,tick=state['player']['tick']))
            hold(names,max(1,min(5,int(distance/.06)-1)))
        report['failedApproach']=trace;persist();raise RuntimeError('Real movement route did not reach its interaction')
    def rotated(s,vector):
        x,y,z,w=s['boat']['orientation'];a=vector
        t=[2*(y*a[2]-z*a[1]),2*(z*a[0]-x*a[2]),2*(x*a[1]-y*a[0])]
        cross=[y*t[2]-z*t[1],z*t[0]-x*t[2],x*t[1]-y*t[0]]
        return [a[i]+w*t[i]+cross[i] for i in range(3)]
    def boarding_point(s):
        offset=rotated(s,[2.1,0,.1]);helm=s['boat']['helmPosition']
        return [helm[0]+offset[0],helm[2]+offset[2]]
    def save_phase(phase,automatic_after=None):
        s=read();slot=args.storage_root/s['world']
        before=checkpoint(slot)[0]['generation'] if (slot/'current').exists() else 0
        if automatic_after is None:
            if s['pause']['phase']!='paused':
                wait(lambda n:n['pause']['canPause'],'safe manual pause');key('p')
                wait(lambda n:n['pause']['phase']=='paused','joined pause')
            key('F10');minimum=before
        else:minimum=automatic_after
        def durable(n):
            if n['pause']['phase']!='paused' or n['rescue']['pending']:return False
            try:
                meta,_=checkpoint(slot)
                return meta['generation']>minimum and str(meta['tick'])==n['pause']['tick']
            except (OSError,AssertionError):return False
        wait(durable,'actual mirrored v6 checkpoint',45)
        meta,payload=checkpoint(slot);s=record(phase+'-durable-checkpoint')
        for field,value in settings(s).items():
            if field=='distance':assert abs(value-meta['character'][field])<1e-5
            else:assert value==meta['character'][field]
        (args.output/(phase+'.svce')).write_bytes(payload)
        if phase not in report['completedPhases']:report['completedPhases'].append(phase)
        report['lastCheckpoint']=dict(phase=phase,archive=meta,state=s);persist();return s
    def resume():
        key('p');wait(lambda s:s['pause']['phase']=='running','resume expedition')

    try:
        controls=Controls()
        for name,types,result in (
            ('XRaiseWindow',[C.c_void_p,C.c_ulong],C.c_int),
            ('XSetInputFocus',[C.c_void_p,C.c_ulong,C.c_int,C.c_ulong],C.c_int),
            ('XGetInputFocus',[C.c_void_p,C.POINTER(C.c_ulong),C.POINTER(C.c_int)],C.c_int)):
            fn=getattr(controls.x,name);fn.argtypes=types;fn.restype=result
        world=report.get('lastCheckpoint',{}).get('archive',{}).get('world')
        start(world);record('continued-durable-world' if world else 'fresh-visible-robot')
        if read()['pause']['phase']=='paused' and args.phase!='restore':resume()
        if args.phase in ('ground','all') and 'ground' not in report['completedPhases']:
            before=read();down('w')
            try:
                a=record('robot-walk-before',wait(lambda s:s['character']['clip']=='walk' and s['character']['draws']==34,'real walk clip'))
                b=record('robot-walk-after',wait(lambda s:s['character']['clip']=='walk' and s['character']['time']>a['character']['time']+.12,'walk animation advances'))
            finally:up('w');fresh()
            assert math.dist(a['player']['feet'],b['player']['feet'])>.15
            wait(lambda s:s['character']['clip']=='idle','stationary idle')
            down('space')
            try:jump=wait(lambda s:s['player']['mode']=='airborne' and s['character']['clip']=='jump','jump clip')
            finally:up('space')
            record('ground-jump',jump)
            record('ground-fall',wait(lambda s:s['player']['mode']=='airborne' and s['character']['clip']=='fall','fall clip'))
            record('ground-land',wait(lambda s:s['player']['mode']=='walking' and s['character']['clip']=='land','landing clip'))
            wait(lambda s:s['character']['clip']=='idle','settled idle after landing')
            key('F2');wait(lambda s:s['nativeMenu']['open'] and s['nativeMenu']['page']=='player-camera','camera options visible')
            original=settings(read());select('View:')
            wait(lambda s:s['characterCamera']['mode']!=original['mode'],'camera mode changed')
            distance=read()['characterCamera']['distance'];select('Move camera closer')
            wait(lambda s:s['characterCamera']['distance']<distance-.1,'native menu zoom accepted')
            select('Frame load:');wait(lambda s:s['characterCamera']['frameLoad']!=original['frameLoad'],'load framing toggled')
            select('Reduced motion:');wait(lambda s:s['characterCamera']['reducedMotion']!=original['reducedMotion'],'reduced motion toggled')
            select('Recenter behind robot')
            wait(lambda s:abs(angle_error(s['characterCamera']['yaw'],s['character']['facingYaw']))<1e-4,'native menu recentered actual camera')
            record('native-camera-options-applied',before=original,after=settings(read()))
            select('Back to game');wait(lambda s:not s['nativeMenu']['open'],'camera menu closed')
            save_phase('ground')
            if args.phase=='all':resume()
        if args.phase in ('motion','all') and 'motion' not in report['completedPhases']:
            if read()['pause']['phase']=='paused':resume()
            near=lambda s:s['player']['interaction']=='board'
            walk([6,-49.5],near);walk([4.5,-49.5],near);walk([4.5,-53],near)
            wait(near,'real dock boarding prompt');key('e');wait(lambda s:s['player']['onBoat'],'board boat');record('boarded-robot')
            walk(lambda s:[s['boat']['helmPosition'][0],s['boat']['helmPosition'][2]],lambda s:s['player']['interaction']=='helm')
            wait(lambda s:s['player']['interaction']=='helm','real helm prompt');key('e')
            record('robot-at-helm',wait(lambda s:s['player']['mode']=='helm' and s['character']['clip']=='helm','helm clip'))
            driven=hold(['w'],90,lambda s:s['boat']['speed']>.4);record('moving-boat-drive',driven)
            key('e');wait(lambda s:s['player']['onBoat'] and s['player']['mode']=='walking','leave helm')
            walk(boarding_point,seconds=20)
            before=record('moving-deck-before-takeoff')
            velocity=before['character']['worldVelocity'];speed=math.hypot(velocity[0],velocity[2])
            assert before['player']['onBoat'] and speed>.2,'Moving support required before momentum check'
            down('space')
            try:jump=wait(lambda s:s['player']['mode']=='airborne' and not s['player']['onBoat'] and s['character']['clip']=='jump','detach by actual jump')
            finally:up('space')
            departed=jump['character']['worldVelocity'];error=math.hypot(departed[0]-velocity[0],departed[2]-velocity[2])
            assert error<max(.30,speed*.35),(velocity,departed,error)
            assert departed[0]*velocity[0]+departed[2]*velocity[2]>.5*speed*speed,(velocity,departed)
            record('moving-deck-inherited-jump',jump,beforeVelocity=velocity,afterVelocity=departed,horizontalError=error)
            outward=rotated(read(),[1,0,0]);names=direction(read(),outward[0],outward[2])
            try:
                for name in names:down(name)
                water=wait(lambda s:s['player']['mode']=='swimming' and s['character']['clip']=='swim','actual swim after leaving deck',12)
            finally:
                for name in names:up(name)
            record('robot-swimming',water);fresh()
            swim_start=read()['player']['feet'];walk(boarding_point,lambda s:s['player']['interaction']=='board',seconds=35)
            wait(lambda s:s['player']['interaction']=='board','water reboarding prompt',10)
            key('e');wait(lambda s:s['player']['onBoat'] and s['player']['mode']=='walking','reboard from water')
            record('robot-reboarded-from-water',swimStart=swim_start)
            slot=args.storage_root/read()['world'];generation=checkpoint(slot)[0]['generation'] if (slot/'current').exists() else 0
            rescues=int(read()['rescue']['completed']);key('r')
            wait(lambda s:int(s['rescue']['completed'])>rescues and not s['rescue']['pending']
                 and s['pause']['phase']=='paused','real rescue and durable acknowledgment',45)
            rescued=record('robot-rescued-with-owned-boat')
            assert not rescued['player']['onBoat'] and math.hypot(rescued['player']['feet'][0]-6,rescued['player']['feet'][2]+49)<.02
            save_phase('motion',automatic_after=generation)
        if args.phase in ('restore','all'):
            saved=report['lastCheckpoint'];world=saved['archive']['world']
            stop();start(world);restored=record('robot-camera-world-restored-paused')
            expected=saved['archive'];meta,_=checkpoint(args.storage_root/world)
            assert meta['owned']==expected['owned'],(meta['owned'],expected['owned'])
            assert settings(restored)==settings(saved['state']),(settings(restored),settings(saved['state']))
            assert restored['player']['mode']==saved['state']['player']['mode'] and restored['player']['onBoat']==saved['state']['player']['onBoat']
            assert math.dist(restored['player']['feet'],saved['state']['player']['feet'])<1e-4
            assert math.dist(restored['character']['worldVelocity'],saved['state']['character']['worldVelocity'])<1e-4
            assert abs(angle_error(restored['character']['facingYaw'],saved['state']['character']['facingYaw']))<1e-5
            before=read();stamp=frame(before);wait(lambda s:int(s['assetFixture']['completedSerial'])>=stamp+4,'paused completed frames')
            frozen=record('paused-character-and-camera-frozen')
            assert frozen['character']==before['character'] and frozen['player']==before['player']
            assert frozen['characterCamera']['eye']==before['characterCamera']['eye']
            if 'restore' not in report['completedPhases']:report['completedPhases'].append('restore')
        report['status']='passed';persist()
    except Exception as error:
        report['status']='failed';report['error']=str(error);report['failureState']=read();persist();raise
    finally:
        try:stop()
        except Exception as error:
            report['status']='failed';report['cleanupError']=str(error);persist()
        if controls:controls.close()
        persist()
    if report['status']!='passed':raise SystemExit(1)


if __name__=='__main__':main()
