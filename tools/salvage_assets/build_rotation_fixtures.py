"""Generate bounded installed inspection registries for all 24 proper rotations.

This creates scene data, not cooked geometry. The actual C++ loader must admit
every generated connection before the scene can render. Outputs are exclusive.
"""
import argparse
import hashlib
import json
from pathlib import Path


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def rotations():
    # DATA-01's frozen serialized ordering: columns are local X,Y,Z axes.
    axes = ((1,0,0),(0,1,0),(0,0,1),(-1,0,0),(0,-1,0),(0,0,-1))
    return [(x,y,cross(x,y)) for x in axes for y in axes if sum(a*b for a,b in zip(x,y)) == 0]


def rotate(columns, value):
    return [sum(columns[col][row]*value[col] for col in range(3)) for row in range(3)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output_dir.absolute()
    output.mkdir(parents=True, exist_ok=False)
    source = root / "data/salvage/fixture-pontoon-v2.json"
    source_bytes = source.read_bytes()
    bundle = json.loads(source_bytes)["bundles"][0]
    matrices = rotations()
    assert len(matrices) == 24 and matrices[0] == ((1,0,0),(0,1,0),(0,0,1))
    manifest = {"schema":1, "purpose":"Static all-rotation assembly inspection; no accepted player builds or live physics",
                "source_registry_sha256":hashlib.sha256(source_bytes).hexdigest(),
                "generator_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                "rotation_columns":matrices, "groups":[]}
    for group, first in [("a",0),("b",12)]:
        registry = {"schema":2, "bundles":[bundle], "prototypes":[], "placements":[], "connections":[],
                    "camera":{"eye":[20,18,-25],"target":[0,3.3,0]}}
        pair_records = []
        views = [{"name":"overview", **registry["camera"], "sun":[.4,.8,-.4]}]
        for offset in range(12):
            rotation = first + offset
            center = [-450+300*(offset%4),150,-300+300*(offset//4)]
            rise = rotate(matrices[rotation], (0,48,0))
            placements = [center,[a+b for a,b in zip(center,rise)]]
            for position in placements:
                registry["placements"].append({"bundle":0,"translation_ticks":position,"rotation":rotation})
            registry["connections"].append({"a":{"placement":offset*2,"socket":"1"},
                                            "b":{"placement":offset*2+1,"socket":"2"}})
            # The view target is the physical center of the two-part stack.
            # Naming uses letters because the existing screenshot runner's
            # bounded name profile is intentionally [a-z-]+.
            name = "pair-"+chr(ord('a')+rotation)
            target = [(placements[0][axis]+placements[1][axis])/100 for axis in range(3)]
            eye = [target[0]+3.2,target[1]+2.4,target[2]-4.8]
            views.append({"name":name,"eye":eye,"target":target,"sun":[.4,.8,-.4]})
            pair_records.append({"rotation":rotation,"view":name,"placements":[offset*2,offset*2+1],
                                 "base_ticks":center,"second_ticks":placements[1]})
        filename = f"fixture-pontoon-rotations-{group}.json"
        data = (json.dumps(registry,indent=2)+"\n").encode()
        assert len(data)<=65536 and len(registry['placements'])==24
        (output/filename).write_bytes(data)
        recipe = {"schema":1,"registry":"data/salvage/"+filename,"physical_viewport":[1920,1080],"fov_degrees":60,
                  "inspection_expectations":{"maximum_model_draws":24,"guide_boxes":[0,26,360]},"views":views}
        (output/f"views-{group}.json").write_text(json.dumps(recipe,indent=2)+"\n")
        manifest["groups"].append({"group":group,"registry":filename,"registry_sha256":hashlib.sha256(data).hexdigest(),
                                   "parts":24,"connections":12,"pairs":pair_records})
    (output/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    print(json.dumps({"status":"generated; C++ admission and actual rendering required","output":str(output),"rotations":24}))


if __name__ == "__main__":
    main()
