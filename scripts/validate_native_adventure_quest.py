#!/usr/bin/env python3
"""Prepared, unexecuted G-B native quest journey; explicit UI-driver permission required.

Uses only an owned child's X11 keyboard/mouse events, read-only runtime JSON,
and confirmed save-file reads. No controller, screenshots, state setters, save
edits, automatic retries or alternate routes. See the companion native-driver.md.
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
import traceback

# Reuse the existing owned-window/physical-key mapping and projection helpers.
# Importing them does not initialize GLFW, open a display, or launch a process.
from validate_native_adventure import Controls, Event, KeyEvent, project


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def materials(state):
    return {kind: state[kind] for kind in ('wood', 'stone', 'scrap')}


def spent(before, after):
    return {kind: before[kind] - after[kind] for kind in before}


def ownership(state):
    """Exact observed ownership, excluding live movement and presentation state."""
    result = {key: state[key] for key in (
        'world', 'structures', 'components', 'wood', 'stone', 'scrap',
        'registeredBed', 'hammerEquipped', 'equippedUtility', 'metNpcMask', 'saveSchema')}
    result['quest'] = {key: state['quest'][key] for key in ('phase', 'recipeUnlocked', 'rewardRevision')}
    return result


def camera_basis(camera):
    matrix = camera['viewProjection']
    assert len(matrix) == 16 and all(math.isfinite(value) for value in matrix)

    def normalized(values):
        length = math.hypot(*values)
        assert length > .1, 'Invalid observed camera basis'
        return [value / length for value in values]

    # Projection row zero gives rendered screen-right even under the LH view.
    right = normalized([matrix[0], matrix[8]])
    forward = normalized([camera['viewTarget'][i] - camera['eye'][i] for i in (0, 2)])
    assert abs(sum(a * b for a, b in zip(right, forward))) < .03
    return right, forward


def verify_bearing(state, target=(-63, -975)):
    compass = state['compass']
    assert compass['equipped'] and compass['available']
    dx, dz = target[0] - state['player']['x'], target[1] - state['player']['z']
    assert abs(compass['distance'] - math.hypot(dx, dz)) < .05
    expected = math.degrees(math.atan2(dx, -dz)) % 360
    assert abs((compass['bearing'] - expected + 180) % 360 - 180) < .05


class Journey:
    def __init__(self, args):
        self.args = args
        self.repo = Path(__file__).resolve().parents[1]
        self.began = time.monotonic()
        self.current_stage = 'prepare owned native host'
        self.controls = self.child = self.stream = self.window = None
        self.index = 0
        self.held = set()
        self.right_down = False
        self.logical_pointer = (1, 1)
        self.last = None
        self.report = dict(
            status='running', startedUtc=datetime.now(timezone.utc).isoformat(),
            binary=str(args.binary), binarySha256=digest(args.binary),
            output=str(args.output), storageRoot=str(args.storage_root),
            executionAuthorizationAcknowledged=True, requestedWindow=[1280, 800],
            scope='Ordinary native keyboard/mouse quest journey; exact observable ownership and durable save copies. No controller, visual or performance acceptance.',
            stages=[], controls=[], walks=[], checkpoints=[], processChecks=[])
        sources = [Path(__file__).resolve(), self.repo / 'adventure.cfg',
                   self.repo / 'tools/validate_adventure_quest_browser.mjs']
        sources += [self.repo / 'scripts' / name for name in (
            'validate_native_adventure.py', 'validate_native_cove_delivery.py', 'validate_native_cove_saves.py')]
        self.report['inputSha256'] = {str(path.relative_to(self.repo)): digest(path) for path in sources}
        (args.output / 'executed-driver.py').write_bytes(Path(__file__).read_bytes())
        self.persist()

    def budget(self):
        if time.monotonic() - self.began >= self.args.seconds:
            raise RuntimeError(f'Overall {self.args.seconds}s limit at {self.current_stage}')

    def persist(self):
        self.report.update(currentStage=self.current_stage, elapsedSeconds=round(time.monotonic() - self.began, 3))
        temporary = self.args.output / 'summary.json.tmp'
        temporary.write_text(json.dumps(self.report, indent=2) + '\n')
        temporary.replace(self.args.output / 'summary.json')

    def read(self):
        try:
            path = self.args.output / f'observation-{self.index}.json'
            assert path.stat().st_size <= 512 * 1024, 'Observation exceeds 512 KiB'
            state = json.loads(path.read_text())
            if state.get('player'):
                self.last = state
            return state
        except (FileNotFoundError, json.JSONDecodeError):
            return {}

    def wait(self, predicate, label, seconds=12):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.budget()
            if self.child and self.child.poll() is not None:
                raise RuntimeError(f'Owned native process exited {self.child.returncode}: {label}')
            state = self.read()
            if state and predicate(state):
                return state
            time.sleep(.05)
        raise RuntimeError(label + '; last observation: ' + json.dumps(self.last))

    def fresh(self):
        previous = int(self.read().get('observation', 0))
        return self.wait(lambda s: int(s['observation']) > previous, 'Fresh released-input observation')

    def trace(self, **value):
        assert len(self.report['controls']) < 12000, 'Input trace capacity reached'
        self.report['controls'].append(dict(**value, observation=self.last['observation']))

    def focus(self):
        self.budget()
        assert self.child and self.child.poll() is None
        assert self.controls.pid(self.window) == self.child.pid, 'Window is no longer owned by this child'
        selected, revert = C.c_ulong(), C.c_int()
        self.controls.x.XGetInputFocus(self.controls.display, C.byref(selected), C.byref(revert))
        assert selected.value == self.window, 'Owned game window lost focus; stop instead of taking another window'

    def hold(self, keys, seconds=.09):
        assert 0 < len(keys) <= 3 and .03 <= seconds <= .22
        self.focus()
        before = self.last['player']
        try:
            for name in keys:
                self.controls.state(self.window, name, True)
                self.held.add(name)
            time.sleep(seconds)
        finally:
            for name in reversed(keys):
                if name in self.held:
                    self.controls.state(self.window, name, False)
                    self.held.discard(name)
        state = self.fresh()
        self.trace(keys=keys, seconds=seconds, before=before, after=state['player'])
        return state

    def key(self, name):
        return self.hold([name])

    def logical_size(self):
        root = C.c_ulong()
        x, y = C.c_int(), C.c_int()
        width, height, border, depth = C.c_uint(), C.c_uint(), C.c_uint(), C.c_uint()
        assert self.controls.x.XGetGeometry(self.controls.display, self.window, C.byref(root),
            C.byref(x), C.byref(y), C.byref(width), C.byref(height), C.byref(border), C.byref(depth))
        assert 100 <= width.value <= 16384 and 100 <= height.value <= 16384
        return width.value, height.value

    def pointer(self, x, y):
        self.focus()
        width, height = self.logical_size()
        assert 5 <= x < width - 5 and 5 <= y < height - 5, 'Pointer outside owned client area'
        x, y = int(round(x)), int(round(y))
        self.controls.x.XWarpPointer(self.controls.display, 0, self.window, 0, 0, 0, 0, x, y)
        self.controls.x.XFlush(self.controls.display)
        state = self.fresh()
        actual = state.get('aimInput', {}).get('pointer')
        assert isinstance(actual, list) and len(actual) == 2, 'Raw logical pointer observation missing'
        if math.dist(actual, [x, y]) > 2:
            # Same ordinary X11 MotionNotify fallback as the G-A owned-window
            # helper. It is recorded explicitly, never a runtime pointer setter.
            event = Event()
            event.key = KeyEvent(6, 0, True, self.controls.display, self.window, self.controls.root, 0,
                max(1, int(time.monotonic() * 1000) & 0xffffffff), x, y, 0, 0,
                1024 if self.right_down else 0, 0, True)
            assert self.controls.x.XSendEvent(self.controls.display, self.window, False, 64, C.byref(event))
            self.controls.x.XFlush(self.controls.display)
            state = self.fresh()
            self.trace(motionNotify=[x, y], warpObserved=actual)
            actual = state['aimInput']['pointer']
        assert math.dist(actual, [x, y]) <= 2, 'Owned window did not receive requested logical pointer'
        self.logical_pointer = (x, y)
        self.trace(pointerLogical=[x, y], clientSize=[width, height])
        return state

    def mouse(self, down, x, y, check_focus=True):
        if check_focus:
            self.focus()
        event = Event()
        event.key = KeyEvent(4 if down else 5, 0, True, self.controls.display, self.window, self.controls.root, 0,
            max(1, int(time.monotonic() * 1000) & 0xffffffff), int(x), int(y), 0, 0,
            0 if down else 1024, 3, True)
        assert self.controls.x.XSendEvent(self.controls.display, self.window, False, 4 if down else 8, C.byref(event))
        self.controls.x.XFlush(self.controls.display)
        self.right_down = down

    def drag(self, dx, dy):
        width, height = self.logical_size()
        x, y = width * .5, height * .34
        dx = max(-min(350, width * .4), min(min(350, width * .4), dx))
        dy = max(-height * .2, min(height * .4, dy))
        self.pointer(x, y)
        self.mouse(True, x, y)
        try:
            time.sleep(.06)
            self.pointer(x + dx, y + dy)
        finally:
            self.mouse(False, x + dx, y + dy)
        self.fresh()
        self.trace(rightDragLogical=[dx, dy])

    def aim(self, point):
        for _ in range(6):
            state = self.read()
            assert state['mode'] == 'build'
            camera = state['camera']
            try:
                pixel = project(camera, point)
            except ValueError:
                pixel = None
            if pixel and 24 <= pixel[0] < camera['width'] - 24 and 24 <= pixel[1] < camera['height'] - 24:
                width, height = self.logical_size()
                logical = [pixel[0] * width / camera['width'], pixel[1] * height / camera['height']]
                state = self.pointer(*logical)
                # Rendering may use a different scale on each axis. Require the
                # observed framebuffer aim to follow the mapped logical pointer;
                # a HUD-owned pointer keeps old aim and does not qualify.
                used = state['aimInput']['usedPointer']
                mapped = [state['aimInput']['pointer'][0] * camera['width'] / width,
                          state['aimInput']['pointer'][1] * camera['height'] / height]
                if (not state['aimInput']['mouseCaptured'] and not state['aimInput']['padOwnsAim']
                        and math.dist(used, mapped) <= 2 and math.dist(used, pixel) <= 4):
                    self.trace(aimWorld=point, framebuffer=pixel, logical=logical, usedPointer=used)
                    return state
            right, forward = camera_basis(state['camera'])
            del right
            yaw = math.atan2(-forward[0], -forward[1])
            desired = math.atan2(-(point[0] - state['player']['x']), -(point[2] - state['player']['z']))
            error = math.atan2(math.sin(desired - yaw), math.cos(desired - yaw))
            self.drag(error / .004, 35)
        raise RuntimeError(f'Cannot frame a world-owned pointer at {point}; six adjustment limit')

    def choose(self, label, prefix=False):
        state = self.read()
        assert state['mode'] not in ('explore', 'build')
        rows = state['rows']
        candidates = [i for i, row in enumerate(rows)
                      if (row['label'].startswith(label) if prefix else row['label'] == label)]
        assert len(candidates) == 1, f'Expected one current menu choice {label!r}: {rows}'
        target = candidates[0]
        row = rows[target]
        assert row['enabled'] and isinstance(row['intent'], int) and row['intent'] > 0, f'Disabled choice: {label}'
        for _ in range(len(rows) + 1):
            current = self.read()
            assert current['mode'] == state['mode'] and current['rows'][target]['intent'] == row['intent'], 'Menu context changed during keyboard navigation'
            selected = int(current['menuSelected'])
            if selected == target:
                self.key('Return')
                self.trace(choice=row['label'], displayedIntent=row['intent'])
                return self.last
            self.key('Down' if selected < target else 'Up')
        raise RuntimeError('Keyboard selection did not reach ' + label)

    def close_sheet(self):
        if self.read()['mode'] not in ('explore', 'build'):
            self.key('F2')
            self.wait(lambda s: s['mode'] in ('explore', 'build'), 'Sheet closed')

    def pause(self):
        self.close_sheet()
        assert self.last['mode'] == 'explore'
        self.key('F2')
        return self.wait(lambda s: s['mode'] == 'pause', 'Pause menu')

    def walk(self, target, tolerance=.2):
        self.current_stage = f'walk {target}'
        trace, previous, stalled = [], math.inf, 0
        for _ in range(240):
            self.budget()
            state = self.read()
            assert state['mode'] == 'explore' and not state['build'], 'World does not own walking input'
            dx, dz = target[0] - state['player']['x'], target[1] - state['player']['z']
            distance = math.hypot(dx, dz)
            if distance <= tolerance:
                self.report['walks'].append(dict(target=target, tolerance=tolerance, trace=trace))
                self.report.pop('activeWalk', None)
                self.persist()
                return state
            stalled = stalled + 1 if abs(previous - distance) < .012 else 0
            assert stalled < 9, f'Blocked route to {target}: {trace[-9:]}'
            right, forward = camera_basis(state['camera'])
            r = (dx * right[0] + dz * right[1]) / distance
            f = (dx * forward[0] + dz * forward[1]) / distance
            keys = ([] if abs(f) <= .4 else ['w' if f > 0 else 's']) + ([] if abs(r) <= .4 else ['d' if r > 0 else 'a'])
            duration = min(.18, max(.035, (distance - tolerance * .5) / 3.6 * .7))
            trace.append(dict(player=state['player'], right=right, forward=forward, keys=keys, seconds=duration))
            self.report['activeWalk'] = dict(target=target, tolerance=tolerance, trace=trace)
            previous = distance
            self.hold(keys, duration)
        raise RuntimeError(f'240 input pulse limit walking to {target}')

    def record(self, name, **extra):
        self.current_stage = name
        state = self.fresh()
        assert re.fullmatch('[0-9a-f]{32}', state['world'])
        self.report['stages'].append(dict(name=name, process=self.index, state=state, **extra))
        self.persist()
        print(name, flush=True)
        return state

    def talk_moss(self):
        moss = next(n for n in self.read()['residents'] if n['id'] == 1)
        assert moss['available'], 'Moss is not admitted'
        self.walk([moss['x'], moss['z'] + 1.1], .15)
        self.wait(lambda s: 'Moss' in s['interaction'], 'Reachable Moss prompt')
        self.key('e')
        self.wait(lambda s: s['mode'] == 'dialogue' and s['dialogue']['npcId'] == 1, 'Moss dialogue')

    def replicas(self, world):
        slot = self.args.storage_root / 'adventure-v1' / world
        copies = {}
        for name in ('current', 'mirror'):
            path = slot / name
            assert path.is_file() and not path.is_symlink() and 100 < path.stat().st_size <= 4 * 1024 * 1024
            copies[name] = path.read_bytes()
        assert copies['current'] == copies['mirror'], 'Confirmed save replicas differ'
        return copies

    def save(self):
        self.current_stage = 'confirm native save through Pause'
        self.pause()
        self.choose('Save adventure')
        state = self.wait(lambda s: s['saveStatus'] == 'Saved adventure' and not s['dirty'], 'Confirmed native manual save', 30)
        replicas = self.replicas(state['world'])
        files = {name: dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest()) for name, data in replicas.items()}
        self.report['checkpoints'].append(dict(world=state['world'], state=state, files=files))
        self.persist()
        return state, replicas

    def start(self, world=None):
        self.current_stage = 'restart saved native world' if world else 'launch fresh isolated native world'
        self.index += 1
        self.stream = (self.args.output / f'process-{self.index}.log').open('w')
        env = {k: v for k, v in os.environ.items() if k not in (
            'VOXY_ADVENTURE_NEW', 'VOXY_ADVENTURE_WORLD', 'VOXY_ADVENTURE_ROOT', 'VOXY_ADVENTURE_OBSERVE')}
        env.update(VOXY_WINDOW_BACKEND='x11', VOXY_ADVENTURE_ROOT=str(self.args.storage_root),
                   VOXY_ADVENTURE_OBSERVE=str(self.args.output / f'observation-{self.index}.json'))
        if world:
            assert re.fullmatch('[0-9a-f]{32}', world)
            env['VOXY_ADVENTURE_WORLD'] = world
        else:
            env['VOXY_ADVENTURE_NEW'] = '1'
        self.child = subprocess.Popen([str(self.args.binary), '--config', 'adventure.cfg', '--uncapped',
            '--width', '1280', '--height', '800'], cwd=self.repo, env=env, stdout=self.stream,
            stderr=subprocess.STDOUT, start_new_session=True)
        # Startup also needs to find a window before the observer first exists.
        end = time.monotonic() + 75
        while time.monotonic() < end:
            self.budget()
            assert self.child.poll() is None, 'Native startup exited before creating its window'
            self.window = self.controls.own_window(self.child.pid)
            if self.window:
                break
            time.sleep(.1)
        assert self.window, 'No owned X11 game window; backend fallback is not an input success'
        self.controls.x.XRaiseWindow(self.controls.display, self.window)
        self.controls.x.XSetInputFocus(self.controls.display, self.window, 2, 0)
        self.controls.x.XFlush(self.controls.display)
        state = self.wait(lambda s: s.get('player') and len(s.get('camera', {}).get('viewProjection', [])) == 16,
                          'Initialized read-only runtime observation', 90)
        self.focus()
        if world:
            assert state['world'] == world
            self.wait(lambda s: s['saveStatus'] == 'Saved adventure loaded', 'Selected saved world restored')
        self.report.setdefault('windows', []).append(dict(process=self.index, pid=self.child.pid,
            logical=list(self.logical_size()), framebuffer=[state['camera']['width'], state['camera']['height']]))
        return self.fresh()

    def stop(self):
        for name in list(self.held):
            self.controls.state(self.window, name, False)
            self.held.discard(name)
        if self.right_down and self.window and self.child and self.child.poll() is None:
            # Release only the owned button; cleanup must not take focus back
            # after an external focus change or fail the elapsed-time guard.
            self.mouse(False, *self.logical_pointer, check_focus=False)
        requested = forced = False
        if self.child:
            if self.child.poll() is None:
                requested = True
                self.child.send_signal(signal.SIGTERM)
                try:
                    self.child.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    forced = True
                    self.child.kill()
                    self.child.wait(timeout=5)
            if self.stream:
                self.stream.close()
            errors = []
            pattern = re.compile(r'uncaptured.*(?:gpu|error)|WebGPU validation error|Abandoning undrained|\[(?:error|fatal)\s*\]', re.I)
            with (self.args.output / f'process-{self.index}.log').open(errors='replace') as log:
                for line in log:
                    if pattern.search(line):
                        errors.append(line.rstrip()[:8000])
                        if len(errors) >= 100:
                            break
            result = dict(process=self.index, exitCode=self.child.returncode,
                requestedTermination=requested, forceKilled=forced, errors=errors)
            self.report['processChecks'].append(result)
            if forced or self.child.returncode not in (0, -signal.SIGTERM) or errors:
                self.report.setdefault('cleanupFailures', []).append(result)
        elif self.stream:
            self.stream.close()
        self.child = self.stream = self.window = None

    def run(self):
        self.controls = Controls()
        declarations = {
            'XRaiseWindow': ([C.c_void_p, C.c_ulong], C.c_int),
            'XSetInputFocus': ([C.c_void_p, C.c_ulong, C.c_int, C.c_ulong], C.c_int),
            'XGetInputFocus': ([C.c_void_p, C.POINTER(C.c_ulong), C.POINTER(C.c_int)], C.c_int),
            'XWarpPointer': ([C.c_void_p, C.c_ulong, C.c_ulong, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_int, C.c_int], C.c_int),
            'XGetGeometry': ([C.c_void_p, C.c_ulong, C.POINTER(C.c_ulong), C.POINTER(C.c_int), C.POINTER(C.c_int),
                C.POINTER(C.c_uint), C.POINTER(C.c_uint), C.POINTER(C.c_uint), C.POINTER(C.c_uint)], C.c_int),
        }
        for name, (arguments, result) in declarations.items():
            function = getattr(self.controls.x, name)
            function.argtypes, function.restype = arguments, result
        state = self.start()
        assert state['parts'] == 0 and state['quest']['phase'] == 'not-accepted' and state['registeredBed'] == '0'
        assert state['equippedUtility']['kind'] == 0 and not state['hammerEquipped']
        right, _ = camera_basis(state['camera'])
        before = state['player']
        self.hold(['d'], .12)
        moved = [self.last['player'][axis] - before[axis] for axis in ('x', 'z')]
        assert sum(a * b for a, b in zip(moved, right)) > .05, 'D does not move toward rendered screen-right'
        self.record('fresh-world-and-rendered-movement-basis', screenRight=right, movement=moved)
        self.talk_moss()
        self.choose('Accept: A Place to Return')
        self.wait(lambda s: s['quest']['phase'] == 'active', 'Home quest accepted')
        assert not self.last['quest']['recipeUnlocked']
        self.record('moss-home-quest-accepted')
        self.close_sheet()
        for point in ([-68.5, -892], [-76, -892], [-76, -895]):
            self.walk(point)
        self.key('b')
        self.wait(lambda s: s['mode'] == 'build', 'B opens native build tray')
        self.key('Tab')
        state = self.wait(lambda s: s['mode'] == 'catalog', 'Keyboard opens building catalog')
        assert state['rows'][0]['label'] == 'Starter room' and state['rows'][0]['pieceKind'] == 0
        assert all(str(cost) in state['rows'][0]['detail'] for cost in (70, 16, 8))
        self.choose('Starter room')
        self.wait(lambda s: s['mode'] == 'build' and s['piece'] == 0, 'Starter room selected')
        self.aim([-85, -146.24, -895])
        for _ in range(4):
            if self.last['valid']:
                break
            assert 'Raise the foundation' in self.last['previewReason'], self.last['previewReason']
            self.key('Prior')
        state = self.wait(lambda s: s['valid'] and s['piece'] == 0, 'Valid west-site room preview')
        origin = [state['preview'][axis] for axis in ('x', 'y', 'z')]
        assert state['preview']['yaw'] == 0 and math.hypot(origin[0] + 85, origin[2] + 895) < 1.5
        stock = materials(state)
        self.key('e')
        self.wait(lambda s: s['parts'] == 19 and s['mode'] == 'explore', 'Paid room accepted')
        assert spent(stock, self.last) == dict(wood=70, stone=16, scrap=8)
        assert len(self.last['structures']) == 1
        self.record('starter-room-built-and-paid', origin=origin, cost=spent(stock, self.last))
        ox, oy, oz = origin

        def enter():
            for point in ([ox + 7, oz + 5], [ox - 1, oz + 5], [ox - 1, oz + 1]):
                self.walk(point)

        def leave():
            for point in ([ox - 1, oz + 1], [ox - 1, oz + 5], [ox + 7, oz + 5]):
                self.walk(point)

        enter()
        assert self.last['player']['y'] > oy + .3
        self.record('walked-through-doorway')
        self.walk([ox - 1, oz + .45])
        self.key('e')
        self.wait(lambda s: s['mode'] == 'chest', 'Reachable chest')
        chest = next(c for c in self.last['components'] if c['kind'] == 2)
        wood = self.last['wood']
        for label, backpack, stored in (('Store Wood', wood - 10, 10), ('Take Wood', wood, 0), ('Store Wood', wood - 10, 10)):
            self.choose(label, prefix=True)
            self.wait(lambda s: s['wood'] == backpack and next(c for c in s['components'] if c['id'] == chest['id'])['wood'] == stored,
                      'Real chest transfer: ' + label)
        self.close_sheet()
        self.walk([ox - .58, oz + 1.25], .08)
        self.key('e')
        self.wait(lambda s: s['mode'] == 'workbench', 'Workbench menu')
        assert any(r['label'] == 'Trail compass: help Moss first' and not r['enabled'] for r in self.last['rows'])
        stock = materials(self.last)
        self.choose('Craft field hammer')
        self.wait(lambda s: s['hammerEquipped'], 'Field hammer crafted and equipped')
        assert spent(stock, self.last) == dict(wood=4, stone=0, scrap=2)
        self.close_sheet()
        self.walk([ox - .1, oz - .5], .06)
        self.key('e')
        self.wait(lambda s: int(s['registeredBed']) > 0 and s['quest']['ready'], 'Sheltered bed registered; home ready')
        self.record('real-home-bed-chest-workbench-ready')
        leave()
        self.walk([-69, -891])
        self.talk_moss()
        stock = materials(self.last)
        self.choose('Complete quest: learn compass')
        self.wait(lambda s: s['quest']['phase'] == 'completed' and s['quest']['recipeUnlocked'] and int(s['quest']['rewardRevision']) > 0,
                  'Permanent recipe receipt')
        receipt = self.last['quest']['rewardRevision']
        assert materials(self.last) == stock and self.last['equippedUtility']['kind'] == 0 and self.last['compass']['backpackSlot'] is None
        self.close_sheet()
        self.talk_moss()
        assert not any('Complete quest' in r['label'] for r in self.last['rows'])
        self.choose('See you soon')
        assert self.last['quest']['rewardRevision'] == receipt and materials(self.last) == stock
        self.record('moss-recipe-earned-once-and-repeat-unavailable')
        self.close_sheet()
        self.walk([-69, -891])
        enter()
        self.walk([ox - .58, oz + 1.25], .08)
        self.key('e')
        self.wait(lambda s: s['mode'] == 'workbench', 'Home workbench after quest')
        stock = materials(self.last)
        self.choose('Craft trail compass')
        self.wait(lambda s: s['compass']['backpackSlot'] is not None and s['equippedUtility']['kind'] == 0, 'Paid compass in backpack')
        assert spent(stock, self.last) == dict(wood=2, stone=0, scrap=4) and not self.last['compass']['available']
        self.choose('Equip trail compass')
        self.wait(lambda s: s['equippedUtility'] == dict(kind=5, quantity=1) and s['compass']['backpackSlot'] is None, 'Compass equipped atomically')
        self.close_sheet()
        leave()
        assert self.last['compass']['target'] == 'relay'
        verify_bearing(self.last)
        distance = self.last['compass']['distance']
        self.walk([ox + 7, oz + 2])
        verify_bearing(self.last)
        assert abs(self.last['compass']['distance'] - distance) > 1
        self.pause()
        self.choose('Open bag')
        self.wait(lambda s: s['mode'] == 'bag', 'Bag menu')
        self.choose('Compass: beacon / switch to home')
        self.wait(lambda s: s['compass']['target'] == 'home' and s['compass']['available'], 'Usable home bearing')
        assert 1 < self.last['compass']['distance'] < 15 and math.isfinite(self.last['compass']['bearing'])
        self.choose('Compass: home / switch to beacon')
        self.wait(lambda s: s['compass']['target'] == 'relay', 'Beacon target restored')
        verify_bearing(self.last)
        self.close_sheet()
        self.record('paid-compass-equipped-and-tracks-real-targets')
        saved, copies = self.save()
        expected = ownership(saved)
        self.stop()
        assert not self.report.get('cleanupFailures'), self.report.get('cleanupFailures')
        restored = self.start(saved['world'])
        assert ownership(restored) == expected, 'Saved observable ownership/progression changed after restart'
        assert self.replicas(saved['world']) == copies, 'Opening the same world rewrote confirmed save bytes'
        assert math.dist([restored['player'][k] for k in ('x', 'y', 'z')], [saved['player'][k] for k in ('x', 'y', 'z')]) < .03
        verify_bearing(restored)
        self.record('same-world-process-restart-preserves-ownership-and-save-copies')
        self.close_sheet()
        self.walk([-69, -891])
        self.talk_moss()
        assert not any('Complete quest' in row['label'] for row in self.last['rows'])
        self.choose('See you soon')
        assert ownership(self.last) == expected, 'Post-restart conversation granted another reward'
        self.record('completed-quest-stays-completed-after-restart')
        assert digest(self.args.binary) == self.report['binarySha256'], 'Native executable changed during journey'
        for name, expected_hash in self.report['inputSha256'].items():
            assert digest(self.repo / name) == expected_hash, 'Prepared input changed during journey: ' + name
        self.report['finalObservation'] = self.read()
        self.report['status'] = 'passed'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--authorized-ui-driver', action='store_true', help='Acknowledge prior explicit owner permission; this flag is not permission')
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--storage-root', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=900)
    args = parser.parse_args()
    if not args.authorized_ui_driver:
        parser.error('Obtain explicit owner permission for this prepared UI driver, then acknowledge it with --authorized-ui-driver. Do not bypass unavailable CUA surfaces.')
    if not 180 <= args.seconds <= 1200:
        parser.error('seconds must be 180..1200')
    args.binary = args.binary.resolve(strict=True)
    if not args.binary.is_file() or not os.access(args.binary, os.X_OK):
        parser.error('binary must be an existing executable')
    args.output, args.storage_root = args.output.resolve(), args.storage_root.resolve()
    if (args.output == args.storage_root or args.output in args.storage_root.parents or args.storage_root in args.output.parents
            or args.output in args.binary.parents or args.storage_root in args.binary.parents):
        parser.error('Use separate new output and world folders, outside the executable directory tree')
    if args.output.exists() or args.storage_root.exists():
        parser.error('Output and storage root must both be new; existing worlds are never opened by a fresh run')
    args.output.mkdir(mode=0o700)
    args.storage_root.mkdir(mode=0o700)
    journey = Journey(args)
    try:
        journey.run()
    except BaseException as error:
        journey.report.update(status='failed', error=f'{type(error).__name__}: {error}',
                              traceback=traceback.format_exc(), lastObservation=journey.last)
        print(journey.report['error'], flush=True)
    finally:
        try:
            journey.stop()
        except BaseException as error:
            journey.report.update(status='failed', cleanupError=f'{type(error).__name__}: {error}')
            if journey.child and journey.child.poll() is None:
                journey.child.kill()
                journey.child.wait(timeout=5)
        finally:
            if journey.controls:
                journey.controls.close()
            if journey.report.get('cleanupFailures'):
                journey.report['status'] = 'failed'
            journey.persist()
    return 0 if journey.report['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
