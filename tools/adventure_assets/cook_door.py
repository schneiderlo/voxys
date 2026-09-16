#!/usr/bin/env python3
"""Cook the original door add-on with the existing strict rigid GLB importer."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from check_door import check, sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source-dir','output-dir','tool'):
        parser.add_argument('--'+name,type=Path,required=True)
    args = parser.parse_args()
    source,output,tool = args.source_dir.resolve(),args.output_dir.absolute(),args.tool.resolve(strict=True)
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        parser.error('requires a new output directory with an existing parent')
    provenance = json.loads((source/'provenance.json').read_text())
    assert provenance['asset_id'] == 'voxys-adventure-hinged-door-r01'
    assert provenance['profile'] == 'salvage-rigid-v1' and provenance['render_to_canonical'] == 0
    assert len(provenance['lods']) == 2
    with tempfile.TemporaryDirectory(prefix='.door-cook-',dir=output.parent) as scratch:
        package = Path(scratch)/'package';(package/'source').mkdir(parents=True)
        shutil.copy2(source/'provenance.json',package/'provenance.json')
        logs = []
        for lod,record in enumerate(provenance['lods']):
            assert record['lod'] == lod
            for suffix in ('blend','glb'):
                path = source/f'door-leaf-lod{lod}.{suffix}'
                assert not path.is_symlink() and path.stat().st_size <= 8*1024*1024
                assert sha(path) == record[suffix+'_sha256']
                shutil.copy2(path,package/'source'/path.name)
            command = [str(tool),'--profile','salvage-rigid-v1',str(package/'source'/f'door-leaf-lod{lod}.glb'),str(package/f'door-leaf-lod{lod}.vmesh')]
            run = subprocess.run(command,capture_output=True,text=True,timeout=60)
            if run.returncode:raise RuntimeError(run.stderr)
            logs.append(dict(lod=lod,exit=run.returncode,stdout=run.stdout,stderr=run.stderr))
        report = check(package)
        (package/'geometry-check.json').write_text(json.dumps(report,indent=2)+'\n')
        manifest = dict(schema=1,asset_id=provenance['asset_id'],profile=provenance['profile'],render_to_canonical=0,
            catalog_sha256=report['catalog_sha256'],frame_sha256=report['frame_sha256'],frame_mesh_index=3,leaf_mesh_index=0,
            hinge_ticks=report['hinge_ticks'],open_yaw_degrees=-90,
            lods=[dict(lod=r['lod'],filename=f'door-leaf-lod{r["lod"]}.vmesh',bytes=r['bytes'],sha256=r['sha256'],requested_gpu_bytes=r['requested_gpu_bytes']) for r in report['lods']],
            cooker_sha256=sha(tool),recipe_sha256=sha(__file__),checker_sha256=sha(Path(__file__).with_name('check_door.py')),
            provenance_sha256=sha(package/'provenance.json'),cook_results=logs)
        (package/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        if output.exists() or output.is_symlink():raise RuntimeError('output appeared during cook')
        os.rename(package,output)
    print(json.dumps(dict(status='passed',output=str(output),manifest_sha256=sha(output/'manifest.json'))))


if __name__ == '__main__':
    main()
