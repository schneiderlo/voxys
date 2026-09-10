#!/usr/bin/env python3
"""Actual native construction/hauling, automatic disk save and process restart.
X11 keys target only owned child windows. Observation is read-only; no images,
direct action calls, game-state setters, altered assets or success injection.
"""
import argparse
import ctypes as C
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import time
from validate_native_cove_saves import X11, Event, KeyEvent, archive


class Controls(X11):
    def __init__(self):
        super().__init__()
        # Game actions use GLFW's physical key IDs. A layout-dependent keysym
        # lookup sends Q to the A action on AZERTY. Ask the same native library
        # for its scancodes; this reads mappings without changing any settings.
        glfw = C.CDLL('libglfw.so.3')
        glfw.glfwInitHint(0x00050003, 0x00060004)  # GLFW_PLATFORM, GLFW_PLATFORM_X11
        assert glfw.glfwInit(), 'Could not read GLFW input mapping'
        try:
            self.keycodes = {chr(key).lower(): glfw.glfwGetKeyScancode(key) for key in range(65, 91)}
        finally:
            glfw.glfwTerminate()

    def key(self, window, name):
        self.state(window, name, True); time.sleep(.08)
        self.state(window, name, False); time.sleep(.08)

    def state(self, window, name, down):
        code = self.keycodes.get(name.lower()) or self.x.XKeysymToKeycode(self.display, self.x.XStringToKeysym(name.encode()))
        assert code, 'Unknown key ' + name
        stamp = max(1, int(time.monotonic() * 1000) & 0xffffffff)
        event = Event()
        event.key = KeyEvent(2 if down else 3, 0, True, self.display, window,
                            self.root, 0, stamp, 1, 1, 1, 1, 0, code, True)
        assert self.x.XSendEvent(self.display, window, False, 1 if down else 2, C.byref(event))
        self.x.XFlush(self.display)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, default=Path('bazel-bin/voxy_native'))
    parser.add_argument('--storage-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--permission-failure', action='store_true', help='Check real unwritable save-root failure and explicit retry')
    args = parser.parse_args()
    args.storage_root = args.storage_root.resolve()
    args.storage_root.mkdir(parents=True, exist_ok=False)
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    report = {'status': 'running', 'kind': 'native keyboard construction, haul, automatic delivery save and process restart',
              'binary': str(args.binary.resolve()), 'binarySha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(), 'stages': []}
    x = Controls()
    child = stream = window = None
    index = 0
    began = time.monotonic()

    def read():
        path = args.output / f'observation-{index}' / 'state.json'
        return json.loads(path.read_text()) if path.exists() else {}

    def wait(predicate, label, seconds=30):
        end = time.monotonic() + seconds
        state = {}
        while time.monotonic() < end:
            if time.monotonic() - began > 360:
                raise RuntimeError('Native journey exceeded six-minute bound')
            if child.poll() is not None:
                raise RuntimeError(f'Native child exited {child.returncode}: {label}')
            state = read()
            assert not state.get('failed'), state
            if predicate(state):
                return state
            time.sleep(.02)
        raise RuntimeError(label + ': ' + json.dumps(state))

    def record(name, state=None, **extra):
        state = state or read()
        assert not state.get('failed') and state['terrainSurface'] == 'lego'
        boat = state['boat']
        roots = boat['roots']
        assert boat['rootCount'] == len(roots) and 0 < len(roots) <= 32
        assert len({root['key'] for root in roots}) == len(roots)
        joined = int(boat['joinedTick'])
        if joined:
            assert all(root['active'] and int(root['observedTick']) == joined for root in roots)
        if state['pause']['phase'] == 'paused':
            assert joined == int(boat['observedTick']) and joined > 0
        report['stages'].append({'name': name, 'state': state, **extra})
        (args.output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
        return state

    def key(name):
        x.key(window, name)

    def hold(names, ticks):
        # Wait for actual player steps: a short wall-clock press can be
        # entirely between native frames, leaving no held movement input.
        before = int(read()['player']['tick'])
        try:
            for name in names:
                x.state(window, name, True)
            wait(lambda s: int(s['player']['tick']) >= before + max(1, ticks), 'held input steps')
        finally:
            for name in names:
                x.state(window, name, False)
        previous = read().get('player', {}).get('tick', '0')
        return wait(lambda s: int(s.get('player', {}).get('tick', 0)) > int(previous), 'fresh input observation')

    def start(world=None):
        nonlocal child, stream, window, index
        index += 1
        stream = (args.output / f'process-{index}.log').open('w')
        command = [str(args.binary.resolve()), '--config', 'salvage_cove.cfg', '--uncapped',
                   '--expedition-root', str(args.storage_root), '--width', '960', '--height', '540',
                   '--expedition-observe', str(args.output / f'observation-{index}')]
        if world:
            command += ['--expedition-world', world]
        child = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT,
                                 env={**os.environ, 'VOXY_WINDOW_BACKEND': 'x11'})
        wait(lambda _: x.own_window(child.pid), 'own native window', 60)
        window = x.own_window(child.pid)
        wait(lambda s: s.get('ready') and s.get('boat', {}).get('active'), 'active native cove', 60)
        if world:
            wait(lambda s: s.get('restore', {}).get('phase') == 'ready' and s['pause']['phase'] == 'paused', 'restored native delivery')

    def stop():
        nonlocal child, stream
        if child and child.poll() is None:
            child.send_signal(signal.SIGTERM)
            child.wait(timeout=15)
        if stream:
            stream.close()
        child = stream = None

    def select(name):
        for _ in range(18):
            if read()['workshop'].get('name') == name:
                return
            previous = read()['workshop']['selected']
            key('Tab')
            wait(lambda s: s['workshop']['selected'] != previous, 'part selection observed')
        raise RuntimeError('Could not select ' + name)

    def keep():
        wait(lambda s: s['workshop'].get('valid') and s['workshop'].get('changed'), 'connected draft')
        revision = int(read()['workshop']['revision'])
        key('e')
        wait(lambda s: int(s['workshop']['revision']) == revision + 1 and not s['workshop'].get('changed'), 'kept edit')

    def move(target):
        placement = read()['workshop']['placement']
        assert read()['workshop']['rotation'] == 0
        for axis, (step, buttons) in enumerate(zip([50, 16, 50], [('Left', 'Right'), ('z', 'q'), ('Up', 'Down')])):
            count = (target[axis] - placement[axis]) / step
            assert count.is_integer() and abs(count) < 40
            for _ in range(abs(int(count))):
                expected = read()['workshop']['placement'].copy()
                expected[axis] += step if count > 0 else -step
                key(buttons[1 if count > 0 else 0])
                wait(lambda s: s['workshop']['placement'] == expected, 'part move observed')
        wait(lambda s: s['workshop']['placement'] == target, 'exact design placement')

    def walk(target):
        for _ in range(100):
            s = read()
            p = s['player']['feet']
            t = target(s) if callable(target) else target
            dx, dz = t[0] - p[0], t[1] - p[2]
            distance = math.hypot(dx, dz)
            if distance < .18:
                hold([], 10)
                return
            yaw = s['camera']['yaw']
            forward = (dx * math.sin(yaw) + dz * math.cos(yaw)) / distance
            right = (dx * math.cos(yaw) - dz * math.sin(yaw)) / distance
            keys = []
            if abs(forward) > .4:
                keys.append('w' if forward > 0 else 's')
            if abs(right) > .4:
                keys.append('d' if right > 0 else 'a')
            hold(keys, max(1, min(5, int(distance / .06) - 1)))
        raise RuntimeError('Could not reach boat interaction: ' + json.dumps(read()))

    try:
        start()
        record('fresh')
        key('b')
        wait(lambda s: s['workshop']['open'], 'workshop')
        select('Cargo cradle'); record('cradle-selected'); key('Delete'); keep()
        assert read()['workshop']['massKg'] == 945
        select('Winch'); move([75, 96, -2750]); keep()
        key('c')
        wait(lambda s: s['workshop']['catalogName'] == 'Beam', 'beam drawer')
        before_add = read()['workshop']['selected']
        key('v'); wait(lambda s: s['workshop']['selected'] != before_add, 'added beam selected')
        move([-50, 56, -2750]); keep()
        select('Winch'); move([-125, 112, -2750]); keep()
        record('lifting-rig-design')
        key('Return')
        wait(lambda s: not s['workshop']['open'] and not s['workshop']['pending'] and s['pause']['canPause'], 'launched rig')
        rig = record('paid-lifting-rig')
        assert rig['boat']['parts'] == 11 and rig['boat']['massKg'] == 1035
        assert rig['session']['inventory']['salvageMaterial'] == '36'
        key('j')
        wait(lambda s: s['job']['phase'] == 'accepted' and not s['job']['pending'], 'accepted job')
        walk([4.5, -49]); walk([4.5, -53])
        wait(lambda s: s['player']['interaction'] == 'board', 'boarding')
        key('e'); wait(lambda s: s['player']['onBoat'], 'aboard')
        walk(lambda s: [s['boat']['helmPosition'][0], s['boat']['helmPosition'][2]])
        wait(lambda s: s['player']['interaction'] == 'helm', 'helm position')
        key('e'); wait(lambda s: s['player']['mode'] == 'helm', 'using helm')
        wait(lambda s: s['tow']['operable'] and s['tow']['confirmed'] and s['tow']['distance'] < 7.5,
             'load within hooking reach')
        record('helm-before-hook')
        hold(['f'], 2)
        wait(lambda s: s['tow']['attached'] and s['tow']['confirmed'], 'hooked cargo')
        record('hooked')
        for i in range(32):
            # Bring the load alongside without hauling it into the beam.
            # This approach tows home partly submerged, then hoists in harbor.
            if read()['tow']['ropeLength'] <= 4:
                break
            hold(['q'], 5); hold([], 10)
            lifted = record(f'shorten-tow-{i}')
            assert not lifted['tow']['broken'], 'Cable broke during lift'
        assert read()['tow']['ropeLength'] <= 4, 'Could not shorten towing line'
        record('load-alongside')
        for i in range(48):
            s = read()
            if s['job']['harborDistance'] <= 3.1:
                break
            p, q = s['tow']['position'], s['boat']['orientation']
            desired = math.atan2(.5 - p[0], -54 - p[2])
            heading = math.atan2(2*(q[0]*q[2] + q[1]*q[3]), 1 - 2*(q[0]**2 + q[1]**2))
            error = math.atan2(math.sin(desired-heading), math.cos(desired-heading))
            keys = ['s']
            if abs(error) > .12:
                keys.append('d' if error > 0 else 'a')
            hold(keys, 15); hold([], 5)
            if i % 3 == 0:
                record(f'loaded-return-{i}')
        assert read()['job']['harborDistance'] <= 3.1, 'Loaded return did not reach harbor'
        record('loaded-return-complete')
        for i in range(16):
            if read()['job']['canDeliver']:
                break
            assert read()['tow']['ropeLength'] > 2.5, 'Stop before pulling cargo into the beam'
            hold(['q'], 5); hold([], 15)
            record(f'harbor-hoist-{i}')
        wait(lambda s: s['job']['canDeliver'], 'cargo inside harbor and slow enough')
        record('eligible-delivery')
        if args.permission_failure:
            args.storage_root.chmod(0o500)
        key('h')
        if args.permission_failure:
            wait(lambda s: s['job']['savePending'] and s['job']['secured'] and s['pause']['phase'] == 'paused'
                 and 'Save failed.' in x.title(window), 'real filesystem permission failure')
            unsaved = record('delivery-save-permission-failed')
            assert not unsaved['job']['durable'] and not (args.storage_root / unsaved['world'] / 'current').exists()
            key('p'); key('r'); key('h'); key('w')
            time.sleep(.15)
            frozen = record('unsaved-delivery-stays-frozen')
            assert frozen['pause'] == unsaved['pause'] and frozen['player'] == unsaved['player']
            assert frozen['job']['savePending'] and not frozen['job']['durable']
            args.storage_root.chmod(0o700)
            key('F10')
        wait(lambda s: s['job']['durable'] and not s['job']['savePending'] and s['pause']['phase'] == 'paused', 'automatic durable delivery')
        saved = record('delivery-saved')
        assert saved['job']['phase'] == 'completed' and saved['job']['secured']
        assert saved['session']['inventory']['salvageMaterial'] == '96' and saved['session']['cargo'] == 0
        assert not saved['tow']['attached']
        meta, payload = archive(args.storage_root / saved['world'])
        assert meta['tick'] == int(saved['pause']['tick'])
        (args.output / 'delivery.svce').write_bytes(payload)
        record('mirrored-delivery-checkpoint', archive=meta)
        stop()
        start(saved['world'])
        restored = record('delivery-restarted')
        assert restored['job']['durable'] and restored['job']['secured'] and restored['job']['phase'] == 'completed'
        assert restored['session']['inventory'] == saved['session']['inventory'] and restored['session']['cargo'] == 0
        assert restored['boat']['paidPartIds'] == saved['boat']['paidPartIds'] and restored['boat']['massKg'] == 1035
        assert int(restored['pause']['tick']) == int(saved['pause']['tick']) + 1
        assert int(restored['observation']['epoch']) == int(saved['observation']['epoch']) + 1
        key('p'); wait(lambda s: s['pause']['phase'] == 'running', 'resume')
        key('h'); hold([], 20)
        repeated = record('duplicate-delivery-refused')
        assert repeated['session']['inventory'] == saved['session']['inventory'] and repeated['session']['cargo'] == 0
        hold(['w'], 90)
        sailed = record('sail-away')
        assert math.dist(sailed['tow']['position'], restored['tow']['position']) < .001
        assert sailed['boat']['speed'] > .2 and sailed['session']['inventory']['salvageMaterial'] == '96'
        report['status'] = 'passed'
    except Exception as error:
        report.update(status='failed', error=str(error))
        raise
    finally:
        args.storage_root.chmod(0o700)
        stop(); x.close()
        (args.output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
