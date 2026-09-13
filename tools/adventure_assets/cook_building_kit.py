#!/usr/bin/env python3
"""Strict GLB→VMESH cook for the original home kit; refuses existing outputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from check_building_kit import check


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source-dir', 'output-dir', 'tool'):
        parser.add_argument('--'+name, type=Path, required=True)
    a = parser.parse_args()
    out, source, tool = a.output_dir.absolute(), a.source_dir.resolve(), a.tool.resolve(strict=True)
    if out.exists() or out.is_symlink() or not out.parent.is_dir():
        parser.error('new output directory with existing parent required')
    prov = json.loads((source/'provenance.json').read_text())
    assert prov['render_to_canonical'] == 0 and len(prov['lods']) == 2
    with tempfile.TemporaryDirectory(prefix='.building-kit-', dir=out.parent) as tmp:
        package = Path(tmp)/'package'
        (package/'source').mkdir(parents=True)
        shutil.copy2(source/'provenance.json', package/'provenance.json')
        logs = []
        for index, record in enumerate(prov['lods']):
            assert record['lod'] == index
            for kind in ('blend', 'glb'):
                src = source/f'building-kit-lod{index}.{kind}'
                assert src.is_file() and not src.is_symlink() and src.stat().st_size <= 16*1024*1024
                assert sha(src) == record[kind+'_sha256']
                shutil.copy2(src, package/'source'/src.name)
            command = [str(tool), '--profile', 'salvage-rigid-v1',
                       str(package/'source'/f'building-kit-lod{index}.glb'),
                       str(package/f'building-kit-lod{index}.vmesh')]
            run = subprocess.run(command, text=True, capture_output=True, timeout=60)
            assert run.returncode == 0, run.stderr
            logs.append(dict(lod=index, exit=run.returncode, stdout=run.stdout, stderr=run.stderr))
        report = check(package)
        (package/'geometry-check.json').write_text(json.dumps(report, indent=2)+'\n')
        manifest = dict(schema=1, asset_id=prov['asset_id'], render_to_canonical=0,
                        catalog_sha256=prov['catalog_sha256'], lods=[dict(lod=r['lod'],
                          filename=f'building-kit-lod{r["lod"]}.vmesh', bytes=r['bytes'],
                          sha256=r['sha256'], requested_gpu_bytes=r['requested_gpu_bytes']) for r in report['lods']],
                        cooker_sha256=sha(tool), recipe_sha256=sha(__file__),
                        checker_sha256=sha(Path(__file__).with_name('check_building_kit.py')),
                        provenance_sha256=sha(package/'provenance.json'), cook_results=logs)
        (package/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
        if out.exists() or out.is_symlink():
            raise RuntimeError('output appeared during cook')
        os.rename(package, out)
    print(json.dumps(dict(status='passed', output=str(out), manifest_sha256=sha(out/'manifest.json'))))


if __name__ == '__main__':
    main()
