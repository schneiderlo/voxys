#!/usr/bin/env python3
"""Owned X11 pointer/keyboard brick construction and native save/restart. No images."""
import argparse
import ctypes as C
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
from validate_native_cove_saves import Event, KeyEvent, archive


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--storage-root',type=Path,required=True)
    parser.add_argument('--source-slot',type=Path)
    parser.add_argument('--resume-report',type=Path)
    parser.add_argument('--expected-presentation-parts',type=int)
    parser.add_argument('--continuous',action='store_true',help='Place eight bricks with only three palette choices; discard the unused ghost on Launch')
    parser.add_argument('--expected-hud',action='store_true',help='Require the native Cove overlay to encode readable text during the journey')
    args=parser.parse_args();args.output=args.output.resolve();args.output.mkdir(exist_ok=False)
    args.storage_root=args.storage_root.resolve();args.storage_root.mkdir(mode=0o700,exist_ok=False)
    report={'status':'running','kind':'Native pointer brick building and save/restart; no images',
        'binarySha256':hashlib.sha256(args.binary.read_bytes()).hexdigest(),'stages':[]}
    controls=Controls();child=stream=window=None;index=0;started=time.monotonic()
    def read():
        path=args.output/f'observation-{index}'/'state.json'
        try:return json.loads(path.read_text())
        except (FileNotFoundError,json.JSONDecodeError):return {}
    def wait(predicate,label,seconds=45):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            if time.monotonic()-started>600:raise RuntimeError('Journey exceeded ten minutes')
            if child and child.poll() is not None:raise RuntimeError('Game exited: '+label)
            state=read();assert not state.get('failed'),state
            if predicate(state):return state
            time.sleep(.025)
        raise RuntimeError(label+': '+json.dumps(read()))
    def record(name,**extra):
        state=read()
        if args.expected_hud:
            hud=state.get('nativeHud',{})
            assert hud.get('enabled') and hud.get('lastEncodedQuads',0)>0,hud
            assert hud['bodyPixels']>=16 and not hud['truncated'],hud
            x,y,width,height=hud['panel']
            assert 0<=x and 0<=y and x+width<=960 and y+height<=540,hud
        report['stages'].append({'name':name,'state':state,**extra})
        print(name,flush=True);(args.output/'summary.json').write_text(json.dumps(report,indent=2)+'\n');return state
    def key(name):controls.key(window,name)
    def frames(before):
        return wait(lambda s:int(s.get('assetFixture',{}).get('submittedSerial',0))>=before+2
            and not s['workshop']['camera']['framePending'],'pointer and camera frames')
    def mouse(point,click=False):
        x,y=map(round,point)
        events=[(6,64,0,0)]+([(4,4,1,0),(5,8,1,256)] if click else [])
        for kind,mask,button,state in events:
            event=Event();event.key=KeyEvent(kind,0,True,controls.display,window,controls.root,0,
                max(1,int(time.monotonic()*1000)&0xffffffff),x,y,x,y,state,button,True)
            assert controls.x.XSendEvent(controls.display,window,False,mask,C.byref(event))
            controls.x.XFlush(controls.display);time.sleep(.08)
    def project(point):
        s=read();p=[v+s['workshop']['displayOrigin'][i]-256*s['camera']['sector'][i] for i,v in enumerate(point)]+[1]
        matrix=s['camera']['viewProjection'];clip=[sum(p[c]*matrix[c*4+r] for c in range(4)) for r in range(4)]
        assert clip[3]>0
        x=(clip[0]/clip[3]+1)*480;y=(1-clip[1]/clip[3])*270
        assert 0<x<960 and 0<y<540,(x,y)
        assert not (.78<y/540<.94 and .24<x/960<.76),'world point covered by native palette'
        return x,y
    def stop():
        nonlocal child,stream
        if child and child.poll() is None:child.send_signal(signal.SIGTERM);child.wait(timeout=15)
        if stream:stream.close()
        child=stream=None
    def start(world=None):
        nonlocal child,stream,window,index
        index+=1;stream=(args.output/f'process-{index}.log').open('w')
        command=[str(args.binary.resolve()),'--config','salvage_cove.cfg','--uncapped','--width','960','--height','540',
            '--expedition-root',str(args.storage_root),'--expedition-observe',str(args.output/f'observation-{index}')]
        if world:command+=['--expedition-world',world]
        child=subprocess.Popen(command,stdout=stream,stderr=subprocess.STDOUT,env={**os.environ,'VOXY_WINDOW_BACKEND':'x11'})
        wait(lambda _:controls.own_window(child.pid),'owned native window',60);window=controls.own_window(child.pid)
        wait(lambda s:s.get('ready') and s.get('boat',{}).get('active'),'native ready',60)
        if world:wait(lambda s:s.get('restore',{}).get('phase')=='ready' and s['pause']['phase']=='paused','saved world ready')
    def hold(names,ticks):
        physical=read()['player']['mode']=='helm';clock=lambda s:int(s['boat']['physicsTicks']['completed'] if physical else s['player']['tick'])
        first=clock(read())
        try:
            for name in names:controls.state(window,name,True)
            wait(lambda s:clock(s)>=first+ticks,'held movement observed')
        finally:
            for name in names:controls.state(window,name,False)
        time.sleep(.12)
    def walk(target):
        for _ in range(100):
            s=read();p=s['player']['feet'];t=target(s) if callable(target) else target
            dx,dz=t[0]-p[0],t[1]-p[2];distance=math.hypot(dx,dz)
            # Actual boarding/helm reach is checked separately. Avoid steering
            # oscillation when a displayed frame consumes multiple key steps.
            if distance<.25:return
            yaw=s['camera']['yaw'];forward=(dx*math.sin(yaw)+dz*math.cos(yaw))/distance
            right=(dx*math.cos(yaw)-dz*math.sin(yaw))/distance;names=[]
            if abs(forward)>.4:names.append('w' if forward>0 else 's')
            if abs(right)>.4:names.append('d' if right>0 else 'a')
            hold(names,max(1,min(5,int(distance/.06)-1)))
        raise RuntimeError('Could not walk to target: '+str(target))
    def geometry():
        s=read()['workshop'];return (s['name'],tuple(s['placement']),s['rotation'])
    try:
        if args.source_slot and not args.resume_report:
            source,_=archive(args.source_slot);slot=args.storage_root/source['world'];slot.mkdir(mode=0o700)
            for name in ('current','mirror'):shutil.copyfile(args.source_slot/name,slot/name);(slot/name).chmod(0o600)
            start(source['world']);old=record('pre-brick-save-resumed');assert old['boat']['parts']==13
            assert old['boat']['paidPartIds']==['35','100'];assert old['session']['inventory']['salvageMaterial']=='0';stop()
        if args.resume_report:
            prior=json.loads(args.resume_report.read_text());report['continues']=str(args.resume_report)
            saved=next(s['state'] for s in prior['stages'] if s['name']=='saved-eight-bricks')
            placed=[s.get('placedBrick',s['state']['workshop']) for s in prior['stages'] if s['name'].startswith('placed-brick-')]
            geometries=[(s['name'],tuple(s['placement']),s['rotation']) for s in placed]
            source,_=archive(args.source_slot);assert source['world']==saved['world']
            slot=args.storage_root/saved['world'];slot.mkdir(mode=0o700)
            for name in ('current','mirror'):shutil.copyfile(args.source_slot/name,slot/name);(slot/name).chmod(0o600)
            start(saved['world'])
        else:
            start();key('b');wait(lambda s:s['workshop']['open'],'workshop open')
            key('Tab');wait(lambda s:s['workshop']['name']=='Cargo cradle','select cargo cradle');key('Delete')
            wait(lambda s:s['workshop']['changed'] and s['workshop']['parts']==10,'cradle removal observed');key('e')
            wait(lambda s:s['workshop']['parts']==10 and not s['workshop']['changed'],'clear cargo deck')
            previous=None;geometries=[];cost=0;palette_picks=0
            thumbs=[2,2,2,1,1,0,0,0] if args.continuous else [2,1,0,2,1,0,1,0]
            for i,thumb in enumerate(thumbs):
                if not args.continuous or i==0 or thumb!=thumbs[i-1]:
                    mouse(((.32+.18*thumb)*960,.86*540),True);palette_picks+=1
                expected=('Brick 1 x 2','Brick 2 x 2','Brick 2 x 4')[thumb]
                wait(lambda s:s['workshop']['pointerPlacement'] and s['workshop']['changed']
                    and s['workshop']['catalogName']==expected,'palette selects '+expected)
                cost+=int(read()['workshop']['partCost'])
                if i==2:
                    rotation=read()['workshop']['rotation'];key('r');wait(lambda s:s['workshop']['rotation']!=rotation,'rotation observed')
                if previous:
                    before=int(read()['assetFixture']['submittedSerial']);key('m');frames(before)
                point=[v*.02+(.66 if axis==1 else 0) for axis,v in enumerate(previous)] if previous else [2,.96,-55]
                target=None;attempts=[]
                for dx,dz in ((0,0),(.5,0),(-.5,0),(0,.5),(0,-.5),(.5,.5),(-.5,.5),(.5,-.5),(-.5,-.5)):
                    candidate=[point[0]+dx,point[1],point[2]+dz]
                    pointer=project(candidate);before=int(read()['assetFixture']['submittedSerial']);mouse(pointer);frames(before)
                    w=read()['workshop'];attempts.append({'point':candidate,'valid':w['valid'],'pointerTarget':w['pointerTarget'],'placement':w['placement']})
                    if w['pointerTarget'] and w['valid'] and (not previous or w['placement'][1]==previous[1]+48):target=pointer;break
                report.setdefault('pointerTargets',[]).append(attempts)
                assert target,'Visible supporting stud required: '+json.dumps(attempts)
                ghost=read()['workshop'];mouse(target,True)
                if args.continuous:
                    wait(lambda s:s['workshop']['brickTool'] and s['workshop']['placedBricks']==i+1
                        and s['workshop']['placedParts']==11+i,'pointer keeps brick and creates next preview')
                else:
                    wait(lambda s:s['workshop']['placedBricks']==i+1 if s['workshop'].get('brickTool') else not s['workshop']['changed'],'pointer keeps brick')
                    if read()['workshop'].get('brickTool'):key('Escape')
                    wait(lambda s:not s['workshop']['changed'] and s['workshop']['parts']==11+i,'selection after placement')
                previous=ghost['placement'];geometries.append((ghost['name'],tuple(previous),ghost['rotation']))
                record('placed-brick-'+str(i+1),placedBrick={k:ghost[k] for k in ('name','placement','rotation')})
            if args.continuous:
                assert palette_picks==3
                key('Escape');wait(lambda s:s['workshop']['open'] and not s['workshop']['brickTool'] and not s['workshop']['changed']
                    and s['workshop']['parts']==18,'Esc stops without removing placed bricks')
                record('continuous-tool-stopped',palettePicks=palette_picks)
            key('z');wait(lambda s:s['workshop']['changed'] and not s['workshop']['valid'],'refused overlap');key('BackSpace')
            wait(lambda s:not s['workshop']['changed'],'cancel overlap');key('Delete')
            wait(lambda s:s['workshop']['changed'] and s['workshop']['parts']==17,'removal observed');key('e')
            wait(lambda s:not s['workshop']['changed'],'removal kept');key('u')
            wait(lambda s:s['workshop']['parts']==18 and not s['workshop']['changed'],'remove and undo');record('remove-undone')
            if args.continuous:
                key('3');wait(lambda s:s['workshop']['brickTool'] and s['workshop']['parts']==19,'unused preview before launch')
                record('unused-preview-before-launch')
            key('Return');wait(lambda s:not s['workshop']['open'] and not s['workshop']['pending'] and s['boat']['parts']==18,'physical launch')
            launched=record('launched-eight-bricks');assert len(launched['boat']['paidPartIds'])==8
            assert launched['session']['inventory']['salvageMaterial']==str(48-cost)
            key('p');wait(lambda s:s['pause']['phase']=='paused','pause to save');key('F10')
            wait(lambda _:'Expedition saved' in controls.title(window),'disk save acknowledgment')
            saved=record('saved-eight-bricks');archive(args.storage_root/saved['world']);stop();start(saved['world'])
        restored=record('restored-eight-bricks');assert restored['boat']['paidPartIds']==saved['boat']['paidPartIds']
        if args.expected_presentation_parts is not None:
            assert restored['assetFixture']['presentationParts']==args.expected_presentation_parts
        assert restored['session']['inventory']==saved['session']['inventory'];assert restored['boat']['massKg']==saved['boat']['massKg']
        key('p');wait(lambda s:s['pause']['phase']=='running','resume');key('b');wait(lambda s:s['workshop']['open'],'restored workshop')
        seen=[]
        for _ in range(18):
            if read()['workshop']['name'].startswith('Brick '):seen.append(geometry())
            previous=read()['workshop']['selected'];key('Tab')
            wait(lambda s:s['workshop']['selected']!=previous,'selection observed')
        assert sorted(seen)==sorted(geometries),(seen,geometries);record('restored-exact-brick-layout')
        key('b');wait(lambda s:not s['workshop']['open'],'close restored workshop')
        walk([4.5,-49]);walk([4.5,-53]);wait(lambda s:s['player']['interaction']=='board','boarding reach')
        key('e');wait(lambda s:s['player']['onBoat'],'board brick boat')
        walk(lambda s:[s['boat']['helmPosition'][0],s['boat']['helmPosition'][2]])
        wait(lambda s:s['player']['interaction']=='helm','helm reach');key('e');wait(lambda s:s['player']['mode']=='helm','helm active')
        before=read()['boat']['position'];hold(['w'],90);sailed=record('sailed-brick-built-boat')
        assert math.dist(before,sailed['boat']['position'])>.3;assert sailed['boat']['paidPartIds']==saved['boat']['paidPartIds']
        report['status']='passed'
    except Exception as error:report['status']='failed';report['error']=str(error);raise
    finally:
        stop();controls.close();(args.output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
