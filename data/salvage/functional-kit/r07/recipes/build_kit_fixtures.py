"""Build bounded static inspection registries from exact cooked candidate manifests."""
import argparse
import json
from pathlib import Path

import functional_kit as kit


def registries(candidate, root):
    def bundle(name):
        if name == 'pontoon':
            return json.loads((root/'fixture-pontoon-v2.json').read_text())['bundles'][0]
        cooked = candidate/name/'cooked'
        part = json.loads((cooked/'gameplay.json').read_text())['part']['key']
        return dict(directory=str(cooked.relative_to(root)),part=part,
                    manifest_sha256=kit.pontoon.sha256(cooked/'cook-manifest.json'),
                    lod_limits=[dict(id=str(i+1),vertices=v,triangles=t,texture_dimension=s)
                                for i,(t,v,s,_) in enumerate(kit.LOD_LIMITS)])

    names=['pontoon','beam','plate','engine','propeller','helm','winch','cradle']
    def placement(name, point):
        return dict(bundle=names.index(name),translation_ticks=list(point),rotation=0)
    def connection(a,sa,b,sb):
        return dict(a=dict(placement=a,socket=str(sa)),b=dict(placement=b,socket=str(sb)))
    result={}
    for width,label in ((25,'narrow'),(75,'broad')):
        placements=[placement('pontoon',(-width,0,0)),placement('pontoon',(width,0,0)),
                    placement('beam',(0,32,-50)),placement('beam',(0,32,50)),
                    placement('plate',(0,48,-50)),placement('plate',(0,48,50)),
                    placement('engine',(25,80,50)),placement('propeller',(25,16,136)),
                    placement('helm',(-75,80,50)),placement('winch',(-75,104,-50)),
                    placement('cradle',(25,72,-50))]
        left,right=(103,105) if width==25 else (101,107)
        links=[connection(0,101,2,left),connection(1,101,2,right),
               connection(0,103,3,left),connection(1,103,3,right)]
        for beam,plate in ((2,4),(3,5)):
            links += [connection(beam,100+i*2,plate,101+i*2) for i in range(4)]
        links += [connection(5,104,6,2),connection(6,10,7,10),connection(5,100,8,2),
                  connection(4,100,9,2),connection(4,104,10,2)]
        result[f'fixture-kit-{label}.json']=dict(schema=2,bundles=[bundle(n) for n in names],
            prototypes=[],placements=placements,connections=links,
            camera=dict(eye=[7,5,-8],target=[0,.7,.2]))
    cargo=['cradle','generator','crate','winch']
    result['fixture-kit-cargo.json']=dict(schema=1,bundles=[bundle(n) for n in cargo],
        placements=[dict(bundle=0,translation_ticks=[0,0,0],rotation=0),
                    dict(bundle=1,translation_ticks=[0,48,0],rotation=0),
                    dict(bundle=2,translation_ticks=[160,32,0],rotation=0),
                    dict(bundle=3,translation_ticks=[-125,40,0],rotation=0)],
        camera=dict(eye=[7,4,-8],target=[.4,.5,0]))
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate',type=Path,required=True)
    parser.add_argument('--root',type=Path,default=Path('data/salvage'))
    args=parser.parse_args()
    outputs=registries(args.candidate.resolve(),args.root.resolve())
    for name in outputs:
        if (args.root/name).exists():
            parser.error(f'{name} exists; review and explicitly move the previous registry first')
    for name,value in outputs.items():
        (args.root/name).write_text(json.dumps(value,indent=2)+'\n')


if __name__=='__main__':
    main()
