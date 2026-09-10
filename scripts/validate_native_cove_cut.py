#!/usr/bin/env python3
"""Cut/save/restart and rebuild the actual boat, starting from a real gameplay save.
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
        'kind':'native live weld cut at sea, root save/restart, Rescue and repeated rebuild; no screenshots','stages':[]}
    x=Controls();child=stream=window=None;index=0;began=time.monotonic()
    def read():
        path=args.output/f'observation-{index}'/'state.json'
        return json.loads(path.read_text()) if path.exists() else {}
    def wait(predicate,label,seconds=40):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            if time.monotonic()-began>600:raise RuntimeError('Journey exceeded ten minutes')
            if child.poll() is not None:raise RuntimeError(f'Game exited {child.returncode}: {label}')
            state=read();assert not state.get('failed'),state
            if predicate(state):return state
            time.sleep(.02)
        raise RuntimeError(label+': '+json.dumps(read()))
    def record(name,state=None,**extra):
        state=state or read();assert state.get('terrainSurface')=='lego' and not state.get('failed')
        print(name,flush=True)
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
    def durable(label,generation):
        state=wait(lambda s:s['pause']['phase']=='paused' and not s['workshop']['savePending']
            and not s['rescue']['pending'] and archive(slot)[0]['generation']>generation,label,60)
        meta,payload=archive(slot);assert meta['schema']==4 and meta['tick']==int(state['pause']['tick'])
        assert meta['rootKeys']==[r['key'] for r in state['boat']['roots']]
        assert all(int(r['observedTick'])==meta['tick'] for r in state['boat']['roots'])
        (args.output/(label+'.svce')).write_bytes(payload)
        return record(label,state,archive=meta)
    try:
        before=record('actual-paid-save-restored',start());paid=before['boat']['paidPartIds'];assert len(paid)==2
        def owned(state,fitted=True):
            assert state['session']['inventory']==before['session']['inventory']
            assert sorted(state['boat']['paidPartIds'])==sorted(paid if fitted else [])
            assert sorted(state['workshop']['storedPartIds'])==sorted([] if fitted else paid)
            assert state['job']['phase']==before['job']['phase'] and state['job']['secured']==before['job']['secured']
            if fitted:assert state['boat']['parts']==before['boat']['parts'] and state['boat']['massKg']==before['boat']['massKg']
        resume();generation=archive(slot)[0]['generation'];key('r');durable('starting-rescue',generation)
        resume();walk([4.5,-49]);walk([4.5,-53]);wait(lambda s:s['player']['interaction']=='board','boat alongside')
        key('e');wait(lambda s:s['player']['onBoat'],'aboard')
        walk(lambda s:[s['boat']['helmPosition'][0],s['boat']['helmPosition'][2]])
        wait(lambda s:s['player']['interaction']=='helm','helm reachable');key('e');wait(lambda s:s['player']['mode']=='helm','using helm')
        if read()['job']['phase']=='available':
            key('j');wait(lambda s:s['job']['phase']=='accepted' and not s['job']['pending'],'accept cargo before cable refusal')
            before={**before,'job':{**before['job'],'phase':'accepted'}}
        key('f');wait(lambda s:s['tow']['attached'] and s['tow']['confirmed'],'live cable before cut refusal')
        hold(['c'],3);refused=record('active-cable-cut-refused');owned(refused)
        assert refused['cutter']['cutWelds']==0 and not refused['cutter']['pending'] and not refused['cutter']['canCut']
        assert refused['tow']['attached'] and refused['boat']['rootCount']==1 and refused['workshop']['recoveryDesigns']==0
        key('f');wait(lambda s:not s['tow']['attached'] and s['tow']['confirmed'],'release cable before cutting')
        home=read()['boat']['position'];hold(['w'],240)
        sailing=record('sailed-away-before-cut');assert math.dist(sailing['boat']['position'],home)>5
        cut_count=0
        for attempt in range(6):
            wait(lambda s:s['cutter']['canCut'],'nearby weld available')
            selected=record('selected-weld-'+str(attempt))['cutter']['weld'];assert selected!='0'
            generation=archive(slot)[0]['generation'];key('c')
            cut=durable('cut-saved-'+str(attempt),generation);owned(cut)
            assert cut['cutter']['cutWelds']==cut_count+1;cut_count+=1
            assert cut['workshop']['recoveryDesigns']==1 and not cut['workshop']['canRemoveRecovery']
            if cut['boat']['rootCount']>1:break
            resume()
        assert cut['boat']['rootCount']>1,'Actual cutting must create separate bodies'
        keys=[r['key'] for r in cut['boat']['roots']];designs=cut['workshop']['recoveryDigests'];rider=cut['player']['rootKey']
        stop();loaded=record('cut-process-restart',start());owned(loaded)
        assert [r['key'] for r in loaded['boat']['roots']]==keys and loaded['cutter']['cutWelds']==cut_count
        assert loaded['player']['rootKey']==rider and loaded['workshop']['recoveryDigests']==designs
        for saved,current in zip(cut['boat']['roots'],loaded['boat']['roots'],strict=True):assert math.dist(saved['position'],current['position'])<.25
        resume();generation=archive(slot)[0]['generation'];key('r');rescued=durable('all-cut-sections-rescued',generation);owned(rescued)
        assert [r['key'] for r in rescued['boat']['roots']]==keys and rescued['cutter']['cutWelds']==cut_count
        assert not rescued['player']['onBoat'] and not rescued['tow']['attached']
        stop();loaded=record('cut-rescue-process-restart',start());owned(loaded)
        assert loaded['workshop']['recoveryDigests']==designs and loaded['boat']['rootCount']==len(keys)
        resume();key('b');wait(lambda s:s['workshop']['open'] and s['workshop']['canRebuild'],'broken workshop opens')
        generation=archive(slot)[0]['generation'];key('h');rebuilt=durable('broken-starter-rebuilt',generation);owned(rebuilt,False)
        assert rebuilt['boat']['rootCount']==1 and rebuilt['cutter']['cutWelds']==0 and rebuilt['boat']['parts']==11
        assert rebuilt['workshop']['recoveryDigests']==designs
        resume();key('b');wait(lambda s:s['workshop']['open'] and s['workshop']['canLoadRecovery'],'protected design offered')
        key('k');wait(lambda s:s['workshop']['canLaunch'],'protected design loaded');assert read()['workshop']['charge']=='0'
        generation=archive(slot)[0]['generation'];key('Return');refit=durable('paid-design-restored-from-stock',generation);owned(refit)
        assert refit['boat']['rootCount']==1 and refit['cutter']['cutWelds']==0
        resume();key('b');wait(lambda s:s['workshop']['open'] and s['workshop']['canRebuild'],'repeat rebuild offered')
        generation=archive(slot)[0]['generation'];key('h');rebuilt=durable('second-rebuild-no-paid-duplication',generation);owned(rebuilt,False)
        stop();loaded=record('repeated-rebuild-process-restart',start());owned(loaded,False)
        assert loaded['workshop']['recoveryDigests']==designs
        assert archive(args.source_slot)[0]==source,'source save was not modified'
        report['status']='passed'
    except Exception as error:
        report.update(status='failed',error=str(error));raise
    finally:
        slot.chmod(0o700);stop();x.close();(args.output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
