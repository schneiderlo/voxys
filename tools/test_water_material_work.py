#!/usr/bin/env python3
"""Dependency-free CPU/structure regressions; these do not execute WGSL."""
from pathlib import Path
import math
import random
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
RAY = (ROOT / 'shaders/ray_blit.wgsl').read_text()
WATER = (ROOT / 'shaders/water_clipmap.wgsl').read_text()
BLIT = (ROOT / 'src/render/blit_path.cpp').read_text()

def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]

def gradient(cell):
    x, y = (v - math.floor(v / 16) * 16 for v in cell)
    def channel(a, b):
        value = f32(f32(math.sin(f32(f32(x * a) + f32(y * b)))) * f32(43758.5453123))
        return f32(f32((value - math.floor(value)) * 2) - 1)
    return channel(f32(127.1), f32(311.7)), channel(f32(269.5), f32(183.3))

def corners(point):
    x, y = map(math.floor, point)
    return [gradient((x, y)), gradient((x + 1, y)),
            gradient((x, y + 1)), gradient((x + 1, y + 1))]

def evaluate(point, values):
    x, y = (p - math.floor(p) for p in point)
    sx = x*x*x*(x*(x*6-15)+10)
    sy = y*y*y*(y*(y*6-15)+10)
    dots = [a * (x-dx) + b * (y-dy)
            for (a, b), (dx, dy) in zip(values, [(0,0),(1,0),(0,1),(1,1)])]
    a = dots[0]*(1-sx) + dots[1]*sx
    b = dots[2]*(1-sx) + dots[3]*sx
    return a*(1-sy)+b*sy

def shared(points):
    cells = [tuple(map(math.floor, p)) for p in points]
    if len(set(cells)) != 1:
        return [evaluate(p, corners(p)) for p in points]
    cached = corners(points[0])
    return [evaluate(p, cached) for p in points]

def smooth(a, b, x):
    t = max(0, min(1, (x-a)/(b-a)))
    return t*t*(3-2*t)

class WorkReductionTests(unittest.TestCase):
    def test_footprints_same_cells_and_discontinuities(self):
        rng = random.Random(77543)
        for i in range(10000):
            p = (f32(rng.uniform(-10000, 10000)), f32(rng.uniform(-10000,10000)))
            size = [0, 0.0001, 0.01, 0.5, 1, 17, 512][i % 7]
            points = [p, (f32(p[0]+size), p[1]), (p[0], f32(p[1]-size))]
            self.assertEqual(shared(points), [evaluate(q, corners(q)) for q in points])

    def test_negative_wrap_and_large_float_boundaries(self):
        for x in [-2**24, -1024, -16, -1, 0, 1, 16, 1024, 2**24]:
            for e in [-0.001, 0, 0.001]:
                p = (f32(x+e), f32(-x+e))
                points = [p, (f32(p[0]+0.25),p[1]), (p[0],f32(p[1]+0.25))]
                self.assertEqual(shared(points), [evaluate(q, corners(q)) for q in points])

    def test_wet_band_conservative_bound(self):
        rng = random.Random(103)
        for _ in range(20000):
            a, b = rng.uniform(-2,2), rng.uniform(-2,2)
            limit = max(0.10+a*0.065+b*0.020,0.04)
            self.assertLess(limit, 0.30)
            self.assertEqual(1-smooth(0.015,limit,0.30),0)
            self.assertEqual(1-smooth(0.015,limit,0.015),1)
        self.assertIn('relativeElevation < 0.30', RAY)
        self.assertIn('relativeElevation > 0.015', RAY)

    def test_foam_cutoffs_cover_all_discarded_terms(self):
        for d in [0.92,1,1.75,2.35,3,20,500]:
            self.assertEqual(smooth(.07,.24,d)*(1-smooth(.48,.92,d)),0)
            self.assertEqual(1-smooth(.16,.62,d),0)
            if d >= 2.35:
                self.assertEqual(smooth(.035,.18,d)*(1-smooth(1.10,2.35,d)),0)
        self.assertIn('waterDepth >= 0.92', WATER)
        self.assertIn('waterDepth >= 2.35', RAY)

    def test_shared_footprint_keeps_discontinuity_fallback(self):
        self.assertIn('!all(floor(pointX) == cell) || !all(floor(pointY) == cell)',RAY)
        self.assertIn('let footprint = terrainMaterialUvFootprint(',RAY)
        self.assertIn('let gradientX = uvX - uv;',RAY)
        self.assertIn('let gradientY = uvY - uv;',RAY)
        self.assertNotIn('@binding(19)',RAY)
        self.assertNotIn('dpdx',RAY)

    def test_render_timestamps_cover_background_and_particles(self):
        self.assertIn('"blit_static_background_pass", true, false, true',BLIT)
        self.assertIn('lightingTimestampStarted\n                ? WGPU_QUERY_SET_INDEX_UNDEFINED : timestampBegin',BLIT)
        self.assertIn('particleTimestamps.endOfPassWriteIndex = timestampEnd',BLIT)
        self.assertIn('timestampWrites.endOfPassWriteIndex = drawParticles',BLIT)
        for cached in [False,True]:
            for refresh in [False,True]:
                for particles in [False,True]:
                    writes = []
                    if cached and refresh: writes += ['begin']
                    if not(cached and refresh): writes += ['begin']
                    if not particles: writes += ['end']
                    if particles: writes += ['end']
                    self.assertEqual(writes,['begin','end'])

    def test_refraction_and_total_internal_reflection_are_branches(self):
        self.assertIn('var refracted : vec3<f32>;\n    if (hasOpaqueRefraction)',WATER)
        self.assertIn('} else if (hasSceneRefraction && sceneHasOpaque)',RAY)
        for source in [RAY,WATER]:
            self.assertIn('var underside : vec3<f32>;\n        if (totalInternalReflection)',source)
            self.assertNotIn('var underside = select(',source)

    def test_telemetry_reports_sample_age_and_real_queue_limit(self):
        source=(ROOT/'src/engine/platform/wasm/entry.cpp').read_text()
        web=(ROOT/'web/index.html').read_text()
        for field in ['render_gpu','age_frames','gpu_queue_limit','lighting_composite_ms']:
            self.assertIn(field,source)
        self.assertIn('GPU queue ${inFlight}/${queueLimit}',web)
        self.assertIn('return static_cast<int>(kMaximumGpuFramesInFlight)',source)

if __name__ == '__main__':
    unittest.main(verbosity=2)
