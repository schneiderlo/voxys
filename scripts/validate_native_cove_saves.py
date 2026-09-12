#!/usr/bin/env python3
"""Real native X11 key controls, disk saves and process restarts. No screenshots.
Runs only its own child windows; never changes game state through debug setters.
Requires the native runtime environment (normally run inside nix-shell).
"""
import argparse
import ctypes as C
import hashlib
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import struct
import time

class KeyEvent(C.Structure):
    _fields_=[('type',C.c_int),('serial',C.c_ulong),('send_event',C.c_int),('display',C.c_void_p),
        ('window',C.c_ulong),('root',C.c_ulong),('subwindow',C.c_ulong),('time',C.c_ulong),
        ('x',C.c_int),('y',C.c_int),('x_root',C.c_int),('y_root',C.c_int),('state',C.c_uint),
        ('keycode',C.c_uint),('same_screen',C.c_int)]
class Event(C.Union):
    _fields_=[('key',KeyEvent),('padding',C.c_long*24)]

class X11:
    def __init__(self):
        self.x=C.CDLL('libX11.so.6')
        declarations={
            'XOpenDisplay':([C.c_char_p],C.c_void_p),
            'XDefaultRootWindow':([C.c_void_p],C.c_ulong),
            'XInternAtom':([C.c_void_p,C.c_char_p,C.c_int],C.c_ulong),
            'XGetWindowProperty':([C.c_void_p,C.c_ulong,C.c_ulong,C.c_long,C.c_long,C.c_int,C.c_ulong,
                C.POINTER(C.c_ulong),C.POINTER(C.c_int),C.POINTER(C.c_ulong),C.POINTER(C.c_ulong),C.POINTER(C.c_void_p)],C.c_int),
            'XQueryTree':([C.c_void_p,C.c_ulong,C.POINTER(C.c_ulong),C.POINTER(C.c_ulong),C.POINTER(C.POINTER(C.c_ulong)),C.POINTER(C.c_uint)],C.c_int),
            'XFetchName':([C.c_void_p,C.c_ulong,C.POINTER(C.c_void_p)],C.c_int),
            'XStringToKeysym':([C.c_char_p],C.c_ulong),
            'XKeysymToKeycode':([C.c_void_p,C.c_ulong],C.c_uint),
            'XSendEvent':([C.c_void_p,C.c_ulong,C.c_int,C.c_long,C.POINTER(Event)],C.c_int),
            'XFlush':([C.c_void_p],C.c_int),'XFree':([C.c_void_p],C.c_int),'XCloseDisplay':([C.c_void_p],C.c_int)}
        for name,(args,result) in declarations.items():
            fn=getattr(self.x,name);fn.argtypes=args;fn.restype=result
        self.display=self.x.XOpenDisplay(None)
        if not self.display:raise RuntimeError('No X11 display available')
        self.root=self.x.XDefaultRootWindow(self.display)
        self.pid_atom=self.x.XInternAtom(self.display,b'_NET_WM_PID',False)
        # Windows may close between a tree query and property read.
        self.error_handler=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_void_p)(lambda *_:0)
        self.x.XSetErrorHandler(self.error_handler)
    def children(self,window):
        root,parent=C.c_ulong(),C.c_ulong();children=C.POINTER(C.c_ulong)();count=C.c_uint()
        if not self.x.XQueryTree(self.display,window,C.byref(root),C.byref(parent),C.byref(children),C.byref(count)):return []
        values=list(children[:count.value]);self.x.XFree(children);return values
    def pid(self,window):
        kind=C.c_ulong();fmt=C.c_int();count,extra=C.c_ulong(),C.c_ulong();data=C.c_void_p()
        self.x.XGetWindowProperty(self.display,window,self.pid_atom,0,1,False,0,C.byref(kind),C.byref(fmt),C.byref(count),C.byref(extra),C.byref(data))
        value=C.cast(data,C.POINTER(C.c_ulong))[0] if data and count.value and fmt.value==32 else None
        if data:self.x.XFree(data)
        return value
    def own_window(self,pid):
        pending=[(self.root,0)]
        for _ in range(4096):
            if not pending:return None
            window,depth=pending.pop()
            if self.pid(window)==pid:return window
            if depth<4:pending.extend((child,depth+1) for child in self.children(window))
        raise RuntimeError('Window search exceeded bound')
    def title(self,window):
        name=C.c_void_p();self.x.XFetchName(self.display,window,C.byref(name))
        text=C.string_at(name).decode(errors='replace') if name else ''
        if name:self.x.XFree(name)
        return text
    def key(self,window,name):
        code=self.x.XKeysymToKeycode(self.display,self.x.XStringToKeysym(name.encode()))
        if not code:raise RuntimeError('Missing key '+name)
        for event_type,mask in [(2,1),(3,2)]:
            # GLFW uses nonzero per-key timestamps to suppress duplicate XIM
            # forwards. CurrentTime (zero) bypasses that deduplication and can
            # turn one workshop toggle into two when an input method is active.
            stamp=max(1,int(time.monotonic()*1000)&0xffffffff)
            event=Event();event.key=KeyEvent(event_type,0,True,self.display,window,self.root,0,stamp,1,1,1,1,0,code,True)
            if not self.x.XSendEvent(self.display,window,False,mask,C.byref(event)):raise RuntimeError('Key dispatch failed')
            self.x.XFlush(self.display);time.sleep(.08)
    def close(self):self.x.XCloseDisplay(self.display)

def archive_payload(payload, expected_world=None):
    """Read the frozen v1-v4 prefix and optional v5/v6 tails, without activation.

    v6 always includes the additional-cargo count, even when empty, then a
    47-byte character record. Parsing that field does not validate a two-job
    gameplay journey; individual single-cargo drivers still require count zero.
    """
    assert len(payload)>=8+419-8+8+32 and payload[:4]==b'SVCE'
    assert hashlib.sha256(payload[:-32]).digest()==payload[-32:]
    schema=int.from_bytes(payload[4:8],'little');assert 1<=schema<=6
    world=payload[40:56].hex()
    if expected_world is not None:assert world==expected_world
    at=419
    def take(size):
        nonlocal at
        assert 0<=size<=4*1024*1024 and at+size<=len(payload)-32
        data=payload[at:at+size];at+=size;return data
    def count(limit):
        value=int.from_bytes(take(4),'little');assert value<=limit;return value
    def identity(allow_zero=False):
        raw=take(24);counter=int.from_bytes(raw[16:24],'little')
        if allow_zero and counter==0:assert raw==bytes(24)
        else:assert counter>0 and raw[:16].hex()==world
        return str(counter)
    roots=[];control_part=player_root=None;recovery=[];character=None;additional=[]
    if schema>=2:take(22)
    if schema>=3:
        designs=count(4);assert schema>=4 or designs>0
        for _ in range(designs):
            size=count(131072);assert size>0;design=take(size)
            assert design[:4]==b'SVBP' and hashlib.sha256(design[:-32]).digest()==design[-32:]
            recovery.append(hashlib.sha256(design).hexdigest())
    if schema>=4:
        control_part=identity();player_root=identity(True);root_count=count(32);assert root_count>0
        for _ in range(root_count):roots.append(identity());take(64)
        assert len(set(roots))==root_count
    if schema>=5:
        extra=count(1);assert schema>=6 or extra>0
        for _ in range(extra):
            record=take(169)
            assert record[:16].hex()==world and record[24:40].hex()==world
            additional.append(hashlib.sha256(record).hexdigest())
    if schema>=6:
        values=struct.unpack('<I5d3B',take(47));profile=values[0];numbers=values[1:6];flags=values[6:]
        assert profile==1 and all(math.isfinite(n) for n in numbers) and all(n in (0,1) for n in flags)
        assert all(abs(n)<=150 for n in numbers[:3]) and abs(numbers[3])<=math.pi and 1.5<=numbers[4]<=12
        mode,aboard=payload[316:318];vertical=struct.unpack_from('<d',payload,292)[0]
        assert mode<=3 and aboard in (0,1)
        if mode in (1,2):assert not aboard and vertical==numbers[1]
        else:assert vertical==0
        character={'profile':profile,'worldVelocity':list(numbers[:3]),'facingYaw':numbers[3],
                   'cameraDistance':numbers[4],'chaseCamera':bool(flags[0]),'reducedMotion':bool(flags[1]),'loadView':bool(flags[2])}
    logical_size=count(4*1024*1024);assert logical_size>0;logical_offset=at;logical=take(logical_size)
    assert logical[:4]==b'SVSC' and hashlib.sha256(logical[:-32]).digest()==logical[-32:]
    parent_size=count(4*1024*1024);parent=take(parent_size)
    if parent_size:assert parent[:4]==b'SVSC' and hashlib.sha256(parent[:-32]).digest()==parent[-32:]
    assert at==len(payload)-32
    return {'schema':schema,'rootKeys':roots,'controlPart':control_part,'playerRoot':player_root,
            'recoveryDesignDigests':recovery,'additionalCargoCount':len(additional),'additionalCargoDigests':additional,
            'character':character,'logicalOffset':logical_offset,'logicalBytes':logical_size,'parentBytes':parent_size}


def archive(slot):
    a=(slot/'current').read_bytes();b=(slot/'mirror').read_bytes();assert a==b,'save replicas must agree after acknowledgment'
    assert a[:4]==b'SVSG' and a[4:8]==(1).to_bytes(4,'little')
    assert hashlib.sha256(a[:-32]).digest()==a[-32:]
    size=int.from_bytes(a[32:40],'little');payload=a[40:-32];assert len(payload)==size
    details=archive_payload(payload,a[8:24].hex())
    return {'world':a[8:24].hex(),'generation':int.from_bytes(a[24:32],'little'),
        'tick':int.from_bytes(payload[8:16],'little'),**details,
        'bytes':len(payload),'sha256':hashlib.sha256(payload).hexdigest()},payload

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--binary',type=Path,default=Path('bazel-bin/voxy_native'))
    parser.add_argument('--storage-root',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();args.storage_root=args.storage_root.resolve();args.storage_root.mkdir(parents=True,exist_ok=False)
    args.output.mkdir(parents=True,exist_ok=False)
    report={'status':'running','kind':'native X11 key events, manual checkpoint, process termination and restart; no screenshots',
        'binarySha256':hashlib.sha256(args.binary.resolve().read_bytes()).hexdigest(),'stages':[]}
    x=X11();child=None;stream=None;index=0
    def record(name,**data):
        report['stages'].append({'name':name,**data});(args.output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
    def text():return (args.output/f'process-{index}.log').read_text(errors='replace')
    def wait(predicate,label,seconds=60):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            if child.poll() is not None:raise RuntimeError(f'Game exited ({child.returncode}): {label}; {text()[-3000:]}')
            value=predicate()
            if value:return value
            time.sleep(.04)
        raise RuntimeError('Timed out: '+label+'; '+text()[-2000:])
    def start(world=None):
        nonlocal child,stream,index
        index+=1;stream=(args.output/f'process-{index}.log').open('w')
        command=[str(args.binary.resolve()),'--config','salvage_cove.cfg','--expedition-root',str(args.storage_root),'--width','960','--height','540']
        if world:command+=['--expedition-world',world]
        child=subprocess.Popen(command,stdout=stream,stderr=subprocess.STDOUT,env={**os.environ,'VOXY_WINDOW_BACKEND':'x11'})
        window=wait(lambda:x.own_window(child.pid),'own native window')
        wait(lambda:'Salvage load active:' in text(),'physical boat and cargo initialized')
        if world:wait(lambda:'Expedition restored state:' in text(),'saved physical world ready')
        else:time.sleep(.3)
        return window
    def latest_state(kind):
        lines=[line.split(kind+': ',1)[1] for line in text().splitlines() if kind+': ' in line]
        return json.loads(re.sub(r'\x1b\[[0-9;]*m','',lines[-1])) if lines else None
    def buy(window):
        x.key(window,'b');wait(lambda:'Workshop |' in x.title(window),'open workshop');record('workshop-open',title=x.title(window))
        x.key(window,'v');wait(lambda:'Fits.' in x.title(window),'preview added pontoon',10);record('pontoon-preview',title=x.title(window));x.key(window,'e');record('pontoon-keep-input',title=x.title(window))
        wait(lambda:'Design kept.' in x.title(window),'keep bought pontoon')
        x.key(window,'Return');wait(lambda:'Workshop |' not in x.title(window),'launch bought pontoon');time.sleep(.2)
    def save(window,label):
        x.key(window,'p');wait(lambda:'Cove paused' in x.title(window),'pause joins physical state')
        x.key(window,'F10');state=wait(lambda:latest_state('Expedition checkpoint state'),'disk save acknowledged')
        meta,payload=archive(args.storage_root/state['world']);assert str(meta['tick'])==state['pause']['tick']
        assert meta['schema']==6 and meta['additionalCargoCount']==0 and meta['rootKeys']==[root['key'] for root in state['boat']['roots']]
        assert meta['controlPart']==state['boat']['controlPart'] and meta['playerRoot']==state['player']['rootKey']
        assert state['boat']['joinedTick']==str(meta['tick'])
        (args.output/(label+'.svce')).write_bytes(payload);record(label,state=state,archive=meta);return state,meta
    def stop():
        nonlocal child,stream
        if child and child.poll() is None:child.send_signal(signal.SIGTERM);child.wait(timeout=15)
        if stream:stream.close()
        child=None;stream=None
    def refused(world,label,reason):
        command=[str(args.binary.resolve()),'--config','salvage_cove.cfg','--expedition-root',str(args.storage_root),'--expedition-world',world]
        result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=15,
            env={**os.environ,'VOXY_WINDOW_BACKEND':'x11'})
        (args.output/(label+'.log')).write_text(result.stdout)
        assert result.returncode!=0 and reason in result.stdout, (label,result.returncode,result.stdout[-2000:])
        assert 'Application initialized successfully' not in result.stdout
        record(label,exitCode=result.returncode,expectedReason=reason)
    try:
        window=start();buy(window);first,first_meta=save(window,'first-paid-save');assert first['boat']['parts']==12
        assert first['boat']['massKg']==1155 and first['session']['inventory']['salvageMaterial']=='24'
        refused(first['world'],'competing-process-refused','This expedition is open in another process.')
        assert archive(args.storage_root/first['world'])[0]==first_meta
        stop();record('first-process-terminated-after-save')
        window=start(first['world']);loaded=latest_state('Expedition restored state')
        assert loaded['pause']['phase']=='paused' and int(loaded['pause']['tick'])==first_meta['tick']+1
        assert loaded['boat']['paidPartIds']==first['boat']['paidPartIds'] and loaded['session']['inventory']==first['session']['inventory']
        assert int(loaded['observation']['epoch'])==int(first['observation']['epoch'])+1
        record('first-restarted',state=loaded)
        x.key(window,'p');wait(lambda:'Cove paused' not in x.title(window),'resume loaded expedition')
        buy(window);second,second_meta=save(window,'purchase-after-restart');assert second['boat']['parts']==13
        assert second['boat']['massKg']==1275 and second['session']['inventory']['salvageMaterial']=='0'
        assert first['boat']['paidPartIds'][0] in second['boat']['paidPartIds'] and len(set(second['boat']['paidPartIds']))==2
        stop();record('second-process-terminated-after-save')
        window=start(first['world']);final=latest_state('Expedition restored state')
        assert final['pause']['phase']=='paused' and int(final['pause']['tick'])==second_meta['tick']+1
        assert final['boat']['paidPartIds']==second['boat']['paidPartIds'] and final['session']['inventory']==second['session']['inventory']
        assert final['boat']['massKg']==1275 and final['terrainSurface']=='lego';record('second-restarted',state=final)
        refused('00000000000000000000000000000001','missing-selected-world-refused','The selected expedition is missing.')
        report['status']='passed'
    except Exception as error:report.update(status='failed',error=str(error));raise
    finally:
        stop();x.close();(args.output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
