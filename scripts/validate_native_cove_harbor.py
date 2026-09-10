#!/usr/bin/env python3
"""Install/use/save/restart the real harbor lift, starting from a real delivered save.
Only ordinary keyboard controls and read-only state. The source save is copied
unchanged into an isolated root; no checkpoint payload is synthesized or edited.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time
from validate_native_cove_delivery import Controls
from validate_native_cove_saves import archive


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--source-slot',type=Path,required=True)
    parser.add_argument('--storage-root',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    args.storage_root=args.storage_root.resolve();args.storage_root.mkdir(mode=0o700,exist_ok=False)
    args.output=args.output.resolve();args.output.mkdir(exist_ok=False)
    source,payload=archive(args.source_slot);world=source['world']
    slot=args.storage_root/world;slot.mkdir(mode=0o700)
    for name in ('current','mirror'):
        shutil.copyfile(args.source_slot/name,slot/name);(slot/name).chmod(0o600)
    report={'status':'running','source':source,'binarySha256':hashlib.sha256(args.binary.read_bytes()).hexdigest(),
        'kind':'native actual keyboard harbor installation, lift, lower, release and save/restart; no screenshots','stages':[]}
    x=Controls();child=stream=window=None;index=0;began=time.monotonic()
    def read():
        path=args.output/f'observation-{index}'/'state.json'
        return json.loads(path.read_text()) if path.exists() else {}
    def wait(predicate,label,seconds=40):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            if time.monotonic()-began>300:raise RuntimeError('Journey exceeded five minutes')
            if child.poll() is not None:raise RuntimeError(f'Game exited {child.returncode}: {label}')
            state=read();assert not state.get('failed'),state
            if predicate(state):return state
            time.sleep(.02)
        raise RuntimeError(label+': '+json.dumps(read()))
    def record(name,state=None,**extra):
        state=state or read();assert state.get('terrainSurface')=='lego' and not state.get('failed')
        report['stages'].append({'name':name,'state':state,**extra})
        (args.output/'summary.json').write_text(json.dumps(report,indent=2)+'\n');return state
    def key(name):x.key(window,name)
    def hold(names,ticks):
        before=int(read()['player']['tick'])
        try:
            for name in names:x.state(window,name,True)
            wait(lambda s:int(s['player']['tick'])>=before+ticks,'held control steps',60)
        finally:
            for name in names:x.state(window,name,False)
        before=int(read()['player']['tick'])
        return wait(lambda s:int(s['player']['tick'])>before,'released control observed')
    def walk(target):
        for _ in range(100):
            s=read();p=s['player']['feet'];t=target(s) if callable(target) else target
            dx,dz=t[0]-p[0],t[1]-p[2];distance=math.hypot(dx,dz)
            if distance<.22:return hold([],6)
            yaw=s['camera']['yaw'];forward=(dx*math.sin(yaw)+dz*math.cos(yaw))/distance
            right=(dx*math.cos(yaw)-dz*math.sin(yaw))/distance;keys=[]
            if abs(forward)>.4:keys.append('w' if forward>0 else 's')
            if abs(right)>.4:keys.append('d' if right>0 else 'a')
            hold(keys,max(1,min(5,int(distance/.06)-1)))
        raise RuntimeError('Walking target unreachable: '+json.dumps(read()))
    def start():
        nonlocal child,stream,window,index
        index+=1;stream=(args.output/f'process-{index}.log').open('w')
        cmd=[str(args.binary.resolve()),'--config','salvage_cove.cfg','--uncapped','--width','960','--height','540',
            '--expedition-root',str(args.storage_root),'--expedition-world',world,
            '--expedition-observe',str(args.output/f'observation-{index}')]
        child=subprocess.Popen(cmd,stdout=stream,stderr=subprocess.STDOUT,env={**os.environ,'VOXY_WINDOW_BACKEND':'x11'})
        wait(lambda _:x.own_window(child.pid),'owned native window',60);window=x.own_window(child.pid)
        return wait(lambda s:s.get('ready') and s.get('restore',{}).get('phase')=='ready'
            and s['pause']['phase']=='paused','physical save restored',60)
    def stop():
        nonlocal child,stream
        if child and child.poll() is None:child.send_signal(signal.SIGTERM);child.wait(timeout=15)
        if stream:stream.close()
        child=stream=None
    def resume():
        key('p');wait(lambda s:s['pause']['phase']=='running','resumed')
    try:
        before=record('actual-delivery-save-restored',start());assert before['job']['durable']
        assert before['session']['inventory']['salvageMaterial']=='96' and not before['harbor']['installed']
        resume();key('r');wait(lambda s:int(s['rescue']['completed'])>int(before['rescue']['completed']) and not s['rescue']['pending'] and s['pause']['phase']=='paused','rescue saved at berth');resume()
        walk([4.5,-52.5]);wait(lambda s:s['harbor']['canInstall'],'dock installation available')
        record('at-dock-before-install');key('k')
        installed=wait(lambda s:s['harbor']['durable'] and not s['harbor']['pending'] and s['pause']['phase']=='paused','installation saved')
        meta,payload=archive(slot);assert int.from_bytes(payload[4:8],'little')==2
        assert meta['tick']==int(installed['pause']['tick']) and installed['session']['inventory']==before['session']['inventory']
        assert 6<installed['tow']['position'][0]<8 and installed['tow']['position'][2]>-49 and installed['tow']['position'][1]>=0 and installed['tow']['speed']==0
        parked=installed['tow']['position'];assert math.dist(parked,before['tow']['position'])>5
        (args.output/'installed.svce').write_bytes(payload);record('installed-and-mirrored',installed,archive=meta)
        resume();hold([],120);record('before-attach');key('f')
        wait(lambda s:s['harbor']['attached'] and s['harbor']['canOperate'],'four lift lines attached')
        rest=record('four-lines-attached');hold(['q'],600)
        raised=record('raised-and-stopped');assert raised['harbor']['brokenMask']==0 and raised['harbor']['motor']==0
        assert raised['boat']['position'][1]>rest['boat']['position'][1]+2, 'Boat did not physically rise'
        key('p');wait(lambda s:s['pause']['phase']=='paused','suspended pause joined')
        generation=archive(slot)[0]['generation'];key('F10')
        wait(lambda _:archive(slot)[0]['generation']>generation,'suspended archive published')
        saved=record('suspended-save');meta,payload=archive(slot);(args.output/'suspended.svce').write_bytes(payload)
        assert meta['tick']==int(saved['pause']['tick']);record('suspended-archive',archive=meta)
        stop();loaded=record('suspended-process-restarted',start())
        assert loaded['harbor']['attached'] and loaded['harbor']['brokenMask']==0 and loaded['harbor']['motor']==0
        assert int(loaded['pause']['tick'])==meta['tick']+1 and loaded['boat']['paidPartIds']==before['boat']['paidPartIds']
        assert loaded['session']['inventory']==before['session']['inventory']
        assert math.dist(loaded['tow']['position'],parked)<.001
        assert max(abs(a-b) for a,b in zip(loaded['harbor']['lengths'],saved['harbor']['lengths']))<.001
        assert math.dist(loaded['boat']['position'],saved['boat']['position'])<.1
        resume();hold(['z'],1320);lowered=record('lowered-after-restart')
        assert lowered['harbor']['brokenMask']==0 and lowered['boat']['position'][1]<raised['boat']['position'][1]-2
        key('f');wait(lambda s:not s['harbor']['attached'] and s['harbor']['stage']==4,'released four lines')
        record('released');walk([4.5,-53]);wait(lambda s:s['player']['interaction']=='board','boat alongside')
        key('e');wait(lambda s:s['player']['onBoat'],'board lowered boat')
        walk(lambda s:[s['boat']['helmPosition'][0],s['boat']['helmPosition'][2]])
        wait(lambda s:s['player']['interaction']=='helm','walk to helm');key('e')
        wait(lambda s:s['player']['mode']=='helm','use helm');hold(['w'],90)
        sailing=record('sailing-after-lift-and-restart');assert sailing['boat']['speed']>1
        assert sailing['session']['inventory']==before['session']['inventory']
        report['status']='passed'
    except Exception as error:
        report.update(status='failed',error=str(error));raise
    finally:
        stop();x.close();(args.output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
