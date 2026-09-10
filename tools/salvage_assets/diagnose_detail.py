#!/usr/bin/env python3
"""Create isolated helm map ablations; preserves every GLB binary byte.

These are diagnostic inputs, never replacement art. The standard strict cook
and actual runtime fixture are still required. Run from the repository root.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct


def sha(data):
    return hashlib.sha256(data).hexdigest()


def ablate(data, mode):
    assert struct.unpack_from('<4sII',data)==(b'glTF',2,len(data))
    size,kind=struct.unpack_from('<II',data,12);assert kind==0x4e4f534a
    document=json.loads(data[20:20+size]);binary_chunks=data[20+size:]
    old=copy.deepcopy(document)
    roughness=dict(cream=.34,teal=.40,coral=.36,slate=.73,steel=.30,rubber=.78)
    for material in document['materials']:
        name=material['name'].removeprefix('salvage_metric_').split('_lod')[0]
        if mode in ('roughness-only','factors-only'):
            material.pop('normalTexture',None)
        if mode in ('normal-only','factors-only'):
            pbr=material['pbrMetallicRoughness']
            pbr.pop('metallicRoughnessTexture',None);pbr['roughnessFactor']=roughness[name]
    assert {k:v for k,v in document.items() if k!='materials'}=={k:v for k,v in old.items() if k!='materials'}
    encoded=json.dumps(document,separators=(',',':')).encode();encoded+=b' '*((-len(encoded))%4)
    result=struct.pack('<4sII',b'glTF',2,20+len(encoded)+len(binary_chunks))
    result+=struct.pack('<II',len(encoded),0x4e4f534a)+encoded+binary_chunks
    return result,sha(binary_chunks)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=False)
    baseline=Path('data/salvage/material-calibration/r04/kit/helm/source')
    report={'status':'diagnostic candidates only; cook and runtime checks pending','baseline':str(baseline),'variants':{}}
    for mode in ('normal-only','roughness-only','factors-only'):
        source=args.output/mode/'source';source.mkdir(parents=True)
        metadata=json.loads((baseline/'helm.gameplay.json').read_text());rows=[]
        for lod in metadata['lods']:
            name=lod['source']['file'];data=(baseline/name).read_bytes()
            candidate,binary_sha=ablate(data,mode);(source/name).write_bytes(candidate)
            lod['source'].update(sha256=sha(candidate),bytes=len(candidate))
            rows.append({'file':name,'baseline_sha256':sha(data),'candidate_sha256':sha(candidate),
                         'unchanged_binary_chunks_sha256':binary_sha})
        (source/'helm.gameplay.json').write_text(json.dumps(metadata,indent=2)+'\n')
        report['variants'][mode]=rows
    (args.output/'provenance.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':
    main()
