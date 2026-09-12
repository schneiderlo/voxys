#!/usr/bin/env python3
"""Owned native workshop/resize/pause controls; numeric observations, no images."""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time
from validate_native_cove_delivery import Controls


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();root=args.output.resolve();root.mkdir(exist_ok=False)
    report={'status':'running','kind':'Native scene shadows through workshop, resize and pause; no images',
            'binarySha256':hashlib.sha256(args.binary.read_bytes()).hexdigest(),'stages':[]}
    controls=Controls();child=None;window=None
    def read():
        try:return json.loads((root/'observation/state.json').read_text())
        except (FileNotFoundError,json.JSONDecodeError):return {}
    def wait(predicate,label):
        end=time.monotonic()+90
        while time.monotonic()<end:
            state=read();assert not state.get('failed'),state
            if predicate(state):return state
            if child.poll() is not None:raise RuntimeError('Game exited: '+label)
            time.sleep(.05)
        raise RuntimeError(label+': '+json.dumps(read()))
    def record(name,predicate=lambda _:True):
        state=wait(lambda s:s.get('ready') and s.get('assetFixture',{}).get('sceneSunShadows') and predicate(s),name)
        assert state['terrainSurface']=='lego'
        assert state['assetFixture']['presentationParts']==3
        assert state['boat']['parts']==11
        assert state['session']['inventory']['salvageMaterial']=='48'
        report['stages'].append({'name':name,'state':state});print(name,flush=True)
        (root/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
        return state
    with (root/'process.log').open('w') as stream:
        try:
            child=subprocess.Popen([str(args.binary.resolve()),'--config','salvage_cove.cfg','--uncapped',
                '--width','960','--height','540','--expedition-root',str(root/'saves'),
                '--expedition-observe',str(root/'observation')],stdout=stream,stderr=subprocess.STDOUT,
                env={**os.environ,'VOXY_WINDOW_BACKEND':'x11'})
            wait(lambda _:controls.own_window(child.pid),'owned window');window=controls.own_window(child.pid)
            record('dock-shadows-active');controls.key(window,'b')
            initial=record('workshop-shadows-active',lambda s:s['workshop']['open'])
            before=initial['assetFixture']['opaqueSceneGpuBytes']
            controls.x.XResizeWindow.argtypes=[C.c_void_p,C.c_ulong,C.c_uint,C.c_uint]
            controls.x.XResizeWindow.restype=C.c_int
            controls.x.XResizeWindow(controls.display,window,1152,720);controls.x.XFlush(controls.display)
            record('resized-shadow-targets',lambda s:s['assetFixture']['opaqueSceneGpuBytes']!=before)
            controls.key(window,'b');record('returned-to-dock',lambda s:not s['workshop']['open'])
            wait(lambda s:s['pause']['canPause'],'pause available');controls.key(window,'p')
            record('paused-shadows-active',lambda s:s['pause']['phase']=='paused')
            report['status']='passed'
        except Exception as error:
            report['status']='failed';report['error']=str(error);report['failureState']=read();raise
        finally:
            if child and child.poll() is None:child.send_signal(signal.SIGTERM);child.wait(timeout=15)
            controls.close();stream.flush()
            errors=[line.strip() for line in (root/'process.log').read_text().splitlines() if '[ERROR]' in line]
            if errors:report['status']='failed';report['engineErrors']=errors
            (root/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
            assert report['status']=='passed',report.get('engineErrors',report.get('error'))


if __name__=='__main__':main()
