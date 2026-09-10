"""Lay out unchanged authored render files as a self-contained HTML review sheet.

The PNG sheet is a browser screenshot of this layout, not synthesized artwork.
Keep source pixels and labels independently inspectable in the HTML artifact.
"""
import argparse
import base64
import hashlib
import html
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    candidate = args.candidate.resolve(strict=True)
    output = args.output.absolute()
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        parser.error('output HTML must be new and its parent must exist')
    views = [('front', 'Front · coral bow'), ('rear', 'Rear'), ('left', 'Left'),
             ('right', 'Right'), ('top', 'Top'), ('bottom', 'Bottom'),
             ('socket-wells', 'Socket wells'), ('socket-pegs', 'Keyed pegs'),
             ('engaged', 'Engaged · 0.96 m centers')]
    cards = []
    inputs = []
    for name, label in views:
        source = candidate / 'preview' / (name + '.png')
        payload = source.read_bytes()
        if len(payload) > 4 * 1024 * 1024 or not payload.startswith(b'\x89PNG\r\n\x1a\n'):
            raise RuntimeError('invalid or oversized preview')
        inputs.append({'file': str(source), 'sha256': hashlib.sha256(payload).hexdigest(),
                       'bytes': len(payload), 'label': label})
        encoded = base64.b64encode(payload).decode('ascii')
        cards.append(f'<figure><img src="data:image/png;base64,{encoded}" alt="{html.escape(label)}">'
                     f'<figcaption>{html.escape(label)}</figcaption></figure>')
    document = '''<!doctype html><meta charset="utf-8"><title>Pontoon — technical review</title>
<style>
*{box-sizing:border-box}body{margin:0;padding:28px;background:#16272b;color:#e6eee9;
font:20px/1.3 sans-serif;width:2048px}h1{margin:0;font-size:34px;font-weight:600}
p{margin:8px 0 22px;color:#b5cac6}main{display:grid;grid-template-columns:repeat(3,1fr);gap:18px}
figure{margin:0;background:#243a3e;border-radius:8px;overflow:hidden}
img{display:block;width:100%;height:520px;object-fit:contain;background:#bac5c7}
figcaption{padding:10px 16px;font-size:22px}
</style><h1>Pontoon v2 · authored candidate</h1>
<p>Offline Blender views · source renders unchanged · construction fit and engine appearance require separate evidence</p>
<main>''' + ''.join(cards) + '</main>'
    output.write_text(document)
    record = {'schema': 1, 'kind': 'HTML layout of unchanged authored images', 'inputs': inputs,
              'script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'html_sha256': hashlib.sha256(output.read_bytes()).hexdigest(),
              'viewport': [2048, 1870], 'candidate': str(candidate)}
    output.with_suffix('.json').write_text(json.dumps(record, indent=2) + '\n')


if __name__ == '__main__':
    main()
