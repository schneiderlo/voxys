#!/usr/bin/env python3
"""Strict, bounded cook of original village props; refuses existing output paths."""
import argparse,hashlib,json,math,os,shutil,struct,subprocess,tempfile
from pathlib import Path

SIZES=((4.6,1.28,4.6),(.9,.7,.9),(2.6,4.2,2.6),(.64,.08,.64))
NAMES=('00_roof','01_planter','02_tree','03_path_tile')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def inspect(path):
    raw=path.read_bytes();assert raw[:8]==b'VOXYMESH' and 256<=len(raw)<=2*1024*1024
    h=struct.unpack_from('<14I13Q',raw,8)
    version,flags,nv,vs,ni,stride,ns,nm,nn,nmesh,nskin,nj,na,nac=h[:14]
    vo,io,so,mo,images,no,skins,anims,channels,channeldata,names,namesize,size=h[14:]
    assert version==1 and vs==72 and stride in (2,4) and nn==nmesh==4
    assert nm<=8 and ns<=16 and nv<=18000 and ni<=42000 and size==len(raw)
    assert not any((nskin,nj,na,nac)) and images==no
    vertices=[struct.unpack_from('<3f',raw,vo+i*vs) for i in range(nv)]
    assert all(math.isfinite(v) for p in vertices for v in p)
    for i in range(nv):
        n=struct.unpack_from('<3f',raw,vo+i*vs+12);assert abs(sum(v*v for v in n)-1)<2.e-4
    indices=struct.unpack_from('<'+('H' if stride==2 else 'I')*ni,raw,io);assert all(i<nv for i in indices)
    subs=[struct.unpack_from('<4I',raw,so+i*16) for i in range(ns)]
    for i in range(4):
        node=struct.unpack_from('<10fiIiI',raw,no+i*64)
        assert node[:10]==(0.,0.,0.,0.,0.,0.,1.,1.,1.,1.) and node[10:13]==(-1,i,-1)
        start=names+node[13];assert raw[start:raw.index(0,start)].decode()==NAMES[i]
        used=[indices[j] for begin,count,material,mesh in subs if mesh==i for j in range(begin//stride,begin//stride+count)]
        assert used
        for j in used:
            assert all((-SIZES[i][a]/2 if a!=1 else 0)-.011<=vertices[j][a]<=(SIZES[i][a]/2 if a!=1 else SIZES[i][a])+.011 for a in range(3))
    return dict(bytes=len(raw),sha256=sha(path),vertices=nv,indices=ni,triangles=ni//3,draws=ns,materials=nm,
                requested_gpu_bytes=nv*72+ni*4+nm*64,meshes=4,nodes=4,canonical_basis=0)
def main():
    p=argparse.ArgumentParser();p.add_argument('--source-dir',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);p.add_argument('--tool',type=Path,required=True);a=p.parse_args()
    source=a.source_dir.resolve();out=a.output_dir.absolute();tool=a.tool.resolve(strict=True)
    assert not out.exists() and out.parent.is_dir()
    prov=json.loads((source/'provenance.json').read_text());assert prov['asset_id']=='voxys-adventure-village-props-r01' and prov['render_to_canonical']==0
    with tempfile.TemporaryDirectory(prefix='.village-props-',dir=out.parent) as temp:
        staged=Path(temp)/'package';(staged/'source').mkdir(parents=True)
        shutil.copy2(source/'provenance.json',staged/'provenance.json');records=[]
        for lod in (0,1):
            for extension in ('blend','glb'):
                src=source/f'village-props-lod{lod}.{extension}';assert src.stat().st_size<16*1024*1024 and sha(src)==prov['lods'][lod][extension+'_sha256']
                shutil.copy2(src,staged/'source'/src.name)
            filename=f'village-props-lod{lod}.vmesh'
            result=subprocess.run([str(tool),'--profile','salvage-rigid-v1',str(staged/'source'/f'village-props-lod{lod}.glb'),str(staged/filename)],capture_output=True,text=True,timeout=60)
            assert result.returncode==0,result.stderr
            records.append(dict(lod=lod,filename=filename,**inspect(staged/filename)))
        manifest=dict(schema=1,asset_id=prov['asset_id'],profile='salvage-rigid-v1',render_to_canonical=0,
                      lods=records,cooker_sha256=sha(tool),recipe_sha256=sha(__file__),provenance_sha256=sha(staged/'provenance.json'))
        (staged/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        os.rename(staged,out)
    print(json.dumps(manifest))
if __name__=='__main__':main()
