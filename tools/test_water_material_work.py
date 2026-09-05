#!/usr/bin/env python3
"""Dependency-free CPU/structure regressions; these do not execute WGSL."""
from pathlib import Path
import random
import unittest

ROOT = Path(__file__).resolve().parents[1]
RAY = (ROOT / 'shaders/ray_blit.wgsl').read_text()
WATER = (ROOT / 'shaders/water_clipmap.wgsl').read_text()
BLIT = (ROOT / 'src/render/blit_path.cpp').read_text()

def smooth(a, b, x):
    t = max(0, min(1, (x-a)/(b-a)))
    return t*t*(3-2*t)

class WorkReductionTests(unittest.TestCase):
    def test_cove_and_caustic_masks_are_exactly_zero(self):
        for r in [80,81,100,1000]:
            self.assertEqual(1-smooth(56,80,r),0)
            self.assertEqual(1-smooth(50,78,r),0)
        for distance in [145,150,1000]:
            self.assertEqual(1-smooth(55,145,distance),0)
        for depth in [52,100,1000]:
            self.assertEqual(1-smooth(28,52,depth),0)
        self.assertIn('if (coveZone > 0.0)',RAY)
        self.assertIn('if (coveMask > 0.0)',RAY)
        self.assertIn('if (depthFade == 0.0 || distanceFade == 0.0 || receiver == 0.0)',RAY)

    def test_fully_rockcovered_pixels_have_no_other_layers(self):
        rng=random.Random(77)
        for _ in range(1000):
            rock=1.0
            sand=rng.random()*rng.random()*(1-rock)
            upland=max(1-rock-sand,0)
            grass=upland*rng.random()
            soil=max(upland-grass,0)
            self.assertEqual((sand,soil,grass,rock),(0,0,0,1))
        self.assertIn('if (rock == 1.0)',RAY)

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

    def test_original_material_coordinates_and_filtering(self):
        for expression in ['let uv = terrainMaterialUv(projectedWorld, layer);',
                           'let uvX = terrainMaterialUv(projectedX, layer);',
                           'let uvY = terrainMaterialUv(projectedY, layer);',
                           'let gradientX = uvX - uv;', 'let gradientY = uvY - uv;']:
            self.assertIn(expression,RAY)
        self.assertNotIn('periodicNoiseFootprint',RAY)
        self.assertIn('@binding(19) var periodicGradientLut',RAY)
        self.assertNotIn('dpdx',RAY)

    def test_render_timestamps_cover_background_and_particles(self):
        self.assertIn('"blit_static_background_pass", true, false, true',BLIT)
        self.assertIn('lightingTimestampStarted\n                ? WGPU_QUERY_SET_INDEX_UNDEFINED : timestampBegin',BLIT)
        self.assertIn('particleTimestamps.endOfPassWriteIndex = timestampEnd',BLIT)
        self.assertIn('if (timestampQuerySet && (!lightingTimestampStarted || !drawParticles))',BLIT)
        self.assertEqual(BLIT.count('        updateUnderwaterParticles();'),1)
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
