#!/usr/bin/env python3
"""Actual X11 camera gestures on an isolated copy of a saved boat. No images."""
import argparse
import ctypes as C
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
    parser.add_argument('--source-slot',type=Path,required=True)
    args=parser.parse_args();root=args.output.resolve();root.mkdir(exist_ok=False)
    original,_=archive(args.source_slot);storage=root/'saves';slot=storage/original['world'];slot.mkdir(parents=True,mode=0o700)
    for name in ('current','mirror'):shutil.copyfile(args.source_slot/name,slot/name);(slot/name).chmod(0o600)
    report={'status':'running','kind':'Actual native workshop camera controls; no images','stages':[]}
    controls=Controls();window=None;child=None
    def read():
        try:return json.loads((root/'observation/state.json').read_text())
        except (FileNotFoundError,json.JSONDecodeError):return {}
    def wait(predicate,label):
        end=time.monotonic()+45
        while time.monotonic()<end:
            s=read();assert not s.get('failed'),s
            if predicate(s):return s
            if child.poll() is not None:raise RuntimeError('Game exited')
            time.sleep(.04)
        raise RuntimeError(label+': '+json.dumps(read()))
    def key(name):controls.key(window,name)
    def mouse(kind,x,y,button=0,state=0):
        event=Event();event.key=KeyEvent(kind,0,True,controls.display,window,controls.root,0,
            max(1,int(time.monotonic()*1000)&0xffffffff),x,y,x,y,state,button,True)
        assert controls.x.XSendEvent(controls.display,window,False,{4:4,5:8,6:64}[kind],C.byref(event))
        controls.x.XFlush(controls.display);time.sleep(.15)
    def record(name):
        report['stages'].append({'name':name,'state':read()});print(name,flush=True)
        (root/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
    with (root/'process.log').open('w') as stream:
        try:
            child=subprocess.Popen([str(args.binary.resolve()),'--config','salvage_cove.cfg','--uncapped','--width','960','--height','540',
                '--expedition-world',original['world'],'--expedition-root',str(storage),'--expedition-observe',str(root/'observation')],
                stdout=stream,stderr=subprocess.STDOUT,env={**os.environ,'VOXY_WINDOW_BACKEND':'x11'})
            wait(lambda _:controls.own_window(child.pid),'owned window');window=controls.own_window(child.pid)
            wait(lambda s:s.get('ready') and s.get('pause',{}).get('phase')=='paused','restored');key('p')
            wait(lambda s:s['pause']['phase']=='running','resume');key('b');wait(lambda s:s['workshop']['open'],'workshop');time.sleep(.3)
            initial=read();record('whole-boat-framed');distance=initial['workshop']['camera']['distance']
            key('g');wait(lambda s:abs(s['workshop']['camera']['distance']-distance)>.1,'focus');record('selected-part-focused')
            key('m');wait(lambda s:abs(s['workshop']['camera']['distance']-distance)<.001,'whole boat')
            mouse(6,220,170);before=read()['workshop']['camera'];mouse(4,220,170,3)
            mouse(6,290,205,state=1024);wait(lambda s:abs(s['workshop']['camera']['yaw']-before['yaw'])>.1,'orbit');mouse(5,290,205,3,1024)
            record('orbit');target=read()['workshop']['camera']['target'];controls.state(window,'Shift_L',True)
            mouse(4,290,205,3);mouse(6,335,180,state=1025)
            wait(lambda s:math.dist(s['workshop']['camera']['target'],target)>.1,'shift pan');mouse(5,335,180,3,1025);controls.state(window,'Shift_L',False)
            record('shift-pan');target=read()['workshop']['camera']['target'];mouse(4,335,180,2);mouse(6,315,205,state=512)
            wait(lambda s:math.dist(s['workshop']['camera']['target'],target)>.1,'middle pan');mouse(5,315,205,2,512)
            distance=read()['workshop']['camera']['distance'];mouse(4,315,205,4);mouse(5,315,205,4,2048)
            wait(lambda s:s['workshop']['camera']['distance']<distance,'wheel zoom');record('middle-pan-and-wheel')
            key('3');wait(lambda s:s['workshop']['changed'],'brick ghost');mouse(6,220,170);time.sleep(.3)
            ghost=read()['workshop'];mouse(4,220,170,3);mouse(6,260,190,state=1024);mouse(4,260,190,1,1024)
            time.sleep(.3);assert read()['workshop']['changed'];assert read()['workshop']['placement']==ghost['placement']
            mouse(5,260,190,1,1280);mouse(5,260,190,3,1024);key('BackSpace');wait(lambda s:not s['workshop']['changed'],'discard ghost')
            record('camera-does-not-place-ghost');key('m');key('b');wait(lambda s:not s['workshop']['open'],'return to dock')
            final=read();assert final['session']['inventory']==initial['session']['inventory']
            assert final['boat']['paidPartIds']==initial['boat']['paidPartIds'];assert final['boat']['parts']==initial['boat']['parts']
            record('unchanged-owned-boat');report['status']='passed'
        except Exception as error:
            report['status']='failed';report['error']=str(error);report['failureState']=read();raise
        finally:
            if child and child.poll() is None:child.send_signal(signal.SIGTERM);child.wait(timeout=15)
            controls.close();(root/'summary.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
