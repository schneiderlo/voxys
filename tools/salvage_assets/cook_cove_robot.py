#!/usr/bin/env python3
"""Publish a new, complete original robot bundle through the strict real cooker.

This offline recipe never changes gameplay registries or an existing bundle.
The executable is explicit; failed conversion is retained as an error and is
never retried through the legacy glTF profile.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool',type=Path,required=True)
    parser.add_argument('--source-dir',type=Path,required=True)
    parser.add_argument('--output-dir',type=Path,required=True)
    args=parser.parse_args()
    source=args.source_dir.resolve();output=args.output_dir.absolute();tool=args.tool.resolve()
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        parser.error('output must be a new directory with an existing parent')
    provenance=json.loads((source/'provenance.json').read_text())
    if provenance.get('profile')!='salvage-animated-rigid-v1' or len(provenance.get('lods',[]))!=3:
        parser.error('expected the complete authored three-LOD robot source')
    records=[]
    with tempfile.TemporaryDirectory(prefix='.robot-cook-',dir=output.parent) as temporary:
        pending=Path(temporary)/'package';pending.mkdir();(pending/'source').mkdir()
        shutil.copyfile(source/'provenance.json',pending/'provenance.json')
        for lod,filename in enumerate(('robot.vmesh','lod1.vmesh','lod2.vmesh')):
            authored=provenance['lods'][lod]
            for extension in ('glb','blend'):
                path=source/'source'/f'robot-lod-{lod}.{extension}'
                if path.is_symlink() or not path.is_file() or path.stat().st_size>16*1024*1024:
                    parser.error('source must be a bounded regular authored file')
                if sha(path)!=authored[extension+'_sha256']:
                    parser.error('authored source hash mismatch')
                shutil.copyfile(path,pending/'source'/path.name)
            completed=subprocess.run([str(tool),'--profile','salvage-animated-rigid-v1',
                str(pending/'source'/f'robot-lod-{lod}.glb'),str(pending/filename)],
                capture_output=True,text=True,timeout=60)
            if completed.returncode:
                raise RuntimeError(f'LOD{lod}: strict cook refused: {completed.stderr}')
            payload=(pending/filename).read_bytes()
            if len(payload)<256 or len(payload)>2*1024*1024 or payload[:8]!=b'VOXYMESH':
                raise RuntimeError('strict cooker emitted invalid/oversized VMESH')
            counts=struct.unpack_from('<12I',payload,16)
            gpu=counts[0]*72+counts[2]*4+counts[5]*64
            if counts[6]!=22 or counts[7]!=15 or counts[4]!=34 or counts[10]!=8 or counts[8] or counts[9]:
                raise RuntimeError('cooked hierarchy/clip/skin contract mismatch')
            if gpu>1024*1024: raise RuntimeError('robot GPU payload cap exceeded')
            records.append(dict(lod=lod,asset_id=f'voxys-cove-robot-r01-lod{lod}',filename=filename,
                sha256=sha(pending/filename),bytes=len(payload),gpu_bytes=gpu,
                vertex_count=counts[0],index_count=counts[2],mesh_count=counts[7],node_count=counts[6],
                expanded_draws=counts[4],clips=counts[10],channels=counts[11],
                source_sha256=authored['glb_sha256'],cook_exit=completed.returncode,cook_stderr=completed.stderr))
        manifest=dict(schema=1,profile='salvage-animated-rigid-v1',asset_id='voxys-cove-robot-r01',
            render_to_canonical=12,filename='robot.vmesh',sha256=records[0]['sha256'],
            maximum_nodes=32,maximum_meshes=24,maximum_draws=48,
            clips=provenance['clips'],anchors=provenance['anchors'],
            source_sha256=records[0]['source_sha256'],provenance_sha256=sha(pending/'provenance.json'),
            cooker_sha256=sha(tool),cook_recipe_sha256=sha(Path(__file__)),lods=records)
        (pending/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        if output.exists() or output.is_symlink(): raise RuntimeError('output appeared during cook; refusing overwrite')
        os.rename(pending,output)
    print(json.dumps(dict(output=str(output),manifest_sha256=sha(output/'manifest.json'),lods=records),indent=2))


if __name__=='__main__':main()
