#!/usr/bin/env python3
"""Load a copied legacy expedition, save v4 through F10, and restart it.

Uses only owned child windows and the public save controls. Source archives
remain byte-identical. No game-state injection, screenshots or source edits.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time

from validate_native_cove_delivery import Controls
from validate_native_cove_saves import archive


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--source-slot', type=Path, required=True)
    parser.add_argument('--storage-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output = args.output.resolve(); args.output.mkdir(parents=True, exist_ok=False)
    args.storage_root = args.storage_root.resolve(); args.storage_root.mkdir(parents=True, exist_ok=False)
    original, source = archive(args.source_slot)
    assert original['schema'] in (1, 2, 3), 'This journey requires an actual legacy archive'
    world = original['world']; slot = args.storage_root / world; slot.mkdir()
    for name in ('current', 'mirror'):
        shutil.copy2(args.source_slot / name, slot / name)
    report = {'status': 'running', 'kind': 'copied legacy archive, actual native load, F10 v4 save, process restart',
              'source': str(args.source_slot), 'original': original,
              'binarySha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(), 'stages': []}
    x = Controls(); child = stream = None; index = 0

    def read():
        p = args.output / f'observation-{index}' / 'state.json'
        return json.loads(p.read_text()) if p.exists() else {}

    def wait(predicate, label):
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            if child.poll() is not None:
                raise RuntimeError(f'Native child exited {child.returncode}: {label}')
            state = read(); assert not state.get('failed'), state
            if predicate(state): return state
            time.sleep(.04)
        raise RuntimeError(label + ': ' + json.dumps(read()))

    def record(name, state):
        report['stages'].append({'name': name, 'state': state})
        (args.output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')

    def start():
        nonlocal child, stream, index
        index += 1; stream = (args.output / f'process-{index}.log').open('w')
        child = subprocess.Popen([str(args.binary.resolve()), '--config', 'salvage_cove.cfg', '--uncapped',
            '--expedition-root', str(args.storage_root), '--expedition-world', world,
            '--width', '960', '--height', '540', '--expedition-observe', str(args.output / f'observation-{index}')],
            stdout=stream, stderr=subprocess.STDOUT, env={**os.environ, 'VOXY_WINDOW_BACKEND': 'x11'})
        state = wait(lambda s: s.get('restore', {}).get('phase') == 'ready'
                     and s.get('pause', {}).get('phase') == 'paused', 'restored paused expedition')
        assert state['terrainSurface'] == 'lego' and state['boat']['rootCount'] == 1
        assert state['boat']['joinedTick'] == state['pause']['tick']
        assert state['boat']['roots'][0]['observedTick'] == state['pause']['tick']
        return state

    def stop():
        nonlocal child, stream
        if child and child.poll() is None:
            child.send_signal(signal.SIGTERM); child.wait(timeout=15)
        if stream: stream.close()
        child = stream = None

    try:
        loaded = start(); record('legacy-loaded', loaded)
        assert int(loaded['pause']['tick']) == original['tick'] + 1
        aboard = bool(source[317]); assert loaded['player']['onBoat'] == aboard
        assert loaded['player']['rootKey'] == (loaded['boat']['roots'][0]['key'] if aboard else '0')
        before, _ = archive(slot)
        window = x.own_window(child.pid); assert window
        x.key(window, 'F10')
        wait(lambda _: 'Expedition saved' in x.title(window), 'manual save acknowledged')
        upgraded, payload = archive(slot)
        assert upgraded['generation'] > before['generation'] and upgraded['schema'] == 4
        assert upgraded['playerRoot'] == loaded['player']['rootKey']
        assert upgraded['controlPart'] == loaded['boat']['controlPart']
        assert upgraded['rootKeys'] == [r['key'] for r in loaded['boat']['roots']]
        assert str(upgraded['tick']) == loaded['pause']['tick']
        (args.output / 'upgraded.svce').write_bytes(payload)
        report['upgraded'] = upgraded; record('v4-saved-through-F10', read()); stop()
        restored = start(); record('v4-restarted', restored)
        assert int(restored['pause']['tick']) == upgraded['tick'] + 1
        assert restored['player']['rootKey'] == loaded['player']['rootKey']
        assert restored['tow']['rootKey'] == loaded['tow']['rootKey']
        assert restored['boat']['paidPartIds'] == loaded['boat']['paidPartIds']
        assert restored['boat']['massKg'] == loaded['boat']['massKg']
        assert restored['session']['inventory'] == loaded['session']['inventory']
        assert restored['job']['phase'] == loaded['job']['phase']
        assert restored['pause']['waterTime'] == loaded['pause']['waterTime']
        assert archive(args.source_slot)[0] == original, 'Historical source must remain unchanged'
        report['status'] = 'passed'
    except Exception as error:
        report.update(status='failed', error=str(error)); raise
    finally:
        stop(); x.close(); (args.output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
