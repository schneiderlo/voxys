#!/usr/bin/env python3
"""Package an already-built browser executable with the matching web interface.

Creates a new directory; never replaces a running preview or player saves.
"""
import argparse,hashlib,json,shutil
from pathlib import Path

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary-directory',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--profile',choices=('adventure','free-build'),default='adventure')
    args=parser.parse_args()
    root=Path(__file__).resolve().parent.parent
    binaries=[args.binary_directory/name for name in ('voxy_wasm.js','voxy_wasm.wasm','voxy_wasm.data')]
    for path in binaries:
        if not path.is_file():raise SystemExit(f'Missing build output: {path}')
    glue=binaries[0].read_text()
    required=('_adventure_action','_adventure_preferences_action','_get_adventure_state_json','_adventure_stage',
              '_adventure_snapshot_hex','_adventure_validate_hex',
              '_adventure_save_completed','voxyAdventureSaveRequested')
    missing=[name for name in required if name not in glue]
    if missing:raise SystemExit('Adventure host exports were lost during optimization: '+', '.join(missing))
    if args.output.exists():raise SystemExit('Use a new output directory; existing previews are preserved.')
    # The checked-in browser bundle must match its reviewed component sources.
    ui_manifest=root/'web/build_ui_manifest.json'
    if not ui_manifest.is_file():raise SystemExit('Build the UI first: npm --prefix ui ci && npm --prefix ui run build')
    ui=json.loads(ui_manifest.read_text())
    for path,expected in {**ui['sources'],**ui['outputs']}.items():
        source=root/path
        if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest()!=expected:
            raise SystemExit('Stale building UI: '+path+'. Run npm --prefix ui run build.')
    shutil.copytree(root/'web',args.output)
    for path in binaries:shutil.copy2(path,args.output/path.name)
    digest=hashlib.sha256()
    for path in sorted(args.output.iterdir()):
        if path.is_file():digest.update(path.name.encode());digest.update(path.read_bytes())
    build_id=args.profile+'-'+digest.hexdigest()[:16]
    for path in args.output.iterdir():
        if path.suffix in ('.html','.js','.css'):
            text=path.read_text()
            if '__VOXY_BUILD_ID__' in text:path.write_text(text.replace('__VOXY_BUILD_ID__',build_id))
    manifest={'buildId':build_id,'files':{path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(args.output.iterdir()) if path.is_file()}}
    (args.output/'package.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps({'buildId':build_id,'directory':str(args.output.resolve()),'files':len(manifest['files'])}))
if __name__=='__main__':main()
