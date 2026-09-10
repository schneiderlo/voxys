#!/usr/bin/env python3
"""Rescue/save/restart the actual boat, starting from a real gameplay save.
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
    parser.add_argument('--permission-failure',action='store_true')
    parser.add_argument('--hook-before-second-rescue',action='store_true')
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
        'kind':'native actual rescue, same ownership/cargo, repeated save and process restart; no screenshots','stages':[]}
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
        physical=read()['player']['mode']=='helm' and bool(names)
        def tick(s):return int(s['boat']['physicsTicks']['completed'] if physical else s['player']['tick'])
        before=tick(read())
        try:
            for name in names:x.state(window,name,True)
            wait(lambda s:tick(s)>=before+ticks,'held control steps',60)
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
        before=record('actual-save-restored',start())
        def unchanged(s):
            assert s['session']['inventory']==before['session']['inventory']
            assert s['boat']['paidPartIds']==before['boat']['paidPartIds']
            assert s['boat']['parts']==before['boat']['parts'] and s['boat']['massKg']==before['boat']['massKg']
            assert [r['key'] for r in s['boat']['roots']]==[r['key'] for r in before['boat']['roots']]
            assert s['boat']['controlPart']==before['boat']['controlPart']
            assert s['job']['phase']==before['job']['phase'] and s['job']['secured']==before['job']['secured']
            assert s['harbor']['installed']==before['harbor']['installed']
            if s['job']['secured']:assert math.dist(s['tow']['position'],before['tow']['position'])<.001
        for cycle in range(2):
            resume()
            if cycle==1 and args.hook_before_second_rescue:
                assert not before['job']['secured'];key('j')
                wait(lambda s:s['job']['phase']=='accepted' and not s['job']['pending'],'accept generator job')
                walk([4.5,-49]);walk([4.5,-53]);wait(lambda s:s['player']['interaction']=='board','board before salvage')
                key('e');wait(lambda s:s['player']['onBoat'],'aboard before hook')
                walk(lambda s:[s['boat']['helmPosition'][0],s['boat']['helmPosition'][2]])
                wait(lambda s:s['player']['interaction']=='helm','helm before salvage');key('e')
                wait(lambda s:s['player']['mode']=='helm','use helm before hook');key('f')
                wait(lambda s:s['tow']['attached'] and s['tow']['confirmed'],'live cargo cable attached')
                before={**before,'job':{**before['job'],'phase':'accepted'}}
                record('real-cargo-attached-before-rescue');hold(['q'],10)
                record('live-winch-before-rescue')
            generation=archive(slot)[0]['generation'];completed=int(read()['rescue']['completed'])
            if args.permission_failure and cycle==0:slot.chmod(0o500)
            key('r')
            if args.permission_failure and cycle==0:
                wait(lambda s:s['rescue']['savePending'] and 'Save failed.' in (args.output/f'process-{index}.log').read_text(),'real storage failure')
                pending=record('rescue-storage-failure');assert archive(slot)[0]['generation']==generation
                tick=pending['pause']['tick'];key('p');key('r');key('w');time.sleep(.3)
                frozen=record('failed-rescue-controls-frozen');assert frozen['pause']['tick']==tick and frozen['rescue']['pending']
                slot.chmod(0o700);key('F10')
            recovered=wait(lambda s:s['rescue']['phase']=='idle' and int(s['rescue']['completed'])>completed and s['pause']['phase']=='paused','rescue durably saved')
            unchanged(recovered);assert not recovered['player']['onBoat'] and not recovered['tow']['attached'] and not recovered['harbor']['attached']
            meta,payload=archive(slot);assert meta['generation']>generation and meta['tick']==int(recovered['pause']['tick'])
            assert meta['schema']==4 and meta['rootKeys']==[r['key'] for r in recovered['boat']['roots']]
            assert all(int(r['observedTick'])==meta['tick'] for r in recovered['boat']['roots'])
            (args.output/f'rescue-{cycle}.svce').write_bytes(payload)
            record('rescue-saved-'+str(cycle),recovered,archive=meta)
            stop();loaded=record('actual-process-restart-'+str(cycle),start());unchanged(loaded)
            assert int(loaded['pause']['tick'])==meta['tick']+1
            assert math.dist(loaded['boat']['position'],recovered['boat']['position'])<.1
            for saved_root,loaded_root in zip(recovered['boat']['roots'],loaded['boat']['roots'],strict=True):
                assert int(loaded_root['observedTick'])==meta['tick']+1
                assert math.dist(loaded_root['position'],saved_root['position'])<.1
            assert math.dist(loaded['tow']['position'],recovered['tow']['position'])<.1
            assert not loaded['tow']['attached'] and not loaded['harbor']['attached']
        resume();walk([5.5,-51]);walk([4.5,-53]);wait(lambda s:s['player']['interaction']=='board','rescued boat alongside')
        key('e');wait(lambda s:s['player']['onBoat'],'board rescued boat')
        walk(lambda s:[s['boat']['helmPosition'][0],s['boat']['helmPosition'][2]])
        wait(lambda s:s['player']['interaction']=='helm','walk to helm');key('e')
        wait(lambda s:s['player']['mode']=='helm','use helm');hold(['w'],90)
        sailing=record('sailing-after-repeated-rescue');unchanged(sailing);assert sailing['boat']['speed']>1
        assert archive(args.source_slot)[0]==source,'source save was not modified'
        report['status']='passed'
    except Exception as error:
        report.update(status='failed',error=str(error));raise
    finally:
        slot.chmod(0o700);stop();x.close();(args.output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
