#!/usr/bin/env python3
"""Strictly cook a new six-LOD Cove scenery package; never update live content."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
from check_cove_environment import check


def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('tool','source-dir','output-dir'):parser.add_argument('--'+name,type=Path,required=True)
    a=parser.parse_args();source=a.source_dir.resolve();output=a.output_dir.absolute();tool=a.tool.resolve(strict=True)
    if output.exists() or output.is_symlink() or not output.parent.is_dir():parser.error('new output required')
    validation=check(source);provenance=json.loads((source/'provenance.json').read_text())
    assert provenance['profile']=='salvage-rigid-v1' and provenance['asset_id']=='voxys-cove-environment-r01'
    records=[];gpu_total=0
    with tempfile.TemporaryDirectory(prefix='.cove-environment-',dir=output.parent) as tmp:
        package=Path(tmp)/'package';package.mkdir();(package/'source').mkdir()
        shutil.copy2(source/'provenance.json',package/'provenance.json')
        for record in provenance['lods']:
            kind,lod=record['kind'],record['lod'];assert kind in ('scenery','gantry') and 0<=lod<=2
            for ext in ('glb','blend'):
                src=source/'source'/f'{kind}-lod-{lod}.{ext}'
                if src.is_symlink() or not src.is_file() or src.stat().st_size>16*1024*1024:raise RuntimeError('invalid authored source')
                if sha(src)!=record[ext+'_sha256']:raise RuntimeError('source provenance digest differs')
                shutil.copy2(src,package/'source'/src.name)
            filename=f'{kind}-lod{lod}.vmesh'
            process=subprocess.run([str(tool),'--profile','salvage-rigid-v1',str(package/'source'/f'{kind}-lod-{lod}.glb'),str(package/filename)],capture_output=True,text=True,timeout=60)
            if process.returncode:raise RuntimeError(f'{filename}: strict cook failed: {process.stderr}')
            payload=(package/filename).read_bytes();assert payload[:8]==b'VOXYMESH' and 256<=len(payload)<=1024*1024
            counts=struct.unpack_from('<12I',payload,16)
            assert counts[6]==counts[7]==(5 if kind=='scenery' else 1)
            assert not any(counts[i] for i in (8,9,10,11))
            gpu=counts[0]*72+counts[2]*4+counts[5]*64;gpu_total+=gpu
            records.append(dict(kind=kind,lod=lod,asset_id=f'voxys-cove-environment-r01-{kind}-lod{lod}',filename=filename,
                sha256=sha(package/filename),bytes=len(payload),gpu_bytes=gpu,vertices=counts[0],indices=counts[2],
                nodes=counts[6],meshes=counts[7],draws=counts[4],materials=counts[5],source_sha256=record['glb_sha256'],
                cook_exit=process.returncode,cook_stderr=process.stderr))
        assert gpu_total<=2*1024*1024
        for i in range(3):assert records[i]['draws']+records[3+i]['draws']<=90
        manifest=dict(schema=1,profile='salvage-rigid-v1',asset_id='voxys-cove-environment-r01',render_to_canonical=12,
            collision=provenance['collision'],gantry_collision=provenance['gantry_collision'],lods=records,
            scene_origin=provenance['scene_origin'],gpu_bytes=gpu_total,
            provenance_sha256=sha(package/'provenance.json'),cooker_sha256=sha(tool),cook_recipe_sha256=sha(__file__),
            checker_sha256=sha(Path(__file__).with_name('check_cove_environment.py')))
        (package/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        (package/'geometry-check.json').write_text(json.dumps(validation,indent=2)+'\n')
        assert (package/'manifest.json').stat().st_size<=64*1024
        if output.exists() or output.is_symlink():raise RuntimeError('output appeared; refusing overwrite')
        os.rename(package,output)
    print(json.dumps(dict(status='passed',output=str(output),manifest_sha256=sha(output/'manifest.json'),gpu_bytes=gpu_total,
        collision_boxes=len(manifest['collision']),records=records),indent=2))


if __name__=='__main__':main()
