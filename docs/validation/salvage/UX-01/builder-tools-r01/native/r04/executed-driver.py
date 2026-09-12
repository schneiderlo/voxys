#!/usr/bin/env python3
"""One native controller builder/design-library journey, with no images.

Uses a temporary Linux uinput controller and X11 keys only. The native observer
is read-only. Exported design files are copied into Imports as a player would;
world save resources and running game state are never modified by the driver.
Requires --binary, a NEW --output directory and a NEW --storage-root directory.
"""
import argparse
import ctypes as C
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import struct
import subprocess
import time

from cove_virtual_gamepad import VirtualGamepad
from validate_native_cove_delivery import Controls
from validate_native_cove_saves import archive

WIDTH, HEIGHT = 960, 800
NAME_KEYS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_.'
TEAL, BLUE = [35, 145, 137, 255], [50, 108, 190, 255]


def blueprint_file(path):
    value = json.loads(path.read_text())
    assert set(value) == {'version', 'name', 'blueprint'} and value['version'] == 1
    raw = bytes.fromhex(value['blueprint'])
    assert value['blueprint'] == raw.hex() and raw[:8] == b'SVBP\1\0\0\0'
    assert hashlib.sha256(raw[:-32]).digest() == raw[-32:]
    parts, connections = struct.unpack_from('<II', raw, 8)
    assert 0 < parts <= 256 and connections <= 1024
    assert len(raw) == 16 + 59 * parts + 70 * connections + 32
    records = [raw[16 + i * 59:16 + (i + 1) * 59] for i in range(parts)]
    assert len({part[:4] for part in records}) == parts
    return value, raw, records


def owned_design(payload):
    """Read the frozen schema4/session records; never write them into the game.

    Authority epochs/leases change during restore. Compare accepted part IDs,
    geometry, health, settings, paint, provenance and weld records exactly.
    """
    assert payload[:8] == b'SVCE\4\0\0\0'
    assert hashlib.sha256(payload[:-32]).digest() == payload[-32:]
    data, at = payload, 441

    def take(n):
        nonlocal at
        assert 0 <= n <= 4 * 1024 * 1024 and at + n <= len(data) - 32
        out = data[at:at + n]; at += n
        return out

    def count(limit):
        n = int.from_bytes(take(4), 'little'); assert n <= limit
        return n

    recovery = [hashlib.sha256(take(count(131072))).hexdigest() for _ in range(count(4))]
    take(48); roots = count(32); assert roots > 0; take(88 * roots)
    data = take(count(4 * 1024 * 1024))
    assert data[:4] == b'SVSC' and 1 <= int.from_bytes(data[4:8], 'little') <= 3
    assert hashlib.sha256(data[:-32]).digest() == data[-32:]
    at = 8 + 4 + 68
    world = take(16).hex(); take(48); take(8); take(8); take(16)
    inventory = list(struct.unpack('<QQ', take(16)))
    take(64); assert take(1) == b'\1'
    builds, all_parts = [], []
    for _ in range(count(32)):
        identity, revision, owner = take(24), take(8), take(24)
        lease = take(1); assert lease in (b'\0', b'\1')
        if lease == b'\1': take(40)
        parts = [take(130) for _ in range(count(256))]
        connections = [take(136) for _ in range(count(1024))]
        assert parts and all(p[:16].hex() == world for p in parts)
        all_parts.extend(parts)
        builds.append({'id': identity.hex(), 'revision': revision.hex(), 'owner': owner.hex(),
                       'partIds': [p[:24].hex() for p in parts], 'parts': len(parts),
                       'partsSha256': hashlib.sha256(b''.join(parts)).hexdigest(),
                       'connections': len(connections),
                       'connectionsSha256': hashlib.sha256(b''.join(connections)).hexdigest()})
    assert len(builds) == 1 and builds[0]['parts'] == 12
    assert all(int.from_bytes(p[89:91], 'little') == 10000 for p in all_parts)
    assert sum(p[105] == 1 for p in all_parts) == 10  # Retained starter loans after deliberate cradle removal.
    assert sum(p[105] == 0 for p in all_parts) == 2   # Only the two actual purchases.
    return {'world': world, 'inventory': inventory, 'builds': builds,
            'recoveryDesignDigests': recovery}, all_parts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--storage-root', type=Path, required=True)
    args = parser.parse_args()
    args.binary = args.binary.resolve(strict=True)
    args.output = args.output.resolve(); args.storage_root = args.storage_root.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    args.storage_root.mkdir(mode=0o700, parents=True, exist_ok=False)
    report = {'status': 'running', 'kind': 'Native OS-controller builder and durable named designs; no images',
              'startedUtc': datetime.now(timezone.utc).isoformat(), 'binary': str(args.binary),
              'binarySha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(),
              'dimensions': [WIDTH, HEIGHT], 'stages': [], 'controls': [], 'checks': []}
    controls = pad = child = stream = window = None
    index = 0; began = time.monotonic(); checked_logs = set()

    def read():
        try: return json.loads((args.output / f'observation-{index}' / 'state.json').read_text())
        except (OSError, json.JSONDecodeError): return {}

    def persist():
        (args.output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')

    def wait(predicate, label, seconds=20):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if time.monotonic() - began > 420:
                raise RuntimeError('Builder journey reached its 420-second action budget')
            if child and child.poll() is not None:
                raise RuntimeError(f'Native game exited ({child.returncode}): {label}')
            state = read(); assert not state.get('failed'), state
            if predicate(state): return state
            time.sleep(.025)
        raise RuntimeError(label + ': ' + json.dumps(read()))

    def frame(s): return int(s.get('assetFixture', {}).get('submittedSerial', 0))
    def fresh():
        # The observer runs at 10 Hz. Do not derive the next control from a
        # sample captured before this release reached the actual input owner.
        before = frame(read())
        return wait(lambda s: frame(s) >= before + 2, 'fresh post-release observation')

    def key(name):
        controls.key(window, name); fresh()

    def focus(target):
        if target != controls.root: controls.x.XRaiseWindow(controls.display, target)
        controls.x.XSetInputFocus(controls.display, target, 2, 0)
        controls.x.XFlush(controls.display)
        def current(_):
            actual, revert = C.c_ulong(), C.c_int()
            controls.x.XGetInputFocus(controls.display, C.byref(actual), C.byref(revert))
            return actual.value == target
        wait(current, 'owned X11 focus transition'); fresh()

    def pulse(button):
        wait(lambda s: s.get('gamepad', {}).get('armed'), 'neutral controller armed')
        # Confirm and menu buttons never repeat. Use one deliberate press,
        # followed by release; never retry a command based on a missed result.
        pad.button(button, True); time.sleep(.20); pad.button(button, False)
        state = fresh()
        report['controls'].append({'button': button, 'page': state.get('nativeMenu', {}).get('page'),
                                   'selected': state.get('nativeMenu', {}).get('selected'), 'frame': frame(state)})
        return state

    def direction(name):
        axis, value = {'up': ('dy', -1), 'down': ('dy', 1), 'left': ('dx', -1), 'right': ('dx', 1)}[name]
        wait(lambda s: s.get('gamepad', {}).get('armed'), 'controller navigation armed')
        pad.axis(axis, value); time.sleep(.09); pad.axis(axis, 0)
        state = fresh()
        report['controls'].append({'direction': name, 'page': state['nativeMenu']['page'],
                                   'selected': state['nativeMenu']['selected'], 'key': state['nativeMenu']['key']})
        return state

    def menu(s=None): return (s or read())['nativeMenu']
    def library(s=None): return menu(s)['library']

    def target(label):
        view = menu(); rows = view['rows']
        matches = [i for i, row in enumerate(rows) if row['label'].removeprefix('* ') == label]
        assert len(matches) == 1, (label, rows)
        wanted = matches[0]; assert rows[wanted]['enabled'], (label, view)
        for _ in range(len(rows) + 1):
            current = menu()['selected']
            if current == wanted: return
            forward, back = (wanted - current) % len(rows), (current - wanted) % len(rows)
            direction('down' if forward <= back else 'up')
        raise RuntimeError('Controller did not reach menu row ' + label)

    def choose(label):
        target(label); return pulse('a')

    def main_menu():
        if not menu()['open']:
            pulse('menu'); wait(lambda s: menu(s)['page'] == 'main', 'controller opened workshop menu')
        for _ in range(5):
            if menu()['page'] == 'main': return
            pulse('b')
        raise RuntimeError('Could not return to the main workshop menu')

    def library_page():
        main_menu(); choose('Saved designs')
        wait(lambda s: menu(s)['page'] == 'library' and library(s)['ready'] and not library(s)['busy'], 'durable library ready')

    def name_with_grid(text=None):
        wait(lambda s: menu(s)['naming'] and menu(s)['keyboardFocus'], 'on-screen naming grid')
        if text is not None:
            for char in text:
                desired = NAME_KEYS.index(char)
                for _ in range(14):
                    current = menu()['key']
                    if current == desired: break
                    x, y = current % 10, current // 10
                    dx, dy = desired % 10, desired // 10
                    if y != dy: direction('down' if (dy-y) % 4 <= (y-dy) % 4 else 'up')
                    else: direction('right' if (dx-x) % 10 <= (x-dx) % 10 else 'left')
                assert menu()['key'] == desired
                pulse('a')
            assert menu()['name'] == text, menu()
        pulse('lb'); wait(lambda s: not menu(s)['keyboardFocus'], 'controller moved from letters to Done')
        choose('Done')
        wait(lambda s: not menu(s)['naming'] and not library(s)['busy'] and library(s)['ready'], 'named operation durably completed')

    def row(name):
        found = [r for r in library()['rows'] if r['name'] == name]
        assert len(found) == 1, (name, library())
        return found[0]

    def identity(s):
        boat = s['boat']; motion = boat['mechanisms']
        return (boat['physicsTicks']['incarnation'], motion['incarnation'], boat['buildId'], boat['topologyRevision'],
                tuple(r['key'] for r in boat['roots']), motion['bodyIndex'], motion['bodyGeneration'])

    def sample(name, predicate=lambda _: True):
        s = wait(lambda s: s.get('ready') and s.get('boat', {}).get('active') and predicate(s), name)
        boat, fixture, hud = s['boat'], s['assetFixture'], s['nativeHud']
        ticks = boat['physicsTicks']; motion = boat['mechanisms']
        assert ticks['supported'] and not ticks['failed'] and int(ticks['incarnation']) > 0
        assert motion['incarnation'] == ticks['incarnation'] and motion['bodyIndex'] > 0 and motion['bodyGeneration'] > 0
        assert fixture['sceneSunShadows'] and fixture['environmentReady'] and fixture['presentationParts'] == 9
        assert hud['enabled'] and hud['lastEncodedQuads'] > 0 and hud['bodyPixels'] >= 16 and not hud['truncated']
        x, y, width, height = hud['panel']; assert 0 <= x and 0 <= y and x + width <= WIDTH and y + height <= HEIGHT
        assert s['session']['inventory']['specialMachinery'] == '0'
        target_frame, tick, body = frame(s), int(ticks['encoded']), identity(s)
        completed = wait(lambda n: n.get('boat', {}).get('mechanisms') and identity(n) == body
                         and int(n['assetFixture']['completedSerial']) >= target_frame
                         and int(n['boat']['physicsTicks']['completed']) >= tick, name + ' actual GPU and physics completion')
        report['stages'].append({'name': name, 'process': index, 'state': s, 'completion': {
            'frame': target_frame, 'completedFrame': int(completed['assetFixture']['completedSerial']),
            'tick': tick, 'completedPhysicsTick': int(completed['boat']['physicsTicks']['completed']), 'body': body}})
        print(name, flush=True); persist(); return s

    def export(name):
        library_page(); generation = library()['generation']; choose('Export current boat'); name_with_grid(name)
        path = Path(library()['lastPath'])
        assert path.parent == args.storage_root / 'Designs' / 'Exports' and path.is_file()
        value, raw, parts = blueprint_file(path)
        assert value['name'] == name and library()['generation'] == generation
        copied = args.output / path.name; shutil.copyfile(path, copied)
        report.setdefault('exports', []).append({'name': name, 'source': str(path), 'evidence': copied.name,
            'fileSha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'blueprintSha256': hashlib.sha256(raw).hexdigest()})
        return path, raw, parts

    def camera_gesture(button, axis, field):
        before = read(); pad.button(button, True); pad.axis(axis, .75)
        try:
            after = wait(lambda s: s['workshop']['camera'][field] != before['workshop']['camera'][field], 'actual controller camera ' + field)
        finally:
            pad.axis(axis, 0); pad.button(button, False); fresh()
        after = sample('controller-camera-' + field)
        for value in ('parts', 'revision', 'changed', 'placedParts'):
            assert after['workshop'][value] == before['workshop'][value]
        assert after['session']['inventory'] == before['session']['inventory']

    def saved(name):
        wait(lambda s: s['pause']['canPause'], 'safe pause available'); key('p')
        paused = wait(lambda s: s['pause']['phase'] == 'paused', 'joined physical pause')
        slot = args.storage_root / paused['world']
        previous = archive(slot)[0]['generation'] if (slot / 'current').exists() else 0
        key('F10')
        def acknowledged(s):
            try:
                meta, _ = archive(slot)
                return 'Expedition saved' in controls.title(window) and meta['generation'] > previous and str(meta['tick']) == s['pause']['tick']
            except (OSError, AssertionError): return False
        wait(acknowledged, 'new mirrored world checkpoint acknowledged')
        meta, payload = archive(slot); state = sample(name)
        assert state['boat']['joinedTick'] == str(meta['tick'])
        (args.output / (name + '.svce')).write_bytes(payload)
        return state, meta, payload

    def stop():
        nonlocal child, stream
        if pad: pad.reset()
        killed = False; code = None; requested = False
        if child:
            if child.poll() is None:
                requested = True
                child.send_signal(signal.SIGTERM)
                try: child.wait(timeout=15)
                except subprocess.TimeoutExpired: killed = True; child.kill(); child.wait(timeout=5)
            code = child.returncode
        if stream: stream.close()
        child = stream = None
        path = args.output / f'process-{index}.log'
        if index and index not in checked_logs and path.exists():
            checked_logs.add(index)
            pattern = re.compile(r'uncaptured(?:\s+WebGPU)?(?:\s+GPU)?\s+error|WebGPU validation error|'
                                 r'(?:WGPU|WebGPU)[^\n]{0,100}(?:error|validation failed)|\[(?:error|fatal)\]', re.I)
            errors = [line for line in path.read_text(errors='replace').splitlines() if pattern.search(line)]
            report.setdefault('processChecks', []).append({'process': index, 'exitCode': code,
                'requestedTermination': requested, 'forceKilled': killed, 'errorLines': errors})
            assert requested and code in (0, -signal.SIGTERM) and not killed and not errors, (requested, code, killed, errors)

    def start(world=None):
        nonlocal child, stream, window, index
        index += 1; stream = (args.output / f'process-{index}.log').open('w')
        command = [str(args.binary), '--config', 'salvage_cove.cfg', '--uncapped', '--width', str(WIDTH), '--height', str(HEIGHT),
                   '--expedition-root', str(args.storage_root), '--expedition-observe', str(args.output / f'observation-{index}')]
        if world: command += ['--expedition-world', world]
        child = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT, env={**os.environ, 'VOXY_WINDOW_BACKEND': 'x11'})
        wait(lambda _: controls.own_window(child.pid), 'owned native game window', 60); window = controls.own_window(child.pid)
        # The native menu owner is constructed lazily when B first opens the
        # workshop. Requiring it before that real control would deadlock startup.
        wait(lambda s: s.get('ready') and s.get('boat', {}).get('active') and s.get('gamepad'), 'native builder ready', 60)
        focus(window)
        wait(lambda s: s['gamepad']['connected'] and s['gamepad']['armed'], 'OS controller connected and neutral')
        if world: wait(lambda s: s.get('restore', {}).get('phase') == 'ready' and s['pause']['phase'] == 'paused', 'same world restored paused')

    try:
        controls = Controls()
        for name, args_, result in (
            ('XSetInputFocus', [C.c_void_p, C.c_ulong, C.c_int, C.c_ulong], C.c_int),
            ('XGetInputFocus', [C.c_void_p, C.POINTER(C.c_ulong), C.POINTER(C.c_int)], C.c_int),
            ('XRaiseWindow', [C.c_void_p, C.c_ulong], C.c_int)):
            fn = getattr(controls.x, name); fn.argtypes = args_; fn.restype = result
        pad = VirtualGamepad(); start(); initial = sample('fresh-owned-world')
        assert initial['boat']['parts'] == 11 and initial['boat']['paidPartIds'] == []
        assert initial['boat']['massKg'] == 1035 and initial['session']['inventory']['salvageMaterial'] == '48'
        key('b'); wait(lambda s: s['workshop']['open'], 'keyboard opened builder')
        camera_gesture('rs', 'rx', 'target'); camera_gesture('ls', 'ry', 'distance')
        key('F2'); wait(lambda s: menu(s)['page'] == 'main', 'F2 opened native tools')
        # The starter cradle occupies the useful deck studs. Free that space
        # through the ordinary editor, as in the established building journey.
        choose('Select and group')
        for _ in range(12):
            if read()['workshop']['name'] == 'Cargo cradle': break
            choose('Next part')
        assert read()['workshop']['name'] == 'Cargo cradle'
        choose('Remove selection')
        wait(lambda s: s['workshop']['parts'] == 10 and s['workshop']['changed'] and s['workshop']['valid'], 'cradle removal is valid')
        choose('Keep change')
        sample('cradle-removed-to-free-deck-studs', lambda s: s['workshop']['placedParts'] == 10 and not s['workshop']['changed'])
        _, _, starter_parts = export('HULL')
        assert len(starter_parts) == 10 and library()['rows'] == []
        added = []; total_cost = 0
        for number in (1, 2):
            main_menu(); choose('Choose parts')
            catalog = read()['workshop']['catalog']
            brick = next(c['name'] for c in catalog if re.search(r'1\s*x\s*2', c['name'], re.I))
            choose(brick); assert read()['workshop']['catalogName'] == brick
            total_cost += int(read()['workshop']['partCost'])
            choose('Add chosen part')
            ghost = wait(lambda s: s['workshop']['parts'] == 10 + number and s['workshop']['changed'] and s['workshop']['valid'], 'valid snapped one-shot brick')
            assert not ghost['workshop']['brickTool']
            added.append({'slot': ghost['workshop']['selected'], 'placement': ghost['workshop']['placement'], 'rotation': ghost['workshop']['rotation']})
            if number == 1:
                main_menu(); target('Back to building'); before = read()['workshop']
                pad.button('a', True)
                try:
                    wait(lambda s: not menu(s)['open'] and not s['gamepad']['armed'], 'held confirm disarmed at menu close')
                    focus(controls.root); focus(window)
                    held = sample('held-controller-close-and-focus-guard', lambda s: not menu(s)['open'] and not s['gamepad']['armed'])
                    for value in ('parts', 'placedParts', 'changed', 'revision', 'placement', 'rotation'):
                        assert held['workshop'][value] == before[value], (value, held['workshop'], before)
                    assert held['workshop']['changed'] and held['workshop']['placedParts'] == 10
                finally: pad.button('a', False); fresh()
                wait(lambda s: s['gamepad']['armed'], 'released neutral controller rearmed')
            main_menu(); choose('Keep change')
            sample(f'controller-brick-{number}-kept', lambda s: not s['workshop']['changed'] and s['workshop']['placedParts'] == 10 + number)
        expected_mass = read()['workshop']['massKg']; expected_stock = 48 - total_cost
        main_menu(); choose('Select and group'); choose('Previous part')
        assert read()['workshop']['selected'] == added[0]['slot']
        choose('Toggle next in group')
        assert set(read()['workshop']['selectedParts']) == {p['slot'] for p in added}
        main_menu(); choose('Paint bricks'); choose('Teal'); choose('Keep paint')
        teal = sample('two-brick-group-paint-kept', lambda s: s['workshop']['paint'] == TEAL and not s['workshop']['changed'])
        assert teal['workshop']['selectedCount'] == 2
        choose('Undo draft'); undone = sample('group-paint-undo', lambda s: s['workshop']['paint'] == [255] * 4 and s['workshop']['redoCount'] > 0)
        main_menu(); choose('Redo draft'); redone = sample('group-paint-redo', lambda s: s['workshop']['paint'] == TEAL and not s['workshop']['changed'])
        assert redone['workshop']['parts'] == undone['workshop']['parts'] == 12
        library_page(); choose('Save boat as new'); name_with_grid('TUG')
        assert int(row('TUG')['revision']) == 1 and not row('TUG')['backupAvailable']
        choose('TUG'); choose('Copy with a new name'); name_with_grid('TWIN')
        library_page(); choose('TWIN'); choose('Rename saved design'); name_with_grid('SPARE')
        assert int(row('SPARE')['revision']) == 2 and row('SPARE')['backupAvailable']
        sample('controller-osk-save-copy-rename')
        main_menu(); choose('Paint bricks'); choose('Blue'); choose('Keep paint')
        wait(lambda s: s['workshop']['paint'] == BLUE and not s['workshop']['changed'], 'blue group kept')
        library_page(); choose('TUG'); choose('Update from current boat'); name_with_grid()
        assert int(row('TUG')['revision']) == 2 and row('TUG')['backupAvailable']
        choose('Restore previous version')
        wait(lambda s: library(s)['ready'] and not library(s)['busy'] and any(r['name'] == 'TUG' and int(r['revision']) == 3 for r in library(s)['rows']), 'previous named version restored durably')
        choose('Load into workshop')
        wait(lambda s: menu(s)['page'] == 'main' and s['workshop']['parts'] == 12 and not s['workshop']['changed'], 'restored named design loaded explicitly')
        sample('named-update-backup-restore-load')
        exported, expected_blueprint, final_parts = export('TRANSFER')
        assert len(final_parts) == 12
        initial_records = [p[4:] for p in starter_parts]; remaining = [p[4:] for p in final_parts]
        for part in initial_records: remaining.remove(part)
        assert len(remaining) == 2 and all(list(p[41:45]) == TEAL for p in remaining)
        assert sorted((list(struct.unpack_from('<iii', p, 28)), p[40]) for p in remaining) == sorted((p['placement'], p['rotation']) for p in added)
        imports = args.storage_root / 'Designs' / 'Imports'
        valid = imports / 'transfer.voxy-design.json'; shutil.copyfile(exported, valid)
        damaged = json.loads(exported.read_text()); damaged['name'] = 'CORRUPT'
        damaged['blueprint'] = ('0' if damaged['blueprint'][0] != '0' else '1') + damaged['blueprint'][1:]
        (imports / 'corrupt.voxy-design.json').write_text(json.dumps(damaged) + '\n')
        generation, rows = library()['generation'], library()['rows']
        choose('Import a design file')
        wait(lambda s: library(s)['ready'] and set(library(s)['imports']) == {'corrupt.voxy-design.json', 'transfer.voxy-design.json'}, 'bounded import files discovered')
        choose('corrupt.voxy-design.json')
        refusal = sample('corrupt-exchange-file-refused', lambda s: library(s)['ready'] and not library(s)['busy'] and 'invalid' in library(s)['message'].lower())
        assert library(refusal)['generation'] == generation and library(refusal)['rows'] == rows
        choose('transfer.voxy-design.json')
        wait(lambda s: library(s)['ready'] and any(r['name'] == 'TRANSFER' for r in library(s)['rows']), 'actual exported design imported')
        assert int(row('TRANSFER')['revision']) == 1
        expected_rows = library()['rows']; expected_generation = library()['generation']
        assert int(expected_generation) == 6
        sample('valid-file-imported-without-world-ownership')
        assert read()['boat']['paidPartIds'] == [] and read()['session']['inventory']['salvageMaterial'] == '48'
        assert int(read()['workshop']['charge']) == total_cost
        main_menu(); choose('Launch boat')
        launched = sample('two-brick-owned-launch', lambda s: not s['workshop']['open'] and not s['workshop']['pending'] and s['boat']['parts'] == 12)
        assert launched['boat']['massKg'] == expected_mass and launched['boat']['paidPartIds'] == ['35', '36']
        assert int(launched['session']['inventory']['salvageMaterial']) == expected_stock and launched['workshop']['storedParts'] == 0
        saved_state, saved_meta, payload = saved('builder-world-saved')
        owned, physical_parts = owned_design(payload)
        assert sorted(p[24:65] + p[91:105] for p in physical_parts) == sorted(p[4:] for p in final_parts)
        assert owned['inventory'] == [expected_stock, 0]
        report['savedArchive'] = saved_meta; report['savedOwnedDesign'] = owned
        report['libraryBeforeRestart'] = {'generation': expected_generation, 'rows': expected_rows}
        stop(); start(saved_state['world'])
        restored = sample('same-world-restored-paused')
        restored_meta, restored_payload = archive(args.storage_root / saved_state['world'])
        restored_owned, _ = owned_design(restored_payload)
        assert restored_owned == owned and restored_meta['generation'] > saved_meta['generation']
        assert restored_meta['tick'] == saved_meta['tick'] and int(restored['pause']['tick']) == saved_meta['tick'] + 1
        assert restored['boat']['paidPartIds'] == ['35', '36'] and restored['boat']['massKg'] == expected_mass
        key('p'); wait(lambda s: s['pause']['phase'] == 'running', 'restored world resumed')
        key('b'); wait(lambda s: s['workshop']['open'], 'restored builder opened')
        library_page(); assert library()['rows'] == expected_rows and library()['generation'] == expected_generation
        sample('all-named-designs-and-backups-reloaded')
        choose('TUG'); choose('Load into workshop')
        wait(lambda s: menu(s)['page'] == 'main' and not s['workshop']['changed'] and s['workshop']['parts'] == 12,
             'durable library payload loaded after process restart')
        _, recovered_blueprint, _ = export('REOPEN')
        assert recovered_blueprint == expected_blueprint
        assert read()['workshop']['charge'] == '0' and not read()['workshop']['canLaunch']
        assert read()['workshop']['storedParts'] == 0 and read()['session']['inventory'] == saved_state['session']['inventory']
        main_menu(); choose('Back to building'); key('b')
        wait(lambda s: not s['workshop']['open'], 'unchanged restored builder closed')
        final, final_meta, final_payload = saved('restored-builder-world-saved')
        final_owned, _ = owned_design(final_payload); assert final_owned == owned
        assert final['boat']['paidPartIds'] == saved_state['boat']['paidPartIds']
        report.update(status='passed', restoredArchive=restored_meta, finalArchive=final_meta,
                      finalOwnedDesign=final_owned, expectedMassKg=expected_mass, expectedMaterial=expected_stock,
                      expectedBlueprintSha256=hashlib.sha256(expected_blueprint).hexdigest())
    except Exception as error:
        report.update(status='failed', error=str(error), lastState=read()); raise
    finally:
        try: stop()
        except Exception as error:
            report.update(status='failed', cleanupError=str(error)); raise
        finally:
            if pad: pad.close()
            if controls: controls.close()
            report['elapsedSeconds'] = round(time.monotonic() - began, 3); persist()


if __name__ == '__main__': main()
