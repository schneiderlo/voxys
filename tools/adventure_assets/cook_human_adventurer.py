#!/usr/bin/env python3
"""Strict, hash-bound human publication through the existing rigid clip cooker."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tool',type=Path,required=True);p.add_argument('--source-dir',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
    a=p.parse_args();source=a.source_dir.resolve();output=a.output_dir.absolute();tool=a.tool.resolve()
    if output.exists() or output.is_symlink() or not output.parent.is_dir():p.error('new output directory required')
    provenance=json.loads((source/'provenance.json').read_text())
    if provenance.get('asset')!='original-warm-brick-minifigure' or provenance.get('profile')!='salvage-animated-rigid-v1' or len(provenance.get('lods',[]))!=3:p.error('complete authored human source required')
    identity='voxys-free-build-builder-r01' if provenance.get('variant')=='classic-builder' else 'voxys-adventure-human-r01'
    records=[]
    with tempfile.TemporaryDirectory(prefix='.human-cook-',dir=output.parent) as tmp:
        pending=Path(tmp)/'package';pending.mkdir();(pending/'source').mkdir()
        shutil.copyfile(source/'provenance.json',pending/'provenance.json')
        for lod,filename in enumerate(('human.vmesh','lod1.vmesh','lod2.vmesh')):
            authored=provenance['lods'][lod]
            for extension in ('glb','blend'):
                path=source/'source'/f'human-lod-{lod}.{extension}'
                if path.is_symlink() or not path.is_file() or path.stat().st_size>16*1024*1024 or sha(path)!=authored[extension+'_sha256']:raise RuntimeError('source bounds/hash mismatch')
                shutil.copyfile(path,pending/'source'/path.name)
            result=subprocess.run([str(tool),'--profile','salvage-animated-rigid-v1',str(pending/'source'/f'human-lod-{lod}.glb'),str(pending/filename)],capture_output=True,text=True,timeout=60)
            if result.returncode:raise RuntimeError(f'LOD{lod} strict cook refused: {result.stderr}')
            payload=(pending/filename).read_bytes()
            if len(payload)<256 or len(payload)>2*1024*1024 or payload[:8]!=b'VOXYMESH':raise RuntimeError('invalid/oversized cooked payload')
            counts=struct.unpack_from('<12I',payload,16);gpu=counts[0]*72+counts[2]*4+counts[5]*64
            if counts[6]!=22 or counts[7]!=15 or not 15<=counts[4]<=48 or counts[10]!=8 or counts[8] or counts[9]:raise RuntimeError('hierarchy/clip/skin contract mismatch')
            if gpu>1024*1024:raise RuntimeError('human GPU payload exceeds1MiB')
            records.append(dict(lod=lod,asset_id=f'{identity}-lod{lod}',filename=filename,
                sha256=sha(pending/filename),bytes=len(payload),gpu_bytes=gpu,vertex_count=counts[0],index_count=counts[2],mesh_count=counts[7],node_count=counts[6],
                expanded_draws=counts[4],clips=counts[10],channels=counts[11],source_sha256=authored['glb_sha256'],cook_exit=result.returncode))
        manifest=dict(schema=1,profile='salvage-animated-rigid-v1',asset_id=identity,render_to_canonical=12,filename='human.vmesh',sha256=records[0]['sha256'],
            maximum_nodes=32,maximum_meshes=24,maximum_draws=48,clips=provenance['clips'],anchors=provenance['anchors'],source_sha256=records[0]['source_sha256'],
            provenance_sha256=sha(pending/'provenance.json'),cooker_sha256=sha(tool),cook_recipe_sha256=sha(Path(__file__)),lods=records)
        (pending/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        if output.exists() or output.is_symlink():raise RuntimeError('output appeared during cook')
        os.rename(pending,output)
    print(json.dumps(dict(output=str(output),manifest_sha256=sha(output/'manifest.json'),lods=records),indent=2))

if __name__=='__main__':main()
