#!/usr/bin/env python3
"""CPU indexing/source regressions; real WGSL checks live in the Node GPU test."""
from pathlib import Path
import math
import random
import re
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def wrap(value):
    value = f32(value)
    return f32(value - f32(math.floor(f32(value / 16.0)) * 16.0))


def gradient(x, y):
    x, y = wrap(x), wrap(y)
    result = []
    for a, b in ((127.1, 311.7), (269.5, 183.3)):
        phase = f32(f32(x * f32(a)) + f32(y * f32(b)))
        hashed = f32(f32(math.sin(phase)) * f32(43758.5453123))
        result.append(f32(f32((hashed - math.floor(hashed)) * 2.0) - 1.0))
    return tuple(result)


def make_table():
    return [[(gradient(x, y), gradient(x + 1, y),
              gradient(x, y + 1), gradient(x + 1, y + 1))
             for x in range(16)] for y in range(16)]


def noise(point, corners):
    x, y = (value - math.floor(value) for value in point)
    u = x*x*x*(x*(x*6 - 15) + 10)
    v = y*y*y*(y*(y*6 - 15) + 10)
    a, b, c, d = (g[0]*dx + g[1]*dy for g, dx, dy in
                  zip(corners, (x, x-1, x, x-1), (y, y, y-1, y-1)))
    return ((a*(1-u) + b*u)*(1-v) + (c*(1-u) + d*u)*v)


class PeriodicGradientLutTest(unittest.TestCase):
    def test_every_wrapped_cell_and_corner(self):
        table = make_table()
        for y in range(-65, 66):
            for x in range(-65, 66):
                self.assertEqual(table[int(wrap(y))][int(wrap(x))],
                    (gradient(x, y), gradient(x+1, y),
                     gradient(x, y+1), gradient(x+1, y+1)))

    def test_interpolation_and_negative_boundaries(self):
        table = make_table()
        rng = random.Random(0x153264)
        points = [(f32(rng.uniform(-16384, 16384)),
                   f32(rng.uniform(-16384, 16384))) for _ in range(10000)]
        for n in (-65536, -33, -32, -17, -16, -1, 0, 1, 15, 16, 31, 32, 65536):
            points += [(f32(n+delta), f32(-n-delta))
                       for delta in (-0.0001, 0, 0.0001, 0.375, 0.9999)]
        for point in points:
            x, y = map(math.floor, point)
            direct = (gradient(x, y), gradient(x+1, y),
                      gradient(x, y+1), gradient(x+1, y+1))
            cached = table[int(wrap(y))][int(wrap(x))]
            self.assertEqual(noise(point, direct), noise(point, cached))

    def test_large_float_lattice_coordinates_stay_in_range(self):
        for n in (-2**30, -2**24, -8388607, -65536, 0, 65536, 8388607, 2**24, 2**30):
            for offset in range(-33, 34):
                value = f32(n + offset)
                self.assertEqual(wrap(value), int(value) % 16)
                self.assertGreaterEqual(wrap(value), 0)
                self.assertLess(wrap(value), 16)

    def test_production_resource_contract(self):
        host = (ROOT/'src/render/blit_path.cpp').read_text()
        header = (ROOT/'src/render/periodic_gradient_lut.hpp').read_text()
        self.assertEqual(host.count('gpu::BindGroupLayoutEntry(19)'), 2)
        self.assertEqual(host.count('gpu::BindGroupEntry(19)'), 3)
        self.assertIn('32u, 16u, WGPUTextureFormat_RGBA32Float', header)
        self.assertIn('wgpuComputePassEncoderDispatchWorkgroups(pass.get(), 2u, 2u, 1u)', header)
        self.assertEqual(header.count('wgpuQueueSubmit('), 1)
        # Calls, not comments: production initialization never reads back/waits.
        self.assertNotRegex(header, r'\b(?:wgpuDevicePoll|wgpuBufferMapAsync|wgpuQueueOnSubmittedWorkDone)\s*\(')
        self.assertIn('periodic_gradient_lut.hpp', (ROOT/'src/render/BUILD').read_text())
        self.assertIn('periodicGradientLut_.reset();', host)
        self.assertEqual(host.count('std::move(other.periodicGradientLut_)'), 2)
        for shader in ('ray_blit.wgsl', 'water_clipmap.wgsl'):
            source = (ROOT/'shaders'/shader).read_text()
            self.assertIn('@binding(19) var periodicGradientLut : texture_2d<f32>', source)
            self.assertIn('override USE_PERIODIC_GRADIENT_LUT : bool = true', source)
            self.assertEqual(source.count('fn periodicGradientNoise('), 1)
            # Both packed rows remain unfiltered f32 loads.
            self.assertIn('let ab = textureLoad(periodicGradientLut, coordinate, 0)', source)
            self.assertIn('let cd = textureLoad(periodicGradientLut, coordinate + vec2<i32>(1, 0), 0)', source)
            self.assertIn('return fract(sin(phase) * 43758.5453123)', source)

    def test_bake_pass_released_before_submission(self):
        source=(ROOT / 'src/render/periodic_gradient_lut.hpp').read_text()
        end=source.index('wgpuComputePassEncoderEnd(pass.get())')
        release=source.index('wgpuComputePassEncoderRelease(pass.release())')
        finish=source.index('wgpuCommandEncoderFinish(encoder.get()')
        submit=source.index('wgpuQueueSubmit(queue')
        self.assertLess(end,release)
        self.assertLess(release,finish)
        self.assertLess(finish,submit)

    def test_background_timing_is_not_silently_excluded(self):
        source = (ROOT/'src/render/blit_path.cpp').read_text()
        self.assertIn('"blit_static_background_pass", true, false, true)', source)
        self.assertIn('timestampWrites.beginningOfPassWriteIndex = lightingTimestampStarted', source)
        self.assertIn('(beginTimestampOnly || deferTimestampEnd)', source)
        self.assertIn('particleTimestamps.endOfPassWriteIndex = timestampEnd', source)


if __name__ == '__main__':
    unittest.main(verbosity=2)
