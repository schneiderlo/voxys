#!/usr/bin/env python3
"""Resume one real older Cove save with new presentations; no images or refits.

Copies an existing test save into a fresh directory, then uses ordinary native
P/B input to check restored, running and workshop rendering. Owned save bytes,
stock and the current physical owner must remain exact. No gameplay setters.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import time

from validate_native_cove_delivery import Controls
from validate_native_cove_mechanisms import saved_owned_design
from validate_native_cove_saves import archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--source-slot', type=Path, required=True)
    parser.add_argument('--storage-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--expected-presentation-parts', type=int, required=True)
    args = parser.parse_args()
    assert 1 <= args.expected_presentation_parts <= 16
    args.binary = args.binary.resolve(strict=True)
    args.source_slot = args.source_slot.resolve(strict=True)
    args.storage_root = args.storage_root.resolve()
    args.output = args.output.resolve()
    original, payload = archive(args.source_slot)
    expected = saved_owned_design(payload)
    assert args.source_slot.name == original['world'] == expected['world']
    args.storage_root.mkdir(parents=True, exist_ok=False)
    args.output.mkdir(parents=True, exist_ok=False)
    slot = args.storage_root / original['world']
    shutil.copytree(args.source_slot, slot)
    assert archive(slot)[0] == original
    report = {'status': 'running', 'kind': __doc__.splitlines()[0], 'stages': [],
              'startedUtc': datetime.now(timezone.utc).isoformat(),
              'binary': str(args.binary), 'binarySha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(),
              'sourceSlot': str(args.source_slot), 'sourceArchive': original,
              'expectedOwnedDesign': expected, 'expectedPresentationParts': args.expected_presentation_parts}
    began = time.monotonic()
    controls = child = window = None
    current_owner = None
    stream = (args.output / 'process.log').open('w')

    def persist():
        (args.output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')

    def read():
        try:
            return json.loads((args.output / 'observation/state.json').read_text())
        except (OSError, json.JSONDecodeError):
            return {}

    def wait(predicate, label):
        end = min(began + 90, time.monotonic() + 30)
        while time.monotonic() < end:
            if child.poll() is not None:
                raise RuntimeError(f'Game exited {child.returncode}: {label}')
            state = read()
            assert not state.get('failed'), state
            if predicate(state):
                return state
            time.sleep(.025)
        raise RuntimeError(label + ': ' + json.dumps(read()))

    def owner(state):
        b, m = state['boat'], state['boat']['mechanisms']
        return [b['physicsTicks']['incarnation'], b['buildId'], b['topologyRevision'],
                m['bodyIndex'], m['bodyGeneration'], [r['key'] for r in b['roots']]]

    def record(name):
        state = read()
        fixture, boat = state['assetFixture'], state['boat']
        assert state['world'] == original['world'] and state['ready'] and boat['active']
        assert state['terrainSurface'] == 'lego' and state['session']['admissionOpen']
        assert not boat['physicsTicks']['failed'] and owner(state) == current_owner
        assert fixture['presentationParts'] == args.expected_presentation_parts
        assert fixture['environmentReady'] and fixture['sceneSunShadows']
        assert 0 < int(fixture['gpuReservationBytes']) <= 16 * 1024 * 1024
        assert fixture['draws'] > 0 and fixture['draws'] <= 512
        assert boat['parts'] == 11 and boat['massKg'] == 1035 and boat['paidPartIds'] == []
        assert state['session']['inventory'] == {'salvageMaterial': '48', 'specialMachinery': '0'}
        hud = state['nativeHud']
        assert hud['enabled'] and hud['lastEncodedQuads'] > 0 and not hud['truncated']
        serial, tick = int(fixture['submittedSerial']), int(boat['mechanisms']['tick'])
        assert serial > 0 and tick > 0
        done = wait(lambda s: owner(s) == current_owner
                    and int(s['assetFixture']['completedSerial']) >= serial
                    and int(s['boat']['physicsTicks']['completed']) >= tick,
                    name + ' actual GPU and physics completion')
        meta, saved = archive(slot)
        assert saved_owned_design(saved) == expected
        report['stages'].append({'name': name, 'state': state, 'archive': meta,
                                 'completion': {'fixture': done['assetFixture']['completedSerial'],
                                                'physics': done['boat']['physicsTicks']['completed']}})
        persist()
        return state

    try:
        controls = Controls()
        command = [str(args.binary), '--config', 'salvage_cove.cfg', '--uncapped', '--width', '960', '--height', '540',
                   '--expedition-root', str(args.storage_root), '--expedition-world', original['world'],
                   '--expedition-observe', str(args.output / 'observation')]
        child = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT,
                                 env={**os.environ, 'VOXY_WINDOW_BACKEND': 'x11'})
        wait(lambda _: controls.own_window(child.pid), 'owned native window')
        window = controls.own_window(child.pid)
        restored = wait(lambda s: s.get('restore', {}).get('phase') == 'ready'
                        and s.get('boat', {}).get('active') and s['pause']['phase'] == 'paused', 'old save restored')
        source_build = expected['builds'][0]
        assert restored['boat']['buildId'] == str(int.from_bytes(bytes.fromhex(source_build['id'])[16:], 'little'))
        assert restored['boat']['topologyRevision'] == str(int.from_bytes(bytes.fromhex(source_build['revision']), 'little'))
        assert [r['key'] for r in restored['boat']['roots']] == original['rootKeys']
        current_owner = owner(restored)
        record('older-save-restored-with-new-art')
        controls.key(window, 'p')
        wait(lambda s: s['pause']['phase'] == 'running' and s['workshop']['canOpen'], 'resume at real workshop availability')
        record('restored-cove-running')
        controls.key(window, 'b')
        wait(lambda s: s['workshop']['open'] and not s['workshop']['camera']['framePending'], 'open existing workshop')
        record('restored-design-in-workshop')
        controls.key(window, 'b')
        wait(lambda s: not s['workshop']['open'], 'close workshop without edits')
        record('back-at-dock-with-unchanged-ownership')
        assert archive(args.source_slot)[0] == original, 'original save must remain unchanged'
        report['status'] = 'passed'
    except Exception as error:
        report.update(status='failed', error=str(error), failureState=read())
        raise
    finally:
        terminating = bool(child and child.poll() is None)
        forced_kill = False
        if terminating:
            child.send_signal(signal.SIGTERM)
            try:
                child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                forced_kill = True
                child.kill()
                child.wait(timeout=5)
        if child:
            report['processExit'] = {'terminationRequested': terminating, 'forcedKill': forced_kill,
                                     'exitCode': child.returncode}
            if not terminating or forced_kill or child.returncode not in (0, -signal.SIGTERM):
                report.update(status='failed', error='Unexpected native process exit or forced cleanup')
        stream.close()
        pattern = re.compile(r'uncaptured(?:\s+WebGPU)?(?:\s+GPU)?\s+error|WebGPU validation error|'
                             r'(?:WGPU|WebGPU)[^\n]{0,100}(?:error|validation failed)|\[(?:error|fatal)\s*\]', re.I)
        report['errorLines'] = [line for line in (args.output / 'process.log').read_text(errors='replace').splitlines()
                                if pattern.search(line)]
        if report['errorLines']:
            report.update(status='failed', error='Unexpected native log errors')
        report['elapsedSeconds'] = round(time.monotonic() - began, 3)
        persist()
        if report['status'] != 'passed':
            raise RuntimeError(report.get('error', 'Native art continuation failed'))


if __name__ == '__main__':
    main()
