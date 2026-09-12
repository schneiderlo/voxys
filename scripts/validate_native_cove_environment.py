#!/usr/bin/env python3
"""One bounded real-control native scenery route from a copied old ACT v6 save.

Reads the retained save, copies its exact current/mirror pair into a new isolated
root, resumes through the real app, walks the workshop stairs, tests a wall and
camera clearance, then saves and restarts once. No state setters. Images are
disabled by default; --capture-start explicitly requests one native startup
capture and normal exit before the image-free control journey.
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
from validate_native_cove_character import checkpoint
from validate_native_cove_saves import archive_payload


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('binary','output','storage-root','source-slot'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--seconds',type=int,default=240)
    parser.add_argument('--width',type=int,default=960)
    parser.add_argument('--height',type=int,default=800)
    parser.add_argument('--capture-start',type=Path,help='Explicit one-shot native startup PNG before controls')
    parser.add_argument('--restore-only-from',type=Path,help='Read-only continuation bound to a prior verified workshop checkpoint summary')
    args=parser.parse_args()
    if args.restore_only_from and args.capture_start:parser.error('read-only continuation does not capture images')
    if not 60<=args.seconds<=420:parser.error('seconds must be60..420')
    if not 640<=args.width<=2560 or not 480<=args.height<=1440:parser.error('bounded viewport640..2560 by480..1440 required')
    args.binary=args.binary.resolve(strict=True);args.source_slot=args.source_slot.resolve(strict=True)
    args.output=args.output.resolve();args.storage_root=args.storage_root.resolve()
    args.output.mkdir(parents=True,exist_ok=False);args.storage_root.mkdir(parents=True,mode=0o700,exist_ok=False)
    if args.capture_start:
        args.capture_start=args.capture_start.resolve()
        if args.capture_start.exists() or args.capture_start.suffix.lower()!='.png':parser.error('capture requires a new PNG path')
        args.capture_start.parent.mkdir(parents=True,exist_ok=True)
    source,payload=checkpoint(args.source_slot)
    source_physical=archive_payload(payload,source['world'])
    source_build=source['owned']['builds'][0]
    source_build_id=str(int.from_bytes(bytes.fromhex(source_build['id'])[16:24],'little'))
    source_revision=str(int.from_bytes(bytes.fromhex(source_build['revision']),'little'))
    assert source['schema']==6 and source['owned']['inventory']==[48,0]
    assert args.source_slot.name==source['world']
    source_feet=list(struct.unpack_from('<3d',payload,268));source_mode=payload[316]
    assert payload[317]==0 and source_mode==0
    continuation=None
    if args.restore_only_from:
        args.restore_only_from=args.restore_only_from.resolve(strict=True)
        continuation=json.loads(args.restore_only_from.read_text())
        prior=continuation['lastCheckpoint']
        assert any(v['name']=='workshop-position-durably-saved' for v in continuation['stages'])
        assert source['world']==prior['archive']['world'] and source['owned']==prior['archive']['owned']
        assert source['generation']>=prior['archive']['generation']
        assert math.dist(source_feet,prior['state']['player']['feet'])<1e-4
    else:
        # The original ACT checkpoint is the real rescue at the dock, off boat.
        assert math.dist(source_feet,[6,1.285,-49])<.02
    replicas={name:(args.source_slot/name).read_bytes() for name in ('current','mirror')}
    slot=args.storage_root/source['world'];slot.mkdir(mode=0o700)
    for name,data in replicas.items():
        assert not (args.source_slot/name).is_symlink()
        (slot/name).write_bytes(data)
    copied,_=checkpoint(slot);assert copied==source
    report=dict(status='running',kind='Copied old v6 world, real stairs/wall/camera controls, save and restart',
        startedUtc=datetime.now(timezone.utc).isoformat(),binary=str(args.binary),
        binarySha256=hashlib.sha256(args.binary.read_bytes()).hexdigest(),storageRoot=str(args.storage_root),
        sourceSlot=str(args.source_slot),sourceArchive=source,copiedArchive=copied,stages=[],route=[],
        viewport=[args.width,args.height],captureRequested=bool(args.capture_start))
    if continuation:
        report.update(kind='Read-only restart of prior verified workshop checkpoint',continuedFrom=str(args.restore_only_from),
            priorSummarySha256=hashlib.sha256(args.restore_only_from.read_bytes()).hexdigest(),inputActions=0,focusRequired=False)
    began=time.monotonic();child=stream=controls=window=None;index=0;held=set();checked=set()

    def persist():
        report['elapsedSeconds']=round(time.monotonic()-began,3)
        (args.output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
    def read():
        try:return json.loads((args.output/f'observation-{index}'/'state.json').read_text())
        except (OSError,json.JSONDecodeError):return {}
    def frame(s):return int(s.get('assetFixture',{}).get('submittedSerial',0))
    def wait(predicate,label,seconds=30):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            if time.monotonic()-began>args.seconds:raise RuntimeError('Bounded journey deadline: '+label)
            if child and child.poll() is not None:raise RuntimeError(f'Owned game exited{child.returncode}: '+label)
            s=read();assert not s.get('failed'),s
            if predicate(s):return s
            time.sleep(.015)
        raise RuntimeError(label+': '+json.dumps(read()))
    def fresh():
        before=frame(read());return wait(lambda s:frame(s)>=before+2,'fresh observation after release')
    def down(key):controls.state(window,key,True);held.add(key)
    def up(key):controls.state(window,key,False);held.discard(key)
    def key(name):
        down(name)
        try:time.sleep(.2)
        finally:up(name)
        return fresh()
    def hold(names,ticks):
        before=int(read()['player']['tick'])
        try:
            for name in names:down(name)
            value=wait(lambda s:int(s['player']['tick'])>=before+ticks,'held real movement',10)
        finally:
            release_tick=int(read().get('player',{}).get('tick',before))
            for name in names:up(name)
        # Render serials can advance while their observed player packet is
        # held. Navigation must see a post-release player update as well.
        wait(lambda s:int(s['player']['tick'])>release_tick,'fresh player position after movement release',10)
        fresh();return value
    def identity(s):
        b=s['boat'];m=b['mechanisms']
        return [b['physicsTicks']['incarnation'],b['buildId'],b['topologyRevision'],
                [r['key'] for r in b['roots']],m['bodyIndex'],m['bodyGeneration']]
    def valid(s,near_wall=False):
        camera=s.get('characterCamera',{})
        expected_draws=0 if near_wall and camera.get('actualDistance',12)<.65 else 34
        return bool(s.get('ready') and s.get('boat',{}).get('environmentCollision')
            and camera.get('valid')
            and s['characterCamera']['geometryTick']==s['characterCamera']['presentedTick']
            and s.get('character',{}).get('draws')==expected_draws)
    def physical(s,near_wall=False):
        b=s['boat'];f=s['assetFixture'];h=s['nativeHud'];c=s['characterCamera']
        assert s['world']==source['world'] and b['buildId']==source_build_id and b['topologyRevision']==source_revision
        assert [r['key'] for r in b['roots']]==source_physical['rootKeys'],b['roots']
        assert b['active'] and b['environmentCollision'] and b['environmentProxies']==39,b
        assert b['parts']==11 and b['massKg']==1035 and b['paidPartIds']==[],b
        assert s['session']['inventory']=={'salvageMaterial':'48','specialMachinery':'0'}
        assert f['sceneryGpuBytes']==1241888 and 19<=f['sceneryDraws']<=20,f
        assert not s['harbor']['installed'],s['harbor']
        assert f['externalGpuReserve']==33328 and int(f['gpuReservationBytes'])<=16*1024*1024,f
        assert f['sceneSunShadows'] and f['environmentReady'] and s['terrainSurface']=='lego'
        assert h['enabled'] and h['lastEncodedQuads']>0 and h['bodyPixels']>=16 and not h['truncated'],h
        expected_draws=0 if near_wall and c['actualDistance']<.65 else 34
        assert s['character']['draws']==expected_draws and c['valid'] and int(c['geometryTick'])==int(c['presentedTick'])>0
        assert all(math.isfinite(v) for v in s['player']['feet']+c['eye']) and 0<c['actualDistance']<=12.001
        assert b['physicsTicks']['supported'] and not b['physicsTicks']['failed']
    def record(name,state=None,near_wall=False,**extra):
        state=state or wait(lambda s:valid(s,near_wall),name);physical(state,near_wall)
        serial=frame(state);tick=int(state['characterCamera']['presentedTick']);owner=identity(state)
        proof=wait(lambda s:valid(s,near_wall) and identity(s)==owner
            and int(s['assetFixture']['completedSerial'])>=serial
            and int(s['boat']['physicsTicks']['completed'])>=tick,name+' actual completion')
        report['stages'].append(dict(name=name,process=index,state=state,completion=dict(
            submittedFrame=serial,completedFrame=int(proof['assetFixture']['completedSerial']),
            presentedTick=tick,completedTick=int(proof['boat']['physicsTicks']['completed']),owner=owner),
            nearWallHidePermitted=near_wall,**extra))
        print(name,flush=True);persist();return state
    def settings(s):
        return {k:s['characterCamera'][k] for k in ('mode','distance','reducedMotion','frameLoad')}
    def start(focus_needed=True):
        nonlocal index,child,stream,window
        index+=1;stream=(args.output/f'process-{index}.log').open('w')
        command=[str(args.binary),'--config','salvage_cove.cfg','--uncapped','--width',str(args.width),'--height',str(args.height),
            '--expedition-root',str(args.storage_root),'--expedition-world',source['world'],
            '--expedition-observe',str(args.output/f'observation-{index}')]
        report.setdefault('commands',[]).append(command);persist()
        child=subprocess.Popen(command,stdout=stream,stderr=subprocess.STDOUT,env={**os.environ,'VOXY_WINDOW_BACKEND':'x11'})
        wait(lambda _:controls.own_window(child.pid),'owned X11 window',60);window=controls.own_window(child.pid)
        if focus_needed:
            controls.x.XRaiseWindow(controls.display,window);controls.x.XSetInputFocus(controls.display,window,2,0)
            controls.x.XFlush(controls.display)
            def focused(_):
                value,revert=C.c_ulong(),C.c_int();controls.x.XGetInputFocus(controls.display,C.byref(value),C.byref(revert))
                return value.value==window
            wait(focused,'real window focus')
        wait(lambda s:valid(s) and s['restore']['phase']=='ready' and s['pause']['phase']=='paused',
            'old world restored with observed environment',60)
    def stop():
        nonlocal child,stream,window
        if not child:return
        prior=child.poll();requested=False;killed=False
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
        if index not in checked:
            checked.add(index)
            pattern=re.compile(r'uncaptured(?:\s+WebGPU)?(?:\s+GPU)?\s+error|WebGPU validation error|'
                r'(?:WGPU|WebGPU)[^\n]{0,100}(?:error|validation failed)|\[(?:error|fatal)\]',re.I)
            errors=[line for line in (args.output/f'process-{index}.log').read_text(errors='replace').splitlines() if pattern.search(line)]
            report.setdefault('processChecks',[]).append(dict(process=index,exitBeforeStop=prior,exitCode=code,
                requestedTermination=requested,forceKilled=killed,errorLines=errors));persist()
            assert requested and not killed and code in (0,-signal.SIGTERM) and not errors,report['processChecks'][-1]
    def direction(s,dx,dz):
        d=math.hypot(dx,dz);assert d>0
        yaw=s['characterCamera']['yaw'];f=(-dx*math.sin(yaw)-dz*math.cos(yaw))/d
        r=(dx*math.cos(yaw)-dz*math.sin(yaw))/d
        return (['w' if f>0 else 's'] if abs(f)>.4 else [])+(['d' if r>0 else 'a'] if abs(r)>.4 else [])
    def walk(target,label,seconds=30):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            s=read();p=s['player']['feet'];dx,dz=target[0]-p[0],target[1]-p[2];distance=math.hypot(dx,dz)
            report['route'].append(dict(label=label,feet=p,mode=s['player']['mode'],tick=s['player']['tick']))
            if distance<.27:return s
            hold(direction(s,dx,dz),max(1,min(4,int(distance/.06)-1)))
        persist();raise RuntimeError('Actual approach blocked: '+label)
    def menu(prefix):
        view=read()['nativeMenu'];assert view['open'] and view['page']=='player-camera'
        choices=[i for i,row in enumerate(view['rows']) if row['label'].startswith(prefix)]
        assert len(choices)==1 and view['rows'][choices[0]]['enabled'],(prefix,view)
        target=choices[0]
        for _ in range(8):
            current=read()['nativeMenu']['selected']
            if current==target:break
            key('Down' if current<target else 'Up')
            wait(lambda s:s['nativeMenu']['selected']!=current,'camera menu row')
        assert read()['nativeMenu']['selected']==target;key('Return')
    def open_menu():
        key('F2');wait(lambda s:s['nativeMenu']['open'] and s['nativeMenu']['page']=='player-camera','real camera menu')
    def close_menu():
        menu('Back to game');wait(lambda s:not s['nativeMenu']['open'],'camera menu closed')

    try:
        if args.capture_start:
            command=[str(args.binary),'--config','salvage_cove.cfg','--uncapped','--width',str(args.width),'--height',str(args.height),
                '--expedition-root',str(args.storage_root),'--expedition-world',source['world'],
                '--expedition-observe',str(args.output/'capture-observation'),
                '--screenshot',str(args.capture_start),'--screenshot-frames','120']
            with (args.output/'capture-process.log').open('w') as log:
                capture=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=60,
                    env={**os.environ,'VOXY_WINDOW_BACKEND':'x11'})
            report['capture']=dict(command=command,exitCode=capture.returncode,path=str(args.capture_start))
            persist();assert capture.returncode==0,report['capture']
            image=args.capture_start.read_bytes()
            assert image[:8]==b'\x89PNG\r\n\x1a\n' and struct.unpack_from('>II',image,16)==(args.width,args.height)
            report['capture'].update(bytes=len(image),sha256=hashlib.sha256(image).hexdigest(),
                inspection='PNG signature/dimensions only; visual inspection remains owner work')
            assert checkpoint(slot)[0]['owned']==source['owned'];persist()
        controls=Controls()
        for name,types,result in (
            ('XRaiseWindow',[C.c_void_p,C.c_ulong],C.c_int),
            ('XSetInputFocus',[C.c_void_p,C.c_ulong,C.c_int,C.c_ulong],C.c_int),
            ('XGetInputFocus',[C.c_void_p,C.POINTER(C.c_ulong),C.POINTER(C.c_int)],C.c_int)):
            fn=getattr(controls.x,name);fn.argtypes=types;fn.restype=result
        if continuation:
            saved_state=continuation['lastCheckpoint']['state']
            start(focus_needed=False)
        else:
            start();restored=record('old-act-v6-world-restored-with39-environment-solids')
            actual,_=checkpoint(slot);assert actual['owned']==source['owned']
            for name in ('mode','reducedMotion','frameLoad'):assert settings(restored)[name]==source['character'][name]
            assert abs(settings(restored)['distance']-source['character']['distance'])<1e-5
            assert math.dist(restored['player']['feet'],source_feet)<1e-4 and not restored['player']['onBoat']
            key('p');wait(lambda s:s['pause']['phase']=='running','resume old world')
            # Orbit mode keeps a stable movement basis while the approach is tested.
            if read()['characterCamera']['mode']!='orbit':
                open_menu();menu('View:');wait(lambda s:s['characterCamera']['mode']=='orbit','actual orbit choice');close_menu()
            walk([6,-43.2],'dock north approach');walk([7.4,-41],'first tread approach')
            base=record('real-workshop-stair-approach')
            start_route=len(report['route']);walk([10.7,-41],'four stair ascent')
            upstairs=record('four-stair-workshop-entry')
            ascent=report['route'][start_route:]
            assert base['player']['feet'][0]<8 and upstairs['player']['feet'][0]>10.35
            assert all(7.0<=p['feet'][0]<=11.1 and -41.4<p['feet'][2]<-40.6 for p in ascent),ascent
            assert upstairs['player']['mode']=='walking' and abs(upstairs['player']['feet'][1]-2.565)<.025,upstairs['player']
            assert upstairs['player']['feet'][1]>base['player']['feet'][1]+.8
            report['ascent']=dict(treadsCrossed=[8,8.5,9,9.5],finalHeight=upstairs['player']['feet'][1],samples=ascent)
            walk([12,-39.5],'open workshop floor');walk([15.1,-39.5],'east wall approach')
            before=read();hold(direction(before,1,0),30)
            # The camera intentionally hides the avatar when unavoidable wall
            # occlusion shortens its actual distance below0.65m. Only this contact
            # stage permits that exact outcome; every other capture requires34.
            blocked=record('actual-east-wall-blocks-walking',near_wall=True)
            assert before['player']['feet'][0]>14.8 and abs(blocked['player']['feet'][0]-15.455)<.015,blocked['player']
            assert blocked['player']['feet'][0]-before['player']['feet'][0]<.5
            # Camera-relative keyboard input slides along the wall while blocked.
            # Keep the complete capsule within the real wall's Z extent.
            assert -41.38<blocked['player']['feet'][2]<-36.62 and blocked['player']['mode']=='walking'
            # Move away while facing west, then request the real camera's eastward
            # follow direction. The wall must shorten it, without moving the robot.
            walk([14.65,-39.5],'camera wall clearance position')
            visible=wait(valid,'avatar visible again away from wall');assert visible['character']['draws']==34
            open_menu();menu('Recenter behind robot')
            shortened=wait(lambda s:valid(s) and s['characterCamera']['actualDistance']<s['characterCamera']['distance']-.4,
                'camera shortens before the real workshop wall')
            camera=record('camera-recenter-stops-before-wall',shortened)
            eye=camera['characterCamera']['eye'];feet=camera['player']['feet']
            assert feet[0]+.1<eye[0]<15.76 and -41.68<eye[2]<-36.32 and 2.56<eye[1]<5.44,(feet,eye)
            close_menu();key('p');wait(lambda s:s['pause']['phase']=='paused','joined pause inside workshop')
            generation=checkpoint(slot)[0]['generation'];key('F10')
            def saved(s):
                if s.get('pause',{}).get('phase')!='paused':return False
                try:
                    meta,_=checkpoint(slot);return meta['generation']>generation and str(meta['tick'])==s['pause']['tick']
                except (OSError,AssertionError):return False
            wait(saved,'actual mirrored workshop checkpoint',45)
            saved_state=record('workshop-position-durably-saved');saved_meta,saved_payload=checkpoint(slot)
            assert saved_meta['owned']==source['owned'];(args.output/'workshop.svce').write_bytes(saved_payload)
            report['lastCheckpoint']=dict(archive=saved_meta,state=saved_state);persist();stop();start(focus_needed=False)
        final=record('workshop-save-restored-with-exact-owned-design-and-collision')
        final_meta,_=checkpoint(slot);assert final_meta['owned']==source['owned']
        assert math.dist(final['player']['feet'],saved_state['player']['feet'])<1e-4
        assert final['player']['mode']==saved_state['player']['mode'] and settings(final)==settings(saved_state)
        report['finalArchive']=final_meta;report['status']='passed';persist()
    except Exception as error:
        report['status']='failed';report['error']=str(error);report['failureState']=read();persist();raise
    finally:
        try:stop()
        except Exception as error:report['status']='failed';report['cleanupError']=str(error)
        if controls:controls.close()
        unchanged=all((args.source_slot/name).read_bytes()==data for name,data in replicas.items())
        report['sourceArchiveUnchanged']=unchanged
        if not unchanged:report['status']='failed';report['sourceError']='Original save changed during isolated journey'
        persist()
    if report['status']!='passed':raise SystemExit(1)


if __name__=='__main__':main()
