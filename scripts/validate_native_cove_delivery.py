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
import re
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
    parser.add_argument('--objectives-and-harbor', action='store_true', help='Assert native recovery guidance and extend this same haul through durable harbor power')
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
    checked_logs = set()
    objective_steps = set()

    def read():
        path = args.output / f'observation-{index}' / 'state.json'
        return json.loads(path.read_text()) if path.exists() else {}

    def wait(predicate, label, seconds=30):
        end = time.monotonic() + seconds
        state = {}
        while time.monotonic() < end:
            if time.monotonic() - began > (480 if args.objectives_and_harbor else 360):
                raise RuntimeError('Native journey exceeded its bounded work time')
            if child.poll() is not None:
                raise RuntimeError(f'Native child exited {child.returncode}: {label}')
            state = read()
            assert not state.get('failed'), state
            if args.objectives_and_harbor:
                hud = state.get('nativeHud', {})
                if hud.get('objective'):
                    objective_steps.add(hud['objective'])
                if hud.get('objective') == 'powered':
                    assert state.get('harbor', {}).get('durable') and state['harbor']['installed'], state
                if hud.get('objective') in ('delivered', 'power', 'powered'):
                    assert state.get('job', {}).get('durable'), state
            if predicate(state):
                return state
            time.sleep(.02)
        raise RuntimeError(label + ': ' + json.dumps(state))

    def objective(*steps):
        if not args.objectives_and_harbor:
            return read()
        return wait(lambda state: state.get('nativeHud', {}).get('objective') in steps,
                    'HUD objective ' + '/'.join(steps))

    def body_identity(state):
        boat = state['boat']
        mechanism = boat['mechanisms']
        return (boat['physicsTicks']['incarnation'], boat['buildId'], boat['topologyRevision'],
                mechanism['bodyIndex'], mechanism['bodyGeneration'],
                tuple(root['key'] for root in boat['roots']))

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
        if args.objectives_and_harbor:
            hud, fixture = state['nativeHud'], state['assetFixture']
            assert hud['enabled'] and hud['lastEncodedQuads'] > 0 and hud['bodyPixels'] >= 20 and not hud['truncated'], hud
            assert fixture['presentationParts'] == 7 and fixture['sceneSunShadows'] and fixture['environmentReady'], fixture
            frame = int(fixture['submittedSerial'])
            tick = max(int(boat['observedTick']), int(state['tow']['observedTick']))
            identity = body_identity(state)
            assert frame > 0 and tick > 0
            completed = wait(lambda newer: body_identity(newer) == identity
                             and int(newer['assetFixture']['completedSerial']) >= frame
                             and int(newer['boat']['physicsTicks']['completed']) >= tick,
                             name + ' actual frame and physics completion')
            extra['completion'] = {'frame': frame, 'tick': tick,
                                   'completedFrame': int(completed['assetFixture']['completedSerial']),
                                   'completedTick': int(completed['boat']['physicsTicks']['completed']),
                                   'body': identity}
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
        try:
            if child and child.poll() is None:
                child.send_signal(signal.SIGTERM)
                try:
                    child.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    child.kill(); child.wait(timeout=5)
        finally:
            if stream:
                stream.close()
            child = stream = None
        path = args.output / f'process-{index}.log'
        if args.objectives_and_harbor and index not in checked_logs and path.exists():
            checked_logs.add(index)
            pattern = re.compile(r'uncaptured(?:\s+WebGPU)?(?:\s+GPU)?\s+error|WebGPU validation error|'
                                 r'(?:WGPU|WebGPU)[^\n]{0,100}(?:error|validation failed)|\[(?:error|fatal)\s*\]', re.IGNORECASE)
            errors = [line for line in path.read_text(errors='replace').splitlines() if pattern.search(line)
                      and not (args.permission_failure and 'Expedition storage error' in line)]
            report.setdefault('processChecks', []).append({'process': index, 'errorLines': errors})
            if errors:
                report.update(status='failed', error='Unexpected native log errors: ' + ' | '.join(errors[:8]))
                raise RuntimeError(report['error'])

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

    def walk(target, reached=None, accept_point=False):
        for _ in range(100):
            s = read()
            if reached and reached(s):
                hold([], 3)
                return
            p = s['player']['feet']
            t = target(s) if callable(target) else target
            dx, dz = t[0] - p[0], t[1] - p[2]
            distance = math.hypot(dx, dz)
            if distance < .18:
                hold([], 10)
                if reached is None or accept_point:
                    return
                continue
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
        objective('accept')
        record('fresh')
        key('b')
        wait(lambda s: s['workshop']['open'], 'workshop')
        objective('workshop')
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
        if args.objectives_and_harbor: record('objective-board', objective('board'))
        if args.objectives_and_harbor:
            for point in ([6, -49.5], [4.5, -49.5]):
                walk(point, lambda state: state['player']['interaction'] == 'board', accept_point=True)
            walk([4.5, -53], lambda state: state['player']['interaction'] == 'board')
        else:
            walk([4.5, -49]); walk([4.5, -53])
        wait(lambda s: s['player']['interaction'] == 'board', 'boarding')
        key('e'); wait(lambda s: s['player']['onBoat'], 'aboard')
        walk(lambda s: [s['boat']['helmPosition'][0], s['boat']['helmPosition'][2]],
             (lambda state: state['player']['interaction'] == 'helm') if args.objectives_and_harbor else None)
        wait(lambda s: s['player']['interaction'] == 'helm', 'helm position')
        key('e'); wait(lambda s: s['player']['mode'] == 'helm', 'using helm')
        wait(lambda s: s['tow']['operable'] and s['tow']['confirmed'] and s['tow']['distance'] < 7.5,
             'load within hooking reach')
        objective('hook')
        record('helm-before-hook')
        hold(['f'], 2)
        wait(lambda s: s['tow']['attached'] and s['tow']['confirmed'], 'hooked cargo')
        objective('return', 'lift')
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
        objective('deliver')
        record('eligible-delivery')
        if args.permission_failure:
            args.storage_root.chmod(0o500)
        key('h')
        if args.permission_failure:
            wait(lambda s: s['job']['savePending'] and s['job']['secured'] and s['pause']['phase'] == 'paused'
                 and 'Save failed.' in x.title(window), 'real filesystem permission failure')
            if args.objectives_and_harbor:
                wait(lambda state: state['job']['savePending'] and not state['job']['durable']
                     and state['pause']['phase'] == 'paused'
                     and state.get('nativeHud', {}).get('objective') == 'saving'
                     and state['nativeHud']['status'] == 'Save failed. F10: Retry',
                     'actual failure reached the 10 Hz HUD')
            unsaved = record('delivery-save-permission-failed')
            if args.objectives_and_harbor:
                assert unsaved['nativeHud']['status'] == 'Save failed. F10: Retry', unsaved['nativeHud']
            assert not unsaved['job']['durable'] and not (args.storage_root / unsaved['world'] / 'current').exists()
            key('p'); key('r'); key('h'); key('w')
            time.sleep(.15)
            frozen = record('unsaved-delivery-stays-frozen')
            assert frozen['pause'] == unsaved['pause'] and frozen['player'] == unsaved['player']
            assert frozen['job']['savePending'] and not frozen['job']['durable']
            args.storage_root.chmod(0o700)
            key('F10')
        wait(lambda s: s['job']['durable'] and not s['job']['savePending'] and s['pause']['phase'] == 'paused', 'automatic durable delivery')
        objective('delivered')
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
        objective('delivered')
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
        if args.objectives_and_harbor:
            # Reuse the existing harbor journey's ordinary Rescue/berth return;
            # no cargo/world payload is fabricated and this is still one haul.
            record('objective-return-to-dock', objective('dock'))
            before_rescue = int(read()['rescue']['completed'])
            key('r')
            wait(lambda state: int(state['rescue']['completed']) > before_rescue
                 and not state['rescue']['pending'] and state['pause']['phase'] == 'paused',
                 'real recovery saved at berth')
            key('p'); wait(lambda state: state['pause']['phase'] == 'running', 'resume at berth')
            walk([4.5, -52.5], lambda state: state['harbor']['canInstall'])
            wait(lambda state: state['harbor']['canInstall'], 'dock installation available')
            ready = record('objective-power-harbor', objective('power'))
            assert 'K: Power harbor' in ready['nativeHud']['hints']
            key('k')
            wait(lambda state: state['harbor']['durable'] and not state['harbor']['pending']
                 and state['pause']['phase'] == 'paused', 'harbor installation saved')
            powered = record('objective-powered-and-saved', objective('powered'))
            assert powered['session']['inventory'] == saved['session']['inventory']
            assert powered['boat']['paidPartIds'] == saved['boat']['paidPartIds']
            assert powered['boat']['massKg'] == saved['boat']['massKg']
            meta, payload = archive(args.storage_root / saved['world'])
            assert meta['tick'] == int(powered['pause']['tick'])
            (args.output / 'powered.svce').write_bytes(payload)
            report['poweredArchive'] = meta
            assert {'accept', 'workshop', 'board', 'hook', 'return', 'lift', 'deliver', 'delivered', 'dock', 'power', 'powered'} <= objective_steps, objective_steps
            if args.permission_failure:
                assert 'saving' in objective_steps
            report['objectiveSteps'] = sorted(objective_steps)
        else:
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
        try:
            stop()
        finally:
            x.close()
            (args.output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
