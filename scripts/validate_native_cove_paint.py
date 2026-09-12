#!/usr/bin/env python3
"""One native brick: paint, place, repaint, save and reopen through actual X11 controls.

Read-only JSON observations and the real save replicas provide evidence. No
screenshots, direct action calls, state setters, sailing sequence or debug saves.
Run from the repository root inside its native runtime environment.
"""
import argparse
import ctypes as C
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time

from validate_native_cove_delivery import Controls
from validate_native_cove_saves import Event, KeyEvent, archive


WIDTH, HEIGHT = 960, 540
TEAL, BLUE = [35, 145, 137, 255], [50, 108, 190, 255]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--storage-root', type=Path, required=True)
    parser.add_argument('--expected-presentation-parts', type=int, default=3,
                        help='Exact expected visual substitutions (1..16; default 3)')
    args = parser.parse_args()
    if not 1 <= args.expected_presentation_parts <= 16:
        parser.error('--expected-presentation-parts must be between 1 and 16')
    args.binary = args.binary.resolve(strict=True)
    args.output = args.output.resolve()
    args.storage_root = args.storage_root.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    args.storage_root.mkdir(mode=0o700, parents=True, exist_ok=False)
    report = {'status': 'running', 'kind': 'Native brick paint, repaint and exact save/restart; no images',
              'expectedPresentationParts': args.expected_presentation_parts,
              'startedUtc': datetime.now(timezone.utc).isoformat(), 'binary': str(args.binary),
              'binarySha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(), 'stages': []}
    controls = child = stream = window = None
    index = 0
    began = time.monotonic()

    def read():
        try:
            return json.loads((args.output / f'observation-{index}' / 'state.json').read_text())
        except (FileNotFoundError, json.JSONDecodeError):
            return {}

    def persist():
        (args.output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')

    def wait(predicate, label, seconds=45):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if time.monotonic() - began > 300:
                raise RuntimeError('Paint journey exceeded its five-minute bound')
            if child and child.poll() is not None:
                raise RuntimeError(f'Game exited ({child.returncode}): {label}')
            state = read()
            assert not state.get('failed'), state
            if predicate(state):
                return state
            time.sleep(.025)
        raise RuntimeError(label + ': ' + json.dumps(read()))

    def frames(label):
        # Submission alone is not completion. Wait for a newer real completed
        # fixture frame as well as the finished camera request before observing.
        before = int(read()['assetFixture']['submittedSerial'])
        state = wait(lambda s: int(s.get('assetFixture', {}).get('completedSerial', 0)) >= before + 2
                     and not s.get('workshop', {}).get('camera', {}).get('framePending', False), label)
        return state

    def record(name, **extra):
        state = frames('completed frames for ' + name)
        hud, fixture = state['nativeHud'], state['assetFixture']
        assert state['terrainSurface'] == 'lego' and fixture['sceneSunShadows'], fixture
        assert fixture['presentationParts'] == args.expected_presentation_parts and fixture['environmentReady'], fixture
        assert hud['enabled'] and hud['lastEncodedQuads'] > 0, hud
        assert hud['bodyPixels'] >= 16 and not hud['truncated'], hud
        x, y, width, height = hud['panel']
        assert 0 <= x and 0 <= y and 0 < width and 0 < height and x + width <= WIDTH and y + height <= HEIGHT, hud
        assert 0 < int(fixture['completedSerial']) <= int(fixture['submittedSerial']), fixture
        report['stages'].append({'name': name, 'process': index, 'state': state, **extra})
        print(name, flush=True)
        persist()
        return state

    def key(name):
        controls.key(window, name)

    def mouse(point, click=False):
        x, y = map(round, point)
        events = [(6, 64, 0, 0)] + ([(4, 4, 1, 0), (5, 8, 1, 256)] if click else [])
        for kind, mask, button, state in events:
            event = Event()
            event.key = KeyEvent(kind, 0, True, controls.display, window, controls.root, 0,
                                 max(1, int(time.monotonic() * 1000) & 0xffffffff), x, y, x, y, state, button, True)
            assert controls.x.XSendEvent(controls.display, window, False, mask, C.byref(event))
            controls.x.XFlush(controls.display)
            time.sleep(.08)

    def project(point):
        state = read()
        p = [v + state['workshop']['displayOrigin'][i] - 256 * state['camera']['sector'][i]
             for i, v in enumerate(point)] + [1]
        matrix = state['camera']['viewProjection']
        clip = [sum(p[c] * matrix[c * 4 + row] for c in range(4)) for row in range(4)]
        assert clip[3] > 0, clip
        ndc = [clip[0] / clip[3], clip[1] / clip[3]]
        left, bottom, right, top = state['workshop']['camera']['rectangle']
        assert left < ndc[0] < right and bottom < ndc[1] < top, (ndc, state['workshop']['camera'])
        x, y = (ndc[0] + 1) * WIDTH / 2, (1 - ndc[1]) * HEIGHT / 2
        assert not (.78 < y / HEIGHT < .94 and .24 < x / WIDTH < .76), 'Deck target overlaps native palette'
        return x, y

    def stop():
        nonlocal child, stream
        try:
            if child and child.poll() is None:
                child.send_signal(signal.SIGTERM)
                try:
                    child.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=5)
        finally:
            if stream:
                stream.close()
            child = stream = None

    def start(world=None):
        nonlocal child, stream, window, index
        index += 1
        stream = (args.output / f'process-{index}.log').open('w')
        command = [str(args.binary), '--config', 'salvage_cove.cfg', '--uncapped', '--width', str(WIDTH), '--height', str(HEIGHT),
                   '--expedition-root', str(args.storage_root), '--expedition-observe', str(args.output / f'observation-{index}')]
        if world:
            command += ['--expedition-world', world]
        child = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT,
                                 env={**os.environ, 'VOXY_WINDOW_BACKEND': 'x11'})
        wait(lambda _: controls.own_window(child.pid), 'owned native window', 60)
        window = controls.own_window(child.pid)
        wait(lambda s: s.get('ready') and s.get('boat', {}).get('active'), 'native Cove ready', 60)
        if world:
            wait(lambda s: s.get('restore', {}).get('phase') == 'ready' and s['pause']['phase'] == 'paused', 'saved world ready')

    def select_brick():
        for _ in range(11):
            state = read()['workshop']
            if state['name'] == 'Brick 2 x 4':
                assert state['canPaint'] and state['placedBricks'] == 1, state
                return
            previous = state['selected']
            key('Tab')
            wait(lambda s: s['workshop']['selected'] != previous, 'part selection observed')
        raise RuntimeError('Sole kept brick was not selectable')

    def physical(state):
        boat = state['boat']
        assert boat['active'] and boat['parts'] == 11 and boat['massKg'] == 993, boat
        assert boat['paidPartIds'] == ['35'], boat
        assert state['session']['inventory']['salvageMaterial'] == '43', state['session']

    def geometry(state):
        w = state['workshop']
        return {'name': w['name'], 'placement': w['placement'], 'rotation': w['rotation']}

    def launch(label):
        before = read()
        count, completed = before['workshop']['launches'], int(before['boat']['physicsTicks']['completed'])
        key('Return')
        wait(lambda s: not s['workshop']['open'] and not s['workshop']['pending']
             and s['workshop']['launches'] == count + 1, label + ' applied')
        wait(lambda s: int(s['boat']['physicsTicks']['completed']) > completed + 1, label + ' completed physics advancement')
        state = record(label)
        physical(state)
        return state

    try:
        controls = Controls()
        start()
        fresh = record('fresh-world')
        assert fresh['boat']['parts'] == 11 and fresh['boat']['paidPartIds'] == [], fresh['boat']
        assert fresh['session']['inventory']['salvageMaterial'] == '48', fresh['session']
        key('b'); wait(lambda s: s['workshop']['open'], 'workshop open')
        key('Tab'); wait(lambda s: s['workshop']['name'] == 'Cargo cradle', 'cargo cradle selected')
        key('Delete'); wait(lambda s: s['workshop']['changed'] and s['workshop']['parts'] == 10, 'cradle removal preview')
        key('e'); wait(lambda s: not s['workshop']['changed'] and s['workshop']['placedParts'] == 10, 'cargo deck cleared')
        key('3'); wait(lambda s: s['workshop']['brickTool'] and s['workshop']['catalogName'] == 'Brick 2 x 4'
                      and s['workshop']['paintIndex'] == 0, '2 x 4 Original brush')
        for color, name in [(1, 'Cream'), (2, 'Teal')]:
            key('y'); wait(lambda s: s['workshop']['paintIndex'] == color and s['workshop']['paintName'] == name, name + ' selected')
        key('m'); frames('whole boat camera framing completed')
        pointer = project([2, .96, -55])  # Real deck stud in the cleared cargo bay.
        mouse(pointer); frames('deck pointer target completed')
        ghost = read()
        report['deckTarget'] = {'world': [2, .96, -55], 'window': pointer,
                                'observedPlacement': geometry(ghost),
                                'pointerTarget': ghost['workshop']['pointerTarget'],
                                'valid': ghost['workshop']['valid']}
        persist()
        assert ghost['workshop']['valid'] and ghost['workshop']['pointerTarget'], ghost['workshop']
        assert ghost['workshop']['placement'][1] == 72, 'Brick must engage the deck studs at height 72 ticks'
        assert ghost['workshop']['paint'] == TEAL and ghost['workshop']['brushPaint'] == TEAL, ghost['workshop']
        placed_geometry = geometry(ghost)
        mouse(pointer, True)
        wait(lambda s: s['workshop']['placedBricks'] == 1 and s['workshop']['placedParts'] == 11
             and s['workshop']['brickTool'] and s['workshop']['parts'] == 12, 'one brick placed and next ghost created')
        next_ghost = record('teal-brick-placed-next-ghost-teal', placedBrick=placed_geometry)
        assert next_ghost['workshop']['paint'] == TEAL and next_ghost['workshop']['paintIndex'] == 2, next_ghost['workshop']
        key('1'); wait(lambda s: s['workshop']['catalogName'] == 'Brick 1 x 2' and s['workshop']['brickTool'], 'switch spare to 1 x 2')
        switched = record('spare-size-changed-color-preserved')
        assert switched['workshop']['paint'] == TEAL and switched['workshop']['brushPaint'] == TEAL, switched['workshop']
        key('Escape'); wait(lambda s: s['workshop']['open'] and not s['workshop']['brickTool']
                           and not s['workshop']['changed'] and s['workshop']['parts'] == 11, 'spare ghost discarded')
        assert read()['workshop']['charge'] == '5' and read()['workshop']['placedBricks'] == 1, read()['workshop']
        first_launch = launch('teal-brick-launched')
        key('b'); wait(lambda s: s['workshop']['open'], 'repaint workshop open'); select_brick()
        assert read()['workshop']['paint'] == TEAL and geometry(read()) == placed_geometry, read()['workshop']
        key('y'); wait(lambda s: s['workshop']['paintIndex'] == 3 and s['workshop']['changed'], 'Blue repaint preview')
        preview = record('blue-repaint-preview')
        assert preview['workshop']['paint'] == BLUE and preview['workshop']['massKg'] == 993, preview['workshop']
        assert geometry(preview) == placed_geometry, preview['workshop']
        key('e'); wait(lambda s: not s['workshop']['changed'] and s['workshop']['canLaunch'], 'Blue repaint kept')
        kept = record('blue-repaint-kept-free')
        assert all(kept['workshop'][k] == '0' for k in ['charge', 'refund', 'machineryCharge', 'machineryRefund']), kept['workshop']
        physical(kept)
        painted = launch('blue-repaint-launched')
        assert painted['boat']['paidPartIds'] == first_launch['boat']['paidPartIds']
        assert painted['session']['inventory'] == first_launch['session']['inventory']
        wait(lambda s: s['pause']['canPause'], 'safe pause available'); key('p')
        wait(lambda s: s['pause']['phase'] == 'paused', 'paused for checkpoint')
        slot = args.storage_root / painted['world']
        prior_generation = archive(slot)[0]['generation'] if (slot / 'current').exists() else 0
        key('F10')
        def save_ready(state):
            if 'Expedition saved' not in controls.title(window):
                return False
            try:
                meta, _ = archive(slot)
                return meta['generation'] > prior_generation and str(meta['tick']) == state['pause']['tick']
            except (AssertionError, OSError):
                return False
        wait(save_ready, 'new checkpoint generation acknowledged')
        meta, payload = archive(slot)
        (args.output / 'blue-brick.svce').write_bytes(payload)
        saved = record('blue-brick-saved', archive=meta)
        assert saved['boat']['joinedTick'] == str(meta['tick']), saved['boat']
        stop(); start(saved['world'])
        recovered_meta, _ = archive(slot)
        restored = record('blue-brick-world-restored', archive=recovered_meta)
        physical(restored)
        assert restored['session']['inventory'] == saved['session']['inventory'], restored['session']
        # Recovery commits a new authority epoch before activating bodies. Its
        # checkpoint generation advances while the saved physical tick stays exact.
        assert recovered_meta['world'] == meta['world'] and recovered_meta['generation'] > meta['generation'], recovered_meta
        assert recovered_meta['tick'] == meta['tick'] and int(restored['restore']['baseTick']) == meta['tick'], restored['restore']
        assert int(restored['pause']['tick']) == meta['tick'] + 1, restored['pause']
        key('p'); wait(lambda s: s['pause']['phase'] == 'running', 'resume restored world')
        key('b'); wait(lambda s: s['workshop']['open'], 'restored workshop open'); select_brick()
        exact = record('blue-brick-exact-paint-and-geometry-restored')
        assert exact['workshop']['paintIndex'] == 3 and exact['workshop']['paintName'] == 'Blue' and exact['workshop']['paint'] == BLUE, exact['workshop']
        assert geometry(exact) == placed_geometry, (geometry(exact), placed_geometry)
        assert exact['workshop']['massKg'] == 993 and exact['workshop']['placedBricks'] == 1 and not exact['workshop']['changed'], exact['workshop']
        assert exact['workshop']['brushPaint'] is None, 'A restarted brush must not invent a stored color preference'
        physical(exact)
        report['status'] = 'passed'
    except Exception as error:
        report['status'], report['error'], report['lastState'] = 'failed', str(error), read()
        raise
    finally:
        try:
            stop()
        finally:
            if controls:
                controls.close()
            report['elapsedSeconds'] = round(time.monotonic() - began, 3)
            persist()


if __name__ == '__main__':
    main()
