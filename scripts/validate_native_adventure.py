#!/usr/bin/env python3
"""One bounded native adventure journey through owned X11 keyboard/mouse input.

Observations and confirmed save files are read only. No game actions, state
setters, fabricated input counters, archive edits, screenshots or camera setters.
A failed stage stops once; --continue-from reuses only its confirmed checkpoint.
"""
import argparse
import ctypes as C
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import time

from validate_native_cove_delivery import Controls
from validate_native_cove_saves import Event, KeyEvent

WIDTH, HEIGHT = 1280, 800


def project(camera, point):
    """Project a world point using the actual observed column-major matrix."""
    matrix = camera['viewProjection']
    local = [point[i] - camera['origin'][i] for i in range(3)] + [1.0]
    clip = [sum(matrix[col * 4 + row] * local[col] for col in range(4)) for row in range(4)]
    if clip[3] <= 0:
        raise ValueError('The target is behind the camera')
    return ((clip[0] / clip[3] + 1) * camera['width'] / 2,
            (1 - clip[1] / clip[3]) * camera['height'] / 2)


def movement_keys(yaw, dx, dz):
    distance = math.hypot(dx, dz)
    if distance < 1e-9:
        return []
    forward = (-dx * math.sin(yaw) - dz * math.cos(yaw)) / distance
    right = (dx * math.cos(yaw) - dz * math.sin(yaw)) / distance
    keys = []
    if abs(forward) > .4:
        keys.append('w' if forward > 0 else 's')
    if abs(right) > .4:
        keys.append('d' if right > 0 else 'a')
    return keys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--storage-root', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=480)
    parser.add_argument('--continue-from', type=Path)
    args = parser.parse_args()
    if not 120 <= args.seconds <= 900:
        parser.error('seconds must be 120..900')
    args.binary = args.binary.resolve(strict=True)
    args.output = args.output.resolve(); args.output.mkdir(parents=True, exist_ok=False)
    args.storage_root = args.storage_root.resolve()
    prior = json.loads((args.continue_from.resolve(strict=True) / 'summary.json').read_text()) if args.continue_from else None
    if prior:
        assert prior['storageRoot'] == str(args.storage_root) and prior.get('checkpoints'), 'No confirmed checkpoint to resume'
        checkpoint = prior['checkpoints'][-1]
        world = checkpoint['world']
        retained = prior['stages'][:checkpoint['stageCount']]
        assert args.storage_root.is_dir() and re.fullmatch('[0-9a-f]{32}', world)
    else:
        args.storage_root.mkdir(parents=True, mode=0o700, exist_ok=False)
        world = None; retained = []
    report = dict(status='running', startedUtc=datetime.now(timezone.utc).isoformat(),
                  binary=str(args.binary), binarySha256=hashlib.sha256(args.binary.read_bytes()).hexdigest(),
                  storageRoot=str(args.storage_root), dimensions=[WIDTH, HEIGHT], stages=list(retained), controls=[],
                  checkpoints=list(prior['checkpoints']) if prior else [], processChecks=[])
    if prior:
        report['continuedFrom'] = str(args.continue_from.resolve())
    source = Path(__file__).read_bytes(); (args.output / 'executed-driver.py').write_bytes(source)
    report['driverSha256'] = hashlib.sha256(source).hexdigest()
    report['helperSha256'] = {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
                             for name in ('validate_native_cove_delivery.py', 'validate_native_cove_saves.py')}
    controls = child = stream = window = None
    index = 0; began = time.monotonic(); held = set(); current_stage = 'startup'

    def persist():
        report['elapsedSeconds'] = round(time.monotonic() - began, 3)
        report['currentStage'] = current_stage
        (args.output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')

    def read():
        try:
            return json.loads((args.output / f'observation-{index}.json').read_text())
        except (OSError, json.JSONDecodeError):
            return {}

    def serial(state=None):
        return int((state or read()).get('observation', 0))

    def wait(predicate, label, seconds=12):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if time.monotonic() - began > args.seconds:
                raise RuntimeError('Journey time limit: ' + label)
            if child and child.poll() is not None:
                raise RuntimeError(f'Native child exited {child.returncode}: {label}')
            state = read()
            if predicate(state):
                return state
            time.sleep(.025)
        raise RuntimeError(label + ': ' + json.dumps(read()))

    def fresh():
        previous = serial()
        return wait(lambda s: serial(s) > previous, 'fresh released-input observation')

    def down(name):
        controls.state(window, name, True); held.add(name)

    def up(name):
        controls.state(window, name, False); held.discard(name)

    def key(name, modifiers=()):
        for modifier in modifiers:
            down(modifier)
        down(name)
        try:
            time.sleep(.12)
        finally:
            up(name)
            for modifier in reversed(modifiers):
                up(modifier)
        state = fresh()
        report['controls'].append(dict(key=name, modifiers=list(modifiers), observation=serial(state)))
        return state

    def hold(names, seconds):
        for name in names:
            down(name)
        try:
            time.sleep(seconds)
        finally:
            for name in names:
                up(name)
        state = fresh()
        report['controls'].append(dict(keys=names, heldSeconds=round(seconds, 3), observation=serial(state)))
        return state

    def pointer(x, y):
        assert 5 <= x < WIDTH - 5 and 5 <= y < HEIGHT - 5, ('Pointer outside owned viewport', x, y)
        controls.x.XWarpPointer(controls.display, 0, window, 0, 0, 0, 0, int(x), int(y))
        controls.x.XFlush(controls.display)
        state = fresh()
        actual = state.get('aimInput', {}).get('pointer', [])
        if len(actual) == 2 and math.dist(actual, [int(x), int(y)]) > 2:
            # A native Wayland surface can occlude this owned XWayland client.
            # Send the normal MotionNotify event directly, just as the existing
            # X11 keyboard helper targets its own window. This is input delivery,
            # not a game pointer/state setter. XMotionEvent's is_hint byte and
            # padding occupy the same four bytes as KeyEvent.keycode (zero).
            event = Event()
            event.key = KeyEvent(6, 0, True, controls.display, window, controls.root, 0,
                                 max(1, int(time.monotonic() * 1000) & 0xffffffff), int(x), int(y), 0, 0, 0, 0, True)
            assert controls.x.XSendEvent(controls.display, window, False, 64, C.byref(event))
            controls.x.XFlush(controls.display)
            state = fresh()
            report['controls'].append(dict(motionNotify=[int(x), int(y)], afterWarpObserved=actual,
                                           delivered=state.get('aimInput', {}).get('pointer'), observation=serial(state)))
            actual = state.get('aimInput', {}).get('pointer', [])
        if len(actual) == 2:
            assert math.dist(actual, [int(x), int(y)]) <= 2, ('Owned window did not receive pointer motion', actual, x, y)
        return state

    def mouse(button, down_state, x, y):
        # XButtonEvent has the same wire layout as XKeyEvent: button occupies
        # the keycode field. Send the genuine window event to the owned child.
        event = Event()
        event.key = KeyEvent(4 if down_state else 5, 0, True, controls.display, window, controls.root, 0,
                             max(1, int(time.monotonic() * 1000) & 0xffffffff), int(x), int(y), 0, 0,
                             0, button, True)
        assert controls.x.XSendEvent(controls.display, window, False, 4 if down_state else 8, C.byref(event))
        controls.x.XFlush(controls.display)

    def drag(dx, dy):
        x, y = WIDTH // 2, HEIGHT // 2
        pointer(x, y); mouse(3, True, x, y); time.sleep(.06)
        try:
            pointer(x + max(-450, min(450, dx)), y + max(-300, min(300, dy)))
        finally:
            mouse(3, False, x, y)
        state = fresh(); report['controls'].append(dict(rightDrag=[dx, dy], observation=serial(state)))
        return state

    def aim(point):
        for _ in range(3):
            state = read(); camera = state['camera']
            try:
                x, y = project(camera, point)
                if 30 <= x < WIDTH - 30 and 30 <= y < HEIGHT - 30:
                    pointer(x, y)
                    report['controls'].append(dict(pointerWorld=point, pixel=[round(x, 2), round(y, 2)], observation=serial()))
                    return read()
            except ValueError:
                pass
            player = state['player']; desired = math.atan2(-(point[0] - player['x']), -(point[2] - player['z']))
            error = math.atan2(math.sin(desired - camera['yaw']), math.cos(desired - camera['yaw']))
            drag(error / .004, 30)
        raise RuntimeError('Could not aim at the visible world point: ' + repr(point))

    def menu(name='Adventure paused'):
        if read().get('menu') != name:
            key('F2')
        return wait(lambda s: s.get('menu') == name, 'menu ' + name)

    def choose(prefix):
        state = read(); rows = state.get('rows', [])
        choices = [i for i, row in enumerate(rows) if row['label'].casefold().startswith(prefix.casefold())]
        assert len(choices) == 1, (prefix, rows)
        goal = choices[0]; assert rows[goal].get('enabled', True)
        for _ in range(len(rows) + 1):
            selected = int(read()['menuSelected'])
            if selected == goal:
                return key('Return')
            key('Down' if selected < goal else 'Up')
        raise RuntimeError('Menu selection did not reach ' + prefix)

    def close_menu():
        if read().get('menu'):
            key('F2')
        return wait(lambda s: s.get('menu') == '', 'world input ownership')

    def walk(target, tolerance=.24, seconds=60):
        end = time.monotonic() + seconds; trace = []; stalled = 0
        previous_distance = None
        while time.monotonic() < end:
            state = read(); assert not state['menu'] and not state['build'], state
            p = state['player']; dx, dz = target[0] - p['x'], target[1] - p['z']; distance = math.hypot(dx, dz)
            if distance <= tolerance:
                report.setdefault('walks', []).append(dict(target=list(target), trace=trace)); persist(); return state
            if previous_distance is not None and abs(previous_distance - distance) < .015:
                stalled += 1
            else:
                stalled = 0
            if stalled >= 8:
                report['failedWalk'] = trace
                raise RuntimeError('Movement is blocked on the actual route: ' + repr(target))
            keys = movement_keys(state['camera']['yaw'], dx, dz)
            duration = min(.18, max(.035, (distance - tolerance / 2) / 3.6 * .7))
            trace.append(dict(player=p, target=list(target), keys=keys, duration=duration))
            previous_distance = distance; hold(keys, duration)
        report['failedWalk'] = trace
        raise RuntimeError('Movement approach timed out: ' + repr(target))

    def record(name, state=None, **extra):
        nonlocal current_stage
        current_stage = name; state = state or fresh()
        assert re.fullmatch('[0-9a-f]{32}', state['world']) and state['camera']['width'] == WIDTH and state['camera']['height'] == HEIGHT
        report['stages'].append(dict(name=name, process=index, state=state, **extra)); persist(); print(name, flush=True)
        return state

    def done(name):
        return any(stage['name'] == name for stage in report['stages'])

    def save(label):
        menu(); key('F5')
        state = wait(lambda s: s.get('saveStatus') == 'Saved adventure' and not s.get('dirty', True), 'confirmed durable ' + label, 25)
        slot = args.storage_root / 'adventure-v1' / state['world']
        replicas = {name: (slot / name).read_bytes() for name in ('current', 'mirror')}
        assert replicas['current'] == replicas['mirror'] and len(replicas['current']) > 100
        report['checkpoints'].append(dict(name=label, world=state['world'], stageCount=len(report['stages']),
            files={name: dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest()) for name, data in replicas.items()}, state=state))
        persist(); close_menu(); return state

    def start(selected_world=None):
        nonlocal child, stream, window, index
        index += 1; stream = (args.output / f'process-{index}.log').open('w')
        env = {k: v for k, v in os.environ.items() if k not in ('VOXY_ADVENTURE_NEW', 'VOXY_ADVENTURE_WORLD', 'VOXY_ADVENTURE_ROOT', 'VOXY_ADVENTURE_OBSERVE')}
        env.update(VOXY_WINDOW_BACKEND='x11', VOXY_ADVENTURE_ROOT=str(args.storage_root),
                   VOXY_ADVENTURE_OBSERVE=str(args.output / f'observation-{index}.json'))
        if selected_world:
            env['VOXY_ADVENTURE_WORLD'] = selected_world
        else:
            env['VOXY_ADVENTURE_NEW'] = '1'
        child = subprocess.Popen([str(args.binary), '--config', 'adventure.cfg', '--uncapped', '--width', str(WIDTH), '--height', str(HEIGHT)],
                                 stdout=stream, stderr=subprocess.STDOUT, env=env)
        wait(lambda _: controls.own_window(child.pid), 'owned adventure window', 70); window = controls.own_window(child.pid)
        controls.x.XRaiseWindow(controls.display, window); controls.x.XSetInputFocus(controls.display, window, 2, 0); controls.x.XFlush(controls.display)
        def focused(_):
            current, revert = C.c_ulong(), C.c_int()
            controls.x.XGetInputFocus(controls.display, C.byref(current), C.byref(revert))
            return current.value == window
        wait(focused, 'owned window focus')
        wait(lambda s: serial(s) and s.get('player') and len(s.get('camera', {}).get('viewProjection', [])) == 16, 'read-only adventure observation', 70)
        if selected_world:
            assert read()['world'] == selected_world
        return fresh()

    def stop():
        nonlocal child, stream, window
        for name in list(held):
            up(name)
        requested = forced = False; code = None
        if child:
            if child.poll() is None:
                requested = True; child.send_signal(signal.SIGTERM)
                try:
                    child.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    forced = True; child.kill(); child.wait(timeout=5)
            code = child.returncode
        if stream:
            stream.close()
        if index:
            path = args.output / f'process-{index}.log'
            pattern = re.compile(r'uncaptured(?:\s+WebGPU)?(?:\s+GPU)?\s+error|WebGPU validation error|Abandoning undrained authored shape resources|\[(?:error|fatal)\s*\]', re.I)
            errors = [line for line in path.read_text(errors='replace').splitlines() if pattern.search(line)]
            report['processChecks'].append(dict(process=index, exitCode=code, requestedTermination=requested, forceKilled=forced, errors=errors))
            if forced or code not in (0, -signal.SIGTERM) or errors:
                report.setdefault('cleanupFailures', []).append(report['processChecks'][-1])
        child = stream = window = None

    def all_parts(state=None):
        return [part for structure in (state or read())['structures'] for part in structure['parts']]

    def house_origin():
        stage = next(stage for stage in report['stages'] if stage['name'] == 'starter-room-built-and-paid')
        return stage['origin']

    def geometry_inventory(state):
        return {key: state[key] for key in ('world', 'structures', 'components', 'wood', 'stone', 'scrap', 'registeredBed', 'hammerEquipped')}

    try:
        controls = Controls()
        declarations = {
            'XRaiseWindow': ([C.c_void_p, C.c_ulong], C.c_int),
            'XSetInputFocus': ([C.c_void_p, C.c_ulong, C.c_int, C.c_ulong], C.c_int),
            'XGetInputFocus': ([C.c_void_p, C.POINTER(C.c_ulong), C.POINTER(C.c_int)], C.c_int),
            'XWarpPointer': ([C.c_void_p, C.c_ulong, C.c_ulong, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_int, C.c_int], C.c_int),
        }
        for name, (arguments, result) in declarations.items():
            fn = getattr(controls.x, name); fn.argtypes = arguments; fn.restype = result
        state = start(world)
        if not done('starter-room-built-and-paid'):
            record('full-landscape-spawn', state)
            current_stage = 'walk to a free home site'; walk((-76, -895))
            menu(); choose('Starter room blueprint'); wait(lambda s: s.get('build') and s.get('piece') == 0, 'starter-room tool')
            aim([-85, -146.24, -895])
            for _ in range(3):
                state = fresh()
                if state['valid']:
                    break
                if 'Raise the foundation' not in state['previewReason']:
                    raise RuntimeError('Starter-room preview refused: ' + state['previewReason'])
                key('Prior')
            state = wait(lambda s: s.get('valid'), 'valid charged starter-room preview')
            origin = [state['preview'][axis] for axis in ('x', 'y', 'z')]
            assert state['preview']['yaw'] == 0 and math.hypot(origin[0] + 85, origin[2] + 895) < 1.5, state
            before = {kind: state[kind] for kind in ('wood', 'stone', 'scrap')}
            key('e'); state = wait(lambda s: s.get('parts') == 19, 'accepted starter-room parts')
            assert {kind: before[kind] - state[kind] for kind in before} == dict(wood=70, stone=16, scrap=8), state
            record('starter-room-built-and-paid', state, origin=origin)
            if read()['build']:
                key('b')
            wait(lambda s: not s['build'], 'leave build mode'); save('starter-room')
        origin = house_origin(); ox, oy, oz = origin
        if not done('manual-floor-place-remove-undo'):
            current_stage = 'manual building controls'
            close_menu(); walk((ox + 7, oz + 4))
            key('b'); key('Tab'); wait(lambda s: s.get('menu') == 'Building pieces', 'manual piece catalogue'); choose('Foundation  |')
            aim([ox + 5, oy + .18, oz + 4]); wait(lambda s: s.get('valid'), 'manual foundation preview')
            base = read()['preview'].copy(); count = read()['parts']; stock = {k: read()[k] for k in ('wood', 'stone', 'scrap')}
            key('e'); wait(lambda s: s.get('parts') == count + 1, 'manual foundation admitted')
            key('Tab'); choose('Floor  |')
            point = [base['x'], base['y'] + .32, base['z']]
            aim(point); wait(lambda s: s.get('valid'), 'manual floor on foundation')
            key('e'); wait(lambda s: s.get('parts') == count + 2, 'manual floor admitted')
            aim([base['x'], base['y'] + .64, base['z']]); key('Delete')
            wait(lambda s: s.get('parts') == count + 1, 'manual floor removed')
            aim(point); wait(lambda s: s.get('valid'), 'same floor affordable after removal'); key('e')
            wait(lambda s: s.get('parts') == count + 2, 'floor placed before undo'); key('z', ('Control_L',))
            wait(lambda s: s.get('parts') == count + 1, 'last floor placement undone')
            aim(point); key('Delete'); state = wait(lambda s: s.get('parts') == count, 'temporary foundation removed')
            assert {k: state[k] for k in stock} == stock, state
            key('b'); record('manual-floor-place-remove-undo', read()); save('manual-building')
        if not done('home-functions-and-real-storage'):
            current_stage = 'walk through the house doorway'
            close_menu(); walk((ox + 7, oz + 5)); walk((ox - 1, oz + 5)); walk((ox - 1, oz + 1.0))
            record('walked-through-doorway', read())
            walk((ox - 1, oz + .45)); key('e'); wait(lambda s: s.get('menu') == 'Chest', 'reachable chest opened')
            before = read()['wood']; choose('Store Wood')
            wait(lambda s: s.get('wood') == before - 10 and any(c['kind'] == 2 and c['wood'] == 10 for c in s['components']), 'real items stored')
            choose('Take Wood'); wait(lambda s: s.get('wood') == before and all(c['wood'] == 0 for c in s['components']), 'stored items retrieved')
            # Stand beside the bench's front-left corner, with enough distance
            # from the chest that the nearest-furniture action is unambiguous.
            close_menu(); walk((ox - .58, oz + 1.25), tolerance=.08)
            before = {k: read()[k] for k in ('wood', 'scrap')}; key('e')
            state = wait(lambda s: s.get('wood') == before['wood'] - 4 and s.get('scrap') == before['scrap'] - 2, 'bench crafted real field hammer')
            assert state['hammerEquipped'], state
            walk((ox - .1, oz - .5), tolerance=.06); key('e')
            state = wait(lambda s: int(s.get('registeredBed', '0')) > 0, 'sheltered bed registered')
            record('home-functions-and-real-storage', state); save('usable-home')
        current_stage = 'confirmed save and process restart'
        saved = save('final-home'); expected = geometry_inventory(saved); selected_world = saved['world']
        stop(); assert not report.get('cleanupFailures'), report.get('cleanupFailures')
        restored = start(selected_world)
        assert geometry_inventory(restored) == expected, (expected, geometry_inventory(restored))
        assert math.dist([restored['player'][k] for k in ('x', 'y', 'z')], [saved['player'][k] for k in ('x', 'y', 'z')]) < .03
        record('process-restart-kept-home-items-bed-and-tool', restored)
        report['status'] = 'passed'
    except BaseException as error:
        report['status'] = 'failed'; report['error'] = f'{type(error).__name__}: {error}'; report['lastObservation'] = read()
        print(report['error'], flush=True)
    finally:
        try:
            stop()
        finally:
            if controls:
                controls.close()
            if report.get('cleanupFailures'):
                report['status'] = 'failed'
            persist()
    return 0 if report['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
