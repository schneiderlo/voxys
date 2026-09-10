"""Stage a complete, reproducible pontoon candidate without publishing it.

Historical candidates and installed runtime selections remain unchanged.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def copy(source, target):
    if not source.is_file() or source.is_symlink():
        raise ValueError(f'Expected a regular source file: {source}')
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    if sha(source) != sha(target):
        raise ValueError(f'Copy differs: {source}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--converter', required=True, type=Path)
    parser.add_argument('--validator', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    candidate = args.candidate.resolve(strict=True)
    output = args.output.absolute()
    converter = args.converter.resolve(strict=True)
    validator = args.validator.resolve(strict=True)
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        parser.error('output must be new with an existing parent')
    authored = json.loads((candidate / 'provenance.json').read_text())
    review = json.loads((candidate / 'review-artifacts.json').read_text())
    # Check the actual original records before moving any payload into staging.
    for record in (authored, review):
        for name, item in record['outputs'].items():
            path = candidate / name
            if path.resolve().parent != candidate and candidate not in path.resolve().parents:
                raise ValueError('Candidate record escapes its directory')
            if not path.is_file() or path.is_symlink() or sha(path) != item['sha256'] or path.stat().st_size != item['bytes']:
                raise ValueError(f'Candidate input changed: {name}')
    recipes = ['author_pontoon.py', 'pontoon_parameters.py', 'pontoon.spec.json',
               'render_pontoon_review.py', 'pontoon_contact_sheet.py', 'cook_gameplay_asset.py']
    folder = root / 'tools/salvage_assets'
    for name, expected in [('author_pontoon.py', authored['generator_sha256']),
                           ('pontoon_parameters.py', authored['parameter_module_sha256']),
                           ('pontoon.spec.json', authored['input_spec_sha256'])]:
        if sha(folder / name) != expected:
            raise ValueError(f'Current authoring recipe differs from candidate: {name}')
    thumbnail = candidate / 'review/thumbnail.png'
    if struct.unpack('>II', thumbnail.read_bytes()[16:24]) != (512, 512):
        raise ValueError('Release thumbnail must be 512 square')
    output.mkdir()
    for path in sorted((candidate / 'source').rglob('*')):
        if path.is_file(): copy(path, output / 'source' / path.relative_to(candidate / 'source'))
    for path in sorted((candidate / 'preview').glob('*.png')):
        name = 'authoring-thumbnail-640.png' if path.name == 'thumbnail.png' else path.name
        copy(path, output / 'preview' / name)
    for source, target in [('review/thumbnail.png', 'preview/thumbnail.png'),
                           ('review/contact-sheet.png', 'preview/contact-sheet.png'),
                           ('review/contact-sheet.html', 'preview/contact-sheet.html'),
                           ('preview/turntable.webm', 'preview/turntable.webm'),
                           ('provenance.json', 'history/authoring.json'),
                           ('review-artifacts.json', 'history/review-artifacts.json'),
                           ('review/thumbnail-provenance.json', 'history/thumbnail.json'),
                           ('review/contact-sheet.json', 'history/contact-sheet.json'),
                           ('cooked/cook-manifest.json', 'history/previous-cook-manifest.json')]:
        copy(candidate / source, output / target)
    for name in recipes: copy(folder / name, output / 'recipes' / name)
    copy(Path(__file__).resolve(), output / 'recipes' / Path(__file__).name)
    command = [sys.executable, str(folder / 'cook_gameplay_asset.py'),
               '--sidecar', str(output / 'source/pontoon.gameplay.json'),
               '--sources', str(output / 'source'), '--output', str(output / 'cooked'),
               '--converter', str(converter), '--validator', str(validator)]
    with (output / 'history/cook.log').open('wb') as log:
        subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
    payloads = ['gameplay.json', 'lod-1.vmesh', 'lod-2.vmesh', 'lod-3.vmesh']
    for name in payloads:
        if sha(output / 'cooked' / name) != sha(candidate / 'cooked' / name):
            raise ValueError(f'Current cook changed the proven runtime payload: {name}')
    guide = '''# Pontoon v2 release candidate

This is a staged asset package, awaiting independent pipeline review. It is not
published, is not accepted cove art, and does not change installed game content.

The editable source, three GLBs, seven original maps, parameter specification,
sidecar, cooked VMESHs, 512-pixel thumbnail, contact sheet and six-second
turntable are included. The original 640-pixel thumbnail is retained under a
separate name. Preview images use Cycles/AgX; engine lighting is a separate
reference. No texture contains baked directional lighting or external imagery.

The part key is namespace 766f7879732d73616c766167652d7631, counter 3, version 2.
The original prototype version 1 remains distinct. Width/body height/length are
1/.96/4 m; engaged stacking advances .96 m; pegs extend .18 m. Placement uses
.02 m ticks, +Y up, -Z forward. The exporter-to-canonical proper rotation is 12,
applied once. Source LOD files 0/1/2 map to durable LOD IDs 1/2/3. The sidecar
contains authoritative socket, collision, buoyancy, mass and LOD metadata.

## Regeneration

Run from the repository using Blender 5.2.1 LTS build 9e2066aef7ef and its Python
modules. Use fresh output directories. Substitute this package's absolute path
for PACKAGE; these commands never overwrite this package.

```sh
blender --background --factory-startup --python-exit-code 1 --python PACKAGE/recipes/author_pontoon.py -- --spec PACKAGE/source/parameters.json --output-dir /tmp/new-pontoon --preview full
python3 PACKAGE/recipes/cook_gameplay_asset.py --sidecar /tmp/new-pontoon/source/pontoon.gameplay.json --sources /tmp/new-pontoon/source --output /tmp/new-pontoon/cooked --converter build-salvage-native/bin/gltf_vmesh_tool --validator build-salvage-native/bin/gameplay_sidecar_tool
blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python PACKAGE/recipes/render_pontoon_review.py -- --candidate /tmp/new-pontoon --output /tmp/new-pontoon/review
python3 PACKAGE/recipes/pontoon_contact_sheet.py --candidate /tmp/new-pontoon --output /tmp/new-pontoon/review/contact-sheet.html
ffmpeg -framerate 12 -i /tmp/new-pontoon/preview/turntable-frames/%03d.png -c:v libvpx-vp9 -crf 26 -b:v 0 /tmp/new-pontoon/preview/turntable.webm
```

Build the strict converter/sidecar validator with the repository Nix toolchain.
The cook manifest records exact executable hashes; an identical payload cooked
with a different executable has a different provenance manifest. Clean r05/r06
runs reproduced GLB, maps, sidecar, VMESH and decoded static preview pixels.
Blender/PNG container bytes can differ. Repeated turntable pixel equality was
not established. The contact-sheet PNG is a capture of the included HTML; it
is not an independently rendered view.

The full implementation plan and runtime review are in
GAME_IMPLEMENTATION_TODO.md and docs/validation/salvage/ASSET-04/ in the repository.
They include real native/browser material, socket, hierarchy, rotation, LOD,
sector and ownership evidence. History files preserve the original candidate's
records; their paths and output lists refer to that original candidate, whose
72 raw turntable frames are intentionally not duplicated here. This package's
own provenance.json inventories all files actually included here.

Before publication, independently review the complete source/runtime evidence,
confirm native/browser loading of the final manifest, and record acceptance.
Then publish one immutable v2 bundle and update explicit registry selections;
do not silently replace prototype v1, old saves or another accepted v2 bundle.
'''
    (output / 'README.md').write_text(guide)
    record = {'schema': 1, 'status': 'staged release candidate; independent pipeline acceptance pending; not published',
              'part': authored['part'], 'input_candidate': str(candidate),
              'input_authoring_sha256': sha(candidate / 'provenance.json'),
              'input_review_sha256': sha(candidate / 'review-artifacts.json'),
              'blender_version': authored['blender_version'], 'blender_build': authored['blender_build_hash'],
              'maps': authored['maps'], 'origin': authored['provenance'],
              'cook_command': command, 'converter_sha256': sha(converter), 'validator_sha256': sha(validator),
              'runtime_payloads_unchanged': {name: sha(output / 'cooked' / name) for name in payloads},
              'independent_review_complete': False, 'published': False,
              'files': {str(path.relative_to(output)): {'sha256': sha(path), 'bytes': path.stat().st_size}
                        for path in sorted(output.rglob('*')) if path.is_file()}}
    (output / 'provenance.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({'status': record['status'], 'files': len(record['files']),
                      'manifest_sha256': sha(output / 'cooked/cook-manifest.json')}))


if __name__ == '__main__':
    main()
