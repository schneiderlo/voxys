#!/usr/bin/env python3
"""Atomic publication of three explicit hash-bound resident character bundles."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
KEYS=('moss','rivet','lumen')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--tool',type=Path,required=True)
    p.add_argument('--source-dir',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
    a=p.parse_args();source=a.source_dir.resolve();output=a.output_dir.absolute();tool=a.tool.resolve()
    if output.exists() or output.is_symlink() or not output.parent.is_dir():p.error('new output directory required')
    summaries=[]
    with tempfile.TemporaryDirectory(prefix='.residents-cook-',dir=output.parent) as tmp:
        pending=Path(tmp)/'package';pending.mkdir()
        for key in KEYS:
            src=source/key;dst=pending/key;dst.mkdir();(dst/'source').mkdir()
            provenance=json.loads((src/'provenance.json').read_text());identity=f'voxys-adventure-resident-{key}-r01'
            if provenance.get('asset_id')!=identity or provenance.get('profile')!='salvage-animated-rigid-v1' or len(provenance.get('lods',[]))!=3:raise RuntimeError('resident identity/profile/source mismatch')
            shutil.copyfile(src/'provenance.json',dst/'provenance.json');records=[]
            for lod,filename in enumerate(('character.vmesh','lod1.vmesh','lod2.vmesh')):
                authored=provenance['lods'][lod]
                for extension in ('glb','blend'):
                    path=src/'source'/f'character-lod-{lod}.{extension}'
                    if path.is_symlink() or not path.is_file() or path.stat().st_size>16*1024*1024 or sha(path)!=authored[extension+'_sha256']:raise RuntimeError('source bounds/hash mismatch')
                    shutil.copyfile(path,dst/'source'/path.name)
                result=subprocess.run([str(tool),'--profile','salvage-animated-rigid-v1',str(dst/'source'/f'character-lod-{lod}.glb'),str(dst/filename)],capture_output=True,text=True,timeout=60)
                if result.returncode:raise RuntimeError(f'{key} LOD{lod}: strict cook refused: {result.stderr}')
                payload=(dst/filename).read_bytes()
                if not 256<=len(payload)<=2*1024*1024 or payload[:8]!=b'VOXYMESH':raise RuntimeError('invalid/oversized VMESH')
                c=struct.unpack_from('<12I',payload,16);gpu=c[0]*72+c[2]*4+c[5]*64
                if c[6]!=22 or c[7]!=15 or not 15<=c[4]<=48 or c[10]!=8 or c[8] or c[9] or gpu>1024*1024:raise RuntimeError('resident hierarchy/clip/GPU budget mismatch')
                records.append(dict(lod=lod,asset_id=identity+f'-lod{lod}',filename=filename,sha256=sha(dst/filename),bytes=len(payload),gpu_bytes=gpu,
                    vertex_count=c[0],index_count=c[2],mesh_count=c[7],node_count=c[6],expanded_draws=c[4],clips=c[10],channels=c[11],source_sha256=authored['glb_sha256'],cook_exit=result.returncode))
            manifest=dict(schema=1,profile='salvage-animated-rigid-v1',asset_id=identity,render_to_canonical=12,filename='character.vmesh',sha256=records[0]['sha256'],
                maximum_nodes=32,maximum_meshes=24,maximum_draws=48,clips=provenance['clips'],anchors=provenance['anchors'],source_sha256=records[0]['source_sha256'],
                provenance_sha256=sha(dst/'provenance.json'),cooker_sha256=sha(tool),cook_recipe_sha256=sha(Path(__file__)),lods=records)
            (dst/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
            summaries.append(dict(key=key,identity=identity,manifest_sha256=sha(dst/'manifest.json'),lods=records))
        if output.exists() or output.is_symlink():raise RuntimeError('output appeared during cook')
        os.rename(pending,output)
    print(json.dumps(dict(output=str(output),residents=summaries),indent=2))

if __name__=='__main__':main()
