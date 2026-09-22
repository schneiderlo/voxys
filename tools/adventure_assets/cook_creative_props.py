#!/usr/bin/env python3
"""Strict cook/packaging for the original six-mesh creative scenery kit."""
import argparse,json,hashlib,math,os,shutil,struct,subprocess,tempfile
from pathlib import Path
NAMES=('00_broadleaf','01_pine','02_flowers','03_rocks','04_bench','05_crate')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def inspect(path,props):
    raw=path.read_bytes();assert raw[:8]==b'VOXYMESH' and 256<=len(raw)<=3*1024*1024
    h=struct.unpack_from('<14I13Q',raw,8)
    version,flags,nv,vs,ni,stride,ns,nm,nn,nmesh,nskin,nj,na,nac=h[:14]
    vo,io,so,mo,images,no,skins,anims,channels,channeldata,names,namesize,size=h[14:]
    assert version==1 and vs==72 and stride in (2,4) and nn==nmesh==6
    assert nm<=8 and ns<=18 and nv<=24000 and ni<=120000 and size==len(raw)
    assert not any((nskin,nj,na,nac)) and images==no
    vertices=[struct.unpack_from('<3f',raw,vo+i*vs) for i in range(nv)]
    assert all(math.isfinite(v) for p in vertices for v in p)
    for i in range(nv):
        n=struct.unpack_from('<3f',raw,vo+i*vs+12);assert abs(sum(v*v for v in n)-1)<2.e-4
    indices=struct.unpack_from('<'+('H' if stride==2 else 'I')*ni,raw,io);assert all(i<nv for i in indices)
    # Flat exported caps/panels must keep plane normals: smooth normals on
    # these triangles cause the warped glossy patches reported in-game.
    planar=0
    for start in range(0,ni,3):
        ids=indices[start:start+3];a,b,c=[vertices[j] for j in ids]
        u=[b[k]-a[k] for k in range(3)];v=[c[k]-a[k] for k in range(3)]
        n=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]);length=math.sqrt(sum(x*x for x in n))
        if length>1.e-8 and abs(n[1])/length>.99999:
            planar+=1
            for j in ids:
                ny=struct.unpack_from('<f',raw,vo+j*vs+16)[0]
                assert abs(ny)>.9999,('bent planar normal',start,j,ny)
    subs=[struct.unpack_from('<4I',raw,so+i*16) for i in range(ns)]
    for i in range(6):
        node=struct.unpack_from('<10fiIiI',raw,no+i*64)
        assert node[:10]==(0.,0.,0.,0.,0.,0.,1.,1.,1.,1.) and node[10:13]==(-1,i,-1)
        start=names+node[13];assert raw[start:raw.index(0,start)].decode()==NAMES[i]
        used=[indices[j] for begin,count,material,mesh in subs if mesh==i for j in range(begin//stride,begin//stride+count)]
        assert used
        lo=props[i]['bounds']['minimum'];hi=props[i]['bounds']['maximum']
        assert all(all(lo[a]-.001<=vertices[j][a]<=hi[a]+.001 for a in range(3)) for j in used)
    return dict(bytes=len(raw),sha256=sha(path),vertices=nv,triangles=ni//3,draws=ns,materials=nm,requested_gpu_bytes=nv*72+ni*4+nm*64,meshes=6,nodes=6,flat_triangles_verified=planar)
def main():
    p=argparse.ArgumentParser();p.add_argument('--source-dir',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);p.add_argument('--tool',type=Path,required=True);a=p.parse_args()
    source=a.source_dir.resolve();out=a.output_dir.absolute();tool=a.tool.resolve(strict=True)
    assert not out.exists() and out.parent.is_dir()
    prov=json.loads((source/'provenance.json').read_text());assert prov['asset_id']=='voxys-creative-props-r01' and prov['render_to_canonical']==0
    with tempfile.TemporaryDirectory(prefix='.creative-props-',dir=out.parent) as temp:
        staged=Path(temp)/'package';(staged/'source').mkdir(parents=True)
        shutil.copy2(source/'provenance.json',staged/'provenance.json')
        for extension in ('blend','glb'):
            src=source/f'creative-props.{extension}';assert src.stat().st_size<16*1024*1024 and sha(src)==prov[extension+'_sha256']
            shutil.copy2(src,staged/'source'/src.name)
        subprocess.run([str(tool),'--profile','salvage-rigid-v1',str(staged/'source/creative-props.glb'),str(staged/'creative-props.vmesh')],check=True,timeout=60)
        report=inspect(staged/'creative-props.vmesh',prov['props'])
        manifest=dict(schema=1,asset_id=prov['asset_id'],profile='salvage-rigid-v1',render_to_canonical=0,mesh=report,cooker_sha256=sha(tool),recipe_sha256=sha(__file__),provenance_sha256=sha(staged/'provenance.json'))
        (staged/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');os.rename(staged,out)
    print(json.dumps(manifest))
if __name__=='__main__':main()
