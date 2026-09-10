"""Ray-check the exported helm well against inserted peg profiles in Blender.

This reads GLBs into a fresh temporary scene. It never writes an asset source.
The retained r08 defect is the positive control for the r09 clearance check.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pontoon_parameters as parameters


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--old-dir', type=Path, required=True)
    parser.add_argument('--candidate-dir', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    if not bpy.app.background or '--factory-startup' not in sys.argv:
        parser.error('requires background factory startup')
    if args.report.exists():
        parser.error('report must be new')
    report = dict(status='running', blender_version=bpy.app.version_string,
                  scope='Exported GLB vertical peg-insertion ray samples; no live collision or exhaustive intersection proof',
                  runner_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), checks=[])
    try:
        for kind, directory in [('old', args.old_dir), ('candidate', args.candidate_dir)]:
            for lod in range(3):
                path = directory / f'helm-lod-{lod}.glb'
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                bpy.ops.wm.read_factory_settings(use_empty=True)
                bpy.ops.import_scene.gltf(filepath=str(path.resolve()))
                vertices, triangles = [], []
                for obj in bpy.context.scene.objects:
                    if obj.type != 'MESH':
                        continue
                    offset = len(vertices)
                    # The glTF importer reconstructs Blender's authoring frame.
                    # Convert evaluated world vertices back to canonical +Y up.
                    for vertex in obj.data.vertices:
                        v = obj.matrix_world @ vertex.co
                        vertices.append(Vector((-v.x, v.z, v.y)))
                    obj.data.calc_loop_triangles()
                    triangles.extend(tuple(offset+i for i in tri.vertices) for tri in obj.data.loop_triangles)
                assert vertices and triangles
                tree = BVHTree.FromPolygons(vertices, triangles, all_triangles=True)
                samples = []
                for peg_lod in range(3):
                    ring = parameters.keyed_profile(*parameters.mount_shape(peg_lod))
                    points = [(0, 0)] + [(x*.99, z*.99) for x, z in ring]
                    for x, z in points:
                        origin = Vector((x, -.49, z))
                        location, _, _, _ = tree.ray_cast(origin, Vector((0, 1, 0)), 2)
                        # A through-hole has no hit; otherwise the inserted peg
                        # ends at -.30 m and must not reach the next solid face.
                        hit_y = float(location.y) if location is not None else None
                        samples.append(dict(peg_lod=peg_lod, x=x, z=z, first_solid_y=hit_y,
                                            clear=hit_y is None or hit_y >= -.30-1e-5))
                failed = [sample for sample in samples if not sample['clear']]
                assert (bool(failed) if kind == 'old' else not failed), (kind, lod, failed[:1])
                assert hashlib.sha256(path.read_bytes()).hexdigest() == digest
                report['checks'].append(dict(kind=kind, lod=lod, source=str(path), source_sha256=digest,
                                              samples=len(samples), blocked_samples=len(failed), rays=samples))
        report['status'] = 'passed; old obstruction reproduced and corrected candidate clears all sampled peg profiles'
    except Exception as error:
        report.update(status='failed', error=str(error))
        raise
    finally:
        args.report.write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__':
    main()
