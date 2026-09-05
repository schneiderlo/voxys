#!/usr/bin/env python3
"""Index, fp32 butterfly and dispatch contracts; actual WGSL tests are separate."""
import cmath
import random
import struct
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def f(x): return struct.unpack('f',struct.pack('f',x))[0]
def add(a,b):return (f(a[0]+b[0]),f(a[1]+b[1]))
def sub(a,b):return (f(a[0]-b[0]),f(a[1]-b[1]))
def mul(a,b):return (f(f(a[0]*b[0])-f(a[1]*b[1])),f(f(a[0]*b[1])+f(a[1]*b[0])))
def reverse(i):return int(f'{i:08b}'[::-1],2)
TW=[]
for stage in range(8):
 for j in range(1<<stage):
  w=cmath.exp(2j*cmath.pi*j/(2<<stage));TW.append((f(w.real),f(w.imag)))
def fft(data,paired):
 a=[data[reverse(i)] for i in range(256)]
 for stage in range(0,8,2 if paired else 1):
  h=1<<stage
  for t in range(64 if paired else 128):
   j=t%(h);i=(t//h)*h*(4 if paired else 2)+j
   x=a[i];y=mul(a[i+h],TW[h-1+j])
   if paired:
    z=a[i+2*h];w=mul(a[i+3*h],TW[h-1+j])
    v0=add(x,y);v1=sub(x,y)
    v2=mul(add(z,w),TW[2*h-1+j]);v3=mul(sub(z,w),TW[3*h-1+j])
    a[i]=add(v0,v2);a[i+h]=add(v1,v3);a[i+2*h]=sub(v0,v2);a[i+3*h]=sub(v1,v3)
   else:a[i]=add(x,y);a[i+h]=sub(x,y)
 return a
class Test(unittest.TestCase):
 def test_no_shared_write_collisions(self):
  for stage in range(0,8,2):
   h=1<<stage;indices=[]
   for t in range(64):
    j=t%h;i=t//h*h*4+j;indices.extend(i+k*h for k in range(4))
   self.assertEqual(sorted(indices),list(range(256)))
 def test_twiddle_indices_are_the_same_pair_of_radix_two_stages(self):
  for stage in range(0,8,2):
   h=1<<stage
   for t in range(64):
    j=t%h
    for i in [h-1+j,2*h-1+j,3*h-1+j]:self.assertTrue(0<=i<255)
 def test_fp32_butterfly_parity(self):
  rng=random.Random(987)
  fixtures=[[(0.,0.)]*256,[(1.,0.)]+[(0.,0.)]*255]
  fixtures += [[(f(rng.uniform(-10,10)),f(rng.uniform(-10,10))) for i in range(256)] for _ in range(20)]
  for data in fixtures:self.assertEqual(fft(data,False),fft(data,True))
 def test_independent_inverse_dft(self):
  rng=random.Random(89);data=[(f(rng.uniform(-1,1)),f(rng.uniform(-1,1))) for _ in range(256)]
  actual=fft(data,True)
  for k in [0,1,7,63,128,255]:
   expected=sum(complex(*x)*cmath.exp(2j*cmath.pi*i*k/256) for i,x in enumerate(data))
   self.assertLess(abs(complex(*actual[k])-expected),1e-4)
 def test_full_dispatch_preserves_both_cascades(self):
  rows=set();columns=set()
  for line in range(512):
   for t in range(256):
    rows.add(line*256+t);columns.add((line//256)*65536+(line%256)+t*256)
  self.assertEqual(rows,set(range(131072)));self.assertEqual(columns,rows)
 def test_live_time_uniform_and_no_intermediate_dispatch(self):
  s=(ROOT/'src/render/water_simulation.cpp').read_text();s=s[s.index('void WaterSimulation::update('):]
  self.assertIn('evolveBindGroup_',s);self.assertIn('fftAxisBindGroups_[1]',s)
  self.assertNotIn('evolveGroups',s);self.assertEqual(s.split('void WaterSimulation::shutdown')[0].count('wgpuComputePassEncoderDispatchWorkgroups('),3)
 def test_frame_envelope_wraps_all_work_not_just_stage_sum(self):
  s=(ROOT/'src/app/application.cpp').read_text();a=s.index('void Application::render()');b=s.index('void Application::endFrame()',a);s=s[a:b]
  self.assertLess(s.index('kRenderGpuFrameBeginQuery'),s.index('waterSimulation_->update('))
  self.assertLess(s.index('renderMoto(encoder'),s.index('kRenderGpuFrameEndQuery'))
  self.assertLess(s.index('kRenderGpuFrameEndQuery'),s.index('wgpuCommandEncoderResolveQuerySet'))
  self.assertIn('frameBegin != 0u', (ROOT/'src/app/application.cpp').read_text())
 def test_configuration_quality_and_cadence(self):
  s=(ROOT/'src/render/water_simulation.cpp').read_text();self.assertIn('1.0f / SPECTRAL_UPDATE_HZ',s)
  h=(ROOT/'src/render/water_simulation.hpp').read_text()
  self.assertIn('RESOLUTION = 256',h);self.assertIn('CASCADE_COUNT = 2',h)
if __name__=='__main__':unittest.main(verbosity=2)
