#!/usr/bin/env python3
"""Temporary Linux controller for owned game-window acceptance, never game-state injection.
The descriptor is destroyed on close. Events pass through GLFW/Chrome's OS backend.
CLI accepts JSON lines: {"button":"a","down":true}, {"axis":"lx","value":0.5},
{"reset":true}, {"close":true}. Each command receives one JSON acknowledgment.
"""
import fcntl
import json
import os
import struct
import sys
import time

class VirtualGamepad:
    buttons={'a':304,'b':305,'x':307,'y':308,'lb':310,'rb':311,'view':314,'menu':315,'ls':317,'rs':318,'home':316}
    axes={'lx':0,'ly':1,'lt':2,'rx':3,'ry':4,'rt':5,'dx':16,'dy':17}
    def __init__(self):
        self.fd=os.open('/dev/uinput',os.O_WRONLY|os.O_NONBLOCK)
        try:
            for value in [1,3]:fcntl.ioctl(self.fd,0x40045564,value)
            for code in self.buttons.values():fcntl.ioctl(self.fd,0x40045565,code)
            low=[0]*64;high=[0]*64
            for name,code in self.axes.items():
                fcntl.ioctl(self.fd,0x40045567,code)
                low[code]=0 if name in ('lt','rt') else -1 if name in ('dx','dy') else -32768
                high[code]=255 if name in ('lt','rt') else 1 if name in ('dx','dy') else 32767
            # Xbox 360 USB identity uses the platform's existing standard mapping.
            data=struct.pack('80sHHHHI',b'Voxys acceptance controller',3,0x045e,0x028e,0x0110,0)
            data+=struct.pack('64i',*high)+struct.pack('64i',*low)+bytes(128*4)
            os.write(self.fd,data);fcntl.ioctl(self.fd,0x5501)
            time.sleep(.25);self.reset()
        except BaseException:
            os.close(self.fd);self.fd=-1;raise
    def event(self,kind,code,value):
        os.write(self.fd,struct.pack('llHHi',0,0,kind,code,value))
    def sync(self):self.event(0,0,0)
    def button(self,name,down):self.event(1,self.buttons[name],int(bool(down)));self.sync()
    def axis(self,name,value):
        value=float(value)
        if not -1<=value<=1:raise ValueError('axis must be -1..1')
        encoded=round(max(0,value)*255) if name in ('lt','rt') else round(value) if name in ('dx','dy') else round(value*32767)
        self.event(3,self.axes[name],encoded);self.sync()
    def reset(self):
        for code in self.buttons.values():self.event(1,code,0)
        for code in self.axes.values():self.event(3,code,0)
        self.sync()
    def close(self):
        if self.fd>=0:
            try:self.reset();fcntl.ioctl(self.fd,0x5502)
            finally:os.close(self.fd);self.fd=-1
    def __enter__(self):return self
    def __exit__(self,*_):self.close()

if __name__=='__main__':
    with VirtualGamepad() as pad:
        print(json.dumps({'ready':True,'backend':'Linux uinput'}),flush=True)
        for line in sys.stdin:
            try:
                command=json.loads(line)
                if command.get('close'):break
                if command.get('reset'):pad.reset()
                elif 'button' in command:pad.button(command['button'],command['down'])
                elif 'axis' in command:pad.axis(command['axis'],command['value'])
                else:raise ValueError('unknown input event')
                print(json.dumps({'ok':True}),flush=True)
            except Exception as error:print(json.dumps({'ok':False,'error':str(error)}),flush=True)
