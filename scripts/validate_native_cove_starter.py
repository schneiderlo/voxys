#!/usr/bin/env python3
"""Rebuild the starter, reuse paid stock, and restart through actual native controls.
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
        'kind':'native starter rebuild, paid stock reuse, exact ownership and process restarts; no screenshots','stages':[]}
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
        before=record('actual-paid-save-restored',start());paid=sorted(before['boat']['paidPartIds'],key=int)
        assert len(paid)==2 and before['session']['inventory']['salvageMaterial']=='0'
        def unchanged(s):
            assert s['session']['inventory']==before['session']['inventory']
            assert s['job']['phase']==before['job']['phase'] and s['job']['secured']==before['job']['secured']
            assert s['harbor']['installed']==before['harbor']['installed']
        def check(s,active,stored):
            unchanged(s)
            assert sorted(s['boat']['paidPartIds'])==sorted(active)
            assert sorted(s['workshop']['storedPartIds'])==sorted(stored)
            assert len(set(active+stored))==len(paid) and sorted(active+stored,key=int)==paid
        def durable(label,generation):
            saved=wait(lambda s:s['pause']['phase']=='paused' and not s['workshop']['savePending']
                and not s['workshop']['pending'] and archive(slot)[0]['generation']>generation,label,70)
            meta,payload=archive(slot);assert meta['tick']==int(saved['pause']['tick'])
            (args.output/(label+'.svce')).write_bytes(payload)
            return record(label,saved,archive=meta)
        resume();key('b');wait(lambda s:s['workshop']['open'] and s['workshop']['canRebuild'],'starter service at dock')
        generation=archive(slot)[0]['generation']
        if args.permission_failure:slot.chmod(0o500)
        key('h')
        if args.permission_failure:
            pending=wait(lambda s:s['workshop']['savePending'] and 'Save failed.' in (args.output/f'process-{index}.log').read_text(),'rebuild storage failure')
            record('rebuild-awaiting-storage',pending);tick=pending['pause']['tick'];key('p');key('h');key('w');time.sleep(.3)
            frozen=record('failed-rebuild-controls-frozen');assert frozen['pause']['tick']==tick and archive(slot)[0]['generation']==generation
            slot.chmod(0o700);key('F10')
        rebuilt=durable('starter-rebuilt-and-saved',generation);check(rebuilt,[],paid)
        assert rebuilt['boat']['parts']==11
        stop();loaded=record('rebuild-process-restart',start());check(loaded,[],paid)
        resume();key('b');wait(lambda s:s['workshop']['open'],'workshop after rebuild')
        wait(lambda s:s['workshop']['catalogName']=='Pontoon' and s['workshop']['partCost']=='0','stored pontoon offered for reuse')
        key('v');wait(lambda s:s['workshop']['changed'] and s['workshop']['valid'],'stored pontoon snapped')
        key('e');wait(lambda s:not s['workshop']['changed'] and s['workshop']['canLaunch'],'kept stored pontoon')
        assert read()['workshop']['charge']=='0';generation=archive(slot)[0]['generation'];key('Return')
        reused=durable('stored-pontoon-reused-and-saved',generation)
        assert len(reused['boat']['paidPartIds'])==1 and reused['boat']['parts']==12
        check(reused,[paid[0]],[paid[1]])
        stop();loaded=record('stock-withdrawal-process-restart',start());check(loaded,[paid[0]],[paid[1]])
        resume();key('b');wait(lambda s:s['workshop']['open'] and s['workshop']['canRebuild'],'repeat starter service')
        generation=archive(slot)[0]['generation'];key('h');rebuilt=durable('second-starter-rebuild-saved',generation);check(rebuilt,[],paid)
        stop();loaded=record('repeated-rebuild-process-restart',start());check(loaded,[],paid)
        assert archive(args.source_slot)[0]==source,'source save was not modified'
        report['status']='passed'
    except Exception as error:
        report.update(status='failed',error=str(error));raise
    finally:
        slot.chmod(0o700);stop();x.close();(args.output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
