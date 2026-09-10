"""CPU-only checks of authored inputs and the independent default mass fixture."""
import copy
import json
import os
from pathlib import Path
import tempfile
import unittest

import pontoon_parameters as recipe

SPEC = Path(__file__).with_name('pontoon.spec.json')


class PontoonParameters(unittest.TestCase):
    def setUp(self):
        self.spec = recipe.read_spec(SPEC)

    def test_default_analytic_volume_and_inertia(self):
        boxes, volume, inertia = recipe.physical_recipe(self.spec)
        self.assertEqual(len(boxes), 9)
        self.assertAlmostEqual(volume, 3.538688, places=12)
        # Independent rational box-volume/parallel-axis calculation retained in design.
        for actual, expected in zip(inertia, (148.77843709759097,149.5392664399913,18.190337842725892)):
            self.assertAlmostEqual(actual, expected, places=10)
        self.assertEqual(boxes[4]['frame']['translation_ticks'], [0,0,-97])
        self.assertEqual(boxes[4]['half_extents_ticks'], [17,16,3])

    def test_symmetric_nonoverlapping_closed_regions(self):
        boxes, _, _ = recipe.physical_recipe(self.spec)
        total_first_moment = 0
        intervals = []
        for box in boxes:
            z = box['frame']['translation_ticks'][2]
            x,y,h = box['half_extents_ticks']
            total_first_moment += z*x*y*h
            intervals.append((z-h,z+h))
        self.assertEqual(total_first_moment,0)
        intervals.sort()
        self.assertEqual(intervals[0][0],-100)
        self.assertEqual(intervals[-1][1],100)
        self.assertTrue(all(left[1] == right[0] for left,right in zip(intervals,intervals[1:])))

    def test_every_peg_lod_fits_every_well_lod_at_engaged_spacing(self):
        clearances=recipe.cross_lod_clearances()
        self.assertEqual({(x['peg_lod'],x['well_lod']) for x in clearances},
                         {(p,w) for p in range(3) for w in range(3)})
        self.assertGreater(min(x['minimum_profile_clearance_metres'] for x in clearances),.0095)
        self.assertTrue(all(abs(x['axial_spare_depth_metres']-.02)<1e-12 for x in clearances))
        # The lowest-detail receiver must widen: equal-radius coarse polygons clip fine pegs.
        radius,flat,segments=recipe.mount_shape(2,True)
        self.assertGreater(radius,recipe.mount_shape(0,True)[0])
        self.assertEqual(segments,5)

    def test_version_one_is_reserved_and_unknown_fields_reject(self):
        for key,value in (('part_version',1),('schema',2),('extra','ignored')):
            spec=copy.deepcopy(self.spec);spec[key]=value
            with self.assertRaises(ValueError): recipe.validate_spec(spec)

    def test_reserved_version_two_rejects_changed_controls(self):
        for key,value in (('width_ticks',60),('dry_mass_kg',140),('seed',1980)):
            spec=copy.deepcopy(self.spec);spec[key]=value
            with self.assertRaises(ValueError):recipe.validate_spec(spec)
            spec['part_version']=3
            recipe.validate_spec(spec)

    def test_invalid_dimensions_and_numeric_types_reject(self):
        for key,value in (('width_ticks',51),('width_ticks',0),('length_ticks',210),
                          ('body_height_ticks',47),('width_ticks',True),('seed',-1),
                          ('seed',2**32),('dry_mass_kg',float('inf')),
                          ('dry_mass_kg',float('nan')),('dry_mass_kg',True)):
            with self.subTest(key=key,value=value):
                spec=copy.deepcopy(self.spec);spec[key]=value
                with self.assertRaises(ValueError): recipe.validate_spec(spec)

    def test_palette_contract_rejects_missing_invalid_and_unknown(self):
        for change in ({'cream':'NOTHEX'},{'cream':'d9c9a2'},{'other':'FFFFFF'}):
            spec=copy.deepcopy(self.spec);spec['palette_srgb'].update(change)
            with self.assertRaises(ValueError):recipe.validate_spec(spec)
        spec=copy.deepcopy(self.spec);del spec['palette_srgb']['teal']
        with self.assertRaises(ValueError):recipe.validate_spec(spec)

    def test_wider_version_three_recomputes_mass_distribution(self):
        variant=copy.deepcopy(self.spec);variant.update(width_ticks=60,part_version=3)
        recipe.validate_spec(variant)
        boxes,volume,inertia=recipe.physical_recipe(variant)
        self.assertGreater(volume,recipe.physical_recipe(self.spec)[1])
        self.assertGreater(inertia[1],recipe.physical_recipe(self.spec)[2][1])
        self.assertEqual(boxes[0]['half_extents_ticks'][0],30)

    def test_all_allowed_size_corners_have_integral_positive_boxes(self):
        for width in (50,80):
            for height in (48,72):
                for length in (200,300):
                    spec=copy.deepcopy(self.spec);spec.update(part_version=3,width_ticks=width,body_height_ticks=height,length_ticks=length)
                    recipe.validate_spec(spec)
                    boxes,volume,inertia=recipe.physical_recipe(spec)
                    self.assertGreater(volume,0)
                    self.assertTrue(all(v>0 for v in inertia))
                    self.assertTrue(all(type(v) is int and v>0 for b in boxes for v in b['half_extents_ticks']))

    def test_duplicate_keys_oversized_inputs_and_nonfinite_json_reject(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'spec.json'
            for contents in ('{"schema":1,"schema":1}', ' '*65537, '{"dry_mass_kg":NaN}'):
                path.write_text(contents)
                with self.assertRaises(ValueError):recipe.read_spec(path)

    def test_symlink_input_rejects(self):
        if not hasattr(os,'O_NOFOLLOW'):self.skipTest('host has no O_NOFOLLOW')
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'link.json';path.symlink_to(SPEC.resolve())
            with self.assertRaises(OSError):recipe.read_spec(path)

    def test_fifo_input_is_nonblocking_and_rejects(self):
        if not hasattr(os,'mkfifo') or not hasattr(os,'O_NONBLOCK'):self.skipTest('POSIX FIFO test')
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'pipe.json';os.mkfifo(path)
            with self.assertRaises(ValueError):recipe.read_spec(path)


if __name__=='__main__':unittest.main()
