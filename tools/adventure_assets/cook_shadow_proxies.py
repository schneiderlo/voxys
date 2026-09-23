#!/usr/bin/env python3
"""Cook and pin the shadow-only village/blacksmith geometry."""
import argparse, hashlib, json, struct, subprocess
from pathlib import Path


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--package',type=Path,required=True)
    parser.add_argument('--tool',type=Path,required=True)
    args = parser.parse_args()
    root = args.package.resolve()
    source = root/'source'
    provenance = json.loads((source/'provenance.json').read_text())
    glb = source/'shadow-proxies.glb'
    assert sha(glb) == provenance['glb_sha256']
    assert sha(source/'shadow-proxies.blend') == provenance['blend_sha256']
    mesh = root/'shadow-proxies.vmesh'
    subprocess.run([str(args.tool.resolve()),'--profile','salvage-rigid-v1',str(glb),str(mesh)],check=True)
    raw = mesh.read_bytes()
    assert raw[:8] == b'VOXYMESH'
    header = struct.unpack_from('<14I13Q',raw,8)
    version,flags,vertices,vertex_stride,indices,index_stride,submeshes,materials,nodes,meshes,*_ = header[:14]
    assert version == 1 and vertex_stride == 72 and index_stride in (2,4)
    assert nodes == meshes == submeshes == 16 and materials == 1
    assert indices//3 == sum(provenance['triangles']) and indices//3 <= 51000
    assert len(raw) == header[-1]
    report = {'asset_id':provenance['asset_id'],'bytes':len(raw),'sha256':sha(mesh),
              'meshes':meshes,'triangles':indices//3,'per_mesh_triangles':provenance['triangles'],
              'provenance_sha256':sha(source/'provenance.json'),'recipe_sha256':sha(__file__)}
    (root/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
