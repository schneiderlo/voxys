#!/usr/bin/env python3
"""Actual exported resident geometry/clip checks using the established CPU sampler."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
ROOT=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').is_file())
sys.path.insert(0,str(Path(__file__).resolve().parent))
from check_human_adventurer import inspect,check_grips
KEYS=('moss','rivet','lumen')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('package',type=Path);a=p.parse_args();residents=[]
    for index,key in enumerate(KEYS,1):
        directory=a.package/key;provenance=json.loads((directory/'provenance.json').read_text())
        assert provenance['asset_id']==f'voxys-adventure-resident-{key}-r01' and provenance['resident_id']==index
        for path,expected in provenance['base_player_files'].items():assert sha(ROOT/path)==expected,'frozen player package changed'
        reports=[]
        for lod in range(3):
            path=directory/'source'/f'character-lod-{lod}.glb'
            assert sha(path)==provenance['lods'][lod]['glb_sha256']
            r=inspect(path)
            assert r['clips']['idle']['maximum'][1]<=1.700001,'idle exceeds standing height'
            for clip in ('walk','land','fall'):assert r['clips'][clip]['minimum'][1]>=-1e-5,clip+' foot penetrates ground'
            r['source_sha256']=sha(path);r['open_toy_grips']=check_grips(path)
            r['doorway_margin_m']=dict(width=1.36-r['clips']['walk']['width'],height=2.24-r['clips']['walk']['maximum'][1])
            reports.append(r)
        residents.append(dict(key=key,id=index,silhouette=provenance['silhouette'],lods=reports))
    print(json.dumps(dict(schema=1,asset='voxys-adventure-residents-r01',checker_sha256=sha(Path(__file__)),
        shared_sampler_sha256=sha(ROOT/'tools/salvage_assets/check_cove_robot.py'),
        player_checker_sha256=sha(ROOT/'tools/adventure_assets/check_human_adventurer.py'),
        frozen_player_files_unchanged=True,residents=residents),indent=2))

if __name__=='__main__':main()
