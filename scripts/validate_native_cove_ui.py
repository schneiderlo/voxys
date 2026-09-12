#!/usr/bin/env python3
"""Native menus, optional preferences, temporary Test/Return and real New/Load.

One bounded journey through the actual X11/OS-controller input owners. Observer,
preferences and owned saves are read only. No screenshots, injected actions,
archive edits, extra construction matrix, or retries of missed commands.
"""
import argparse
import ctypes as C
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time

from cove_virtual_gamepad import VirtualGamepad
from validate_native_cove_delivery import Controls
from validate_native_cove_character import checkpoint
from validate_native_cove_saves import archive_payload

WIDTH, HEIGHT = 960, 800


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--storage-root', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=420)
    parser.add_argument('--handoffs-only', action='store_true', help='Retained save: Job Resume and paused-workshop Return-to-building only')
    parser.add_argument('--world', help='Existing lowercase world ID for --handoffs-only')
    parser.add_argument('--continue-from', type=Path, help='Failed output with the real world-a-before-test checkpoint; skip its completed menus/settings')
    parser.add_argument('--continue-after-return', action='store_true', help='Reuse the recorded completed physical Return; run only New/Load/quit/restart')

    args = parser.parse_args()
    if not 60 <= args.seconds <= 600: parser.error('seconds must be 60..600')
    args.binary = args.binary.resolve(strict=True)
    args.output = args.output.resolve(); args.storage_root = args.storage_root.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    if args.handoffs_only and (args.continue_from or not args.world or not re.fullmatch('[0-9a-f]{32}',args.world)):
        parser.error('handoffs-only requires one existing --world and no continuation')
    if args.continue_after_return and not args.continue_from: parser.error('after-return requires the recorded prior result')
    prior = json.loads((args.continue_from.resolve(strict=True)/'summary.json').read_text()) if args.continue_from else None
    if args.handoffs_only:
        assert args.storage_root.is_dir()
        handoff_source,handoff_payload=checkpoint(args.storage_root/args.world)
    elif prior:
        assert Path(prior['storageRoot']) == args.storage_root and args.storage_root.is_dir()
    else: args.storage_root.mkdir(parents=True, mode=0o700, exist_ok=False)
    report = dict(status='running', kind=__doc__.splitlines()[0], startedUtc=datetime.now(timezone.utc).isoformat(),
                  binary=str(args.binary), binarySha256=hashlib.sha256(args.binary.read_bytes()).hexdigest(),
                  storageRoot=str(args.storage_root), dimensions=[WIDTH, HEIGHT], stages=[], controls=[], checkpoints=[])
    driver_bytes = Path(__file__).read_bytes()
    (args.output/'executed-driver.py').write_bytes(driver_bytes)
    report['driverSha256'] = hashlib.sha256(driver_bytes).hexdigest()
    report['helperSha256'] = {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
        for name in ('cove_virtual_gamepad.py','validate_native_cove_delivery.py',
                     'validate_native_cove_character.py','validate_native_cove_saves.py')}
    controls = pad = child = stream = window = None
    process = 0; began = time.monotonic(); checked = set()

    def read():
        try: return json.loads((args.output / f'observation-{process}' / 'state.json').read_text())
        except (OSError, json.JSONDecodeError): return {}

    def persist():
        report['elapsedSeconds'] = round(time.monotonic() - began, 3)
        (args.output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')

    def wait(predicate, label, seconds=25):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if time.monotonic() - began > args.seconds: raise RuntimeError('Action budget reached: ' + label)
            if child and child.poll() is not None: raise RuntimeError(f'Native process exited {child.returncode}: {label}')
            state = read(); assert not state.get('failed'), state
            if predicate(state): return state
            time.sleep(.025)
        raise RuntimeError(label + ': ' + json.dumps(read()))

    def frame(s): return int(s.get('assetFixture', {}).get('submittedSerial', 0))
    def fresh():
        before = frame(read())
        return wait(lambda s: frame(s) >= before + 2, 'fresh input-release observation')
    def menu(s=None): return (s or read()).get('nativeMenu', {})
    def page(name): return wait(lambda s: menu(s).get('open') and menu(s).get('page') == name, 'menu page ' + name)
    def focus():
        nonlocal window
        wait(lambda _: controls.own_window(child.pid), 'owned native window', 60)
        window = controls.own_window(child.pid)
        controls.x.XRaiseWindow(controls.display, window)
        controls.x.XSetInputFocus(controls.display, window, 2, 0); controls.x.XFlush(controls.display)
        def owns(_):
            actual, revert = C.c_ulong(), C.c_int()
            controls.x.XGetInputFocus(controls.display, C.byref(actual), C.byref(revert))
            return actual.value == window
        wait(owns, 'actual native window focus'); fresh()
        wait(lambda s: s.get('gamepad', {}).get('connected') and s['gamepad']['armed'], 'OS controller neutral rearm')

    def key(name):
        controls.state(window, name, True)
        try: time.sleep(.20)
        finally: controls.state(window, name, False)
        result = fresh(); report['controls'].append(dict(key=name, frame=frame(result)))
        return result

    def pulse(button):
        wait(lambda s: s.get('gamepad', {}).get('armed'), 'neutral controller before one button press')
        pad.button(button, True)
        try: time.sleep(.20)
        finally: pad.button(button, False)
        result = fresh(); report['controls'].append(dict(button=button, page=menu(result).get('page'), frame=frame(result)))
        return result

    def direction(name):
        axis, amount = {'up': ('dy', -1), 'down': ('dy', 1)}[name]
        wait(lambda s: s.get('gamepad', {}).get('armed'), 'neutral controller before navigation')
        pad.axis(axis, amount)
        try: time.sleep(.09)
        finally: pad.axis(axis, 0)
        result = fresh(); report['controls'].append(dict(direction=name, page=menu(result).get('page'), selected=menu(result).get('selected')))
        return result

    def target(label):
        rows = menu()['rows']; matches = [i for i, row in enumerate(rows) if row['label'] == label]
        assert len(matches) == 1, (label, rows)
        desired = matches[0]; assert rows[desired]['enabled'], (label, menu())
        for _ in range(len(rows) + 1):
            selected = menu()['selected']
            if selected == desired: return
            direction('down' if (desired-selected) % len(rows) <= (selected-desired) % len(rows) else 'up')
        raise RuntimeError('Controller did not reach ' + label)

    def choose(label): target(label); return pulse('a')

    def game_menu():
        if not menu().get('open') or menu().get('page') != 'game': pulse('menu')
        page('game'); wait(lambda s: s['pause']['phase'] == 'paused', 'joined pause menu')
        return read()

    def identity(s):
        boat = s['boat']; motion = boat['mechanisms']
        return (s['world'], boat['physicsTicks']['incarnation'], boat['buildId'], boat['topologyRevision'],
                tuple(row['key'] for row in boat['roots']), motion['bodyIndex'], motion['bodyGeneration'])

    def physical(s, workshop=False, practice=False):
        boat, fixture, hud = s['boat'], s['assetFixture'], s['nativeHud']
        assert boat['active'] and boat['parts'] == 11 and boat['massKg'] == 1035, boat
        assert s['practice']['phase'] == ('running' if practice else 'none')
        if practice:
            # The rendered trial uses isolated prototype identities. These are
            # not purchases in the frozen authoritative expedition archive.
            assert boat['buildId'] != str(int.from_bytes(bytes.fromhex(before_test['owned']['builds'][0]['id'])[16:24], 'little'))
            assert len(boat['paidPartIds']) == 11 and len(set(boat['paidPartIds'])) == 11
        else: assert boat['paidPartIds'] == [], boat
        assert s['session']['inventory'] == {'salvageMaterial': '48', 'specialMachinery': '0'}
        assert s['workshop']['open'] == workshop
        if workshop:
            # Blit last-use is stale when workshop composition bypasses it.
            assert fixture['sceneryDraws'] == 0 and fixture['dockMarkingDraws'] == 0
        else: assert fixture['sceneSunShadows']
        assert fixture['environmentReady'] and fixture['presentationParts'] == 9
        assert int(fixture['gpuReservationBytes']) <= 16*1024*1024
        assert boat['physicsTicks']['supported'] and not boat['physicsTicks']['failed']
        assert boat['mechanisms']['bodyIndex'] > 0 and boat['mechanisms']['bodyGeneration'] > 0
        assert hud['enabled'] and hud['lastEncodedQuads'] > 0 and hud['bodyPixels'] >= 16 and not hud['truncated'], hud
        x, y, width, height = hud['panel']
        assert 0 <= x and 0 <= y and x + width <= WIDTH and y + height <= HEIGHT

    def record(name, predicate=lambda _: True, workshop=False, practice=False):
        state = wait(lambda s: s.get('ready') and s.get('boat', {}).get('active')
                     and s.get('workshop', {}).get('open') == workshop
                     and (workshop or s.get('assetFixture', {}).get('sceneSunShadows'))
                     and s.get('practice', {}).get('phase') == ('running' if practice else 'none') and predicate(s), name)
        physical(state, workshop, practice); body = identity(state); submitted = frame(state); tick = int(state['boat']['physicsTicks']['encoded'])
        completed = wait(lambda s: s.get('boat', {}).get('mechanisms') and identity(s) == body
                         and s['workshop']['open'] == workshop and s['practice']['phase'] == state['practice']['phase']
                         and int(s['assetFixture']['completedSerial']) >= submitted
                         and int(s['boat']['physicsTicks']['completed']) >= tick, name + ' actual completion')
        report['stages'].append(dict(name=name, process=process, state=state, completion=dict(identity=body,
            submittedFrame=submitted, completedFrame=int(completed['assetFixture']['completedSerial']),
            encodedTick=tick, completedTick=int(completed['boat']['physicsTicks']['completed']))))
        print(name, flush=True); persist(); return state

    def bind_saved_identity(state, metadata, payload):
        build = metadata['owned']['builds'][0]
        assert state['world'] == metadata['world']
        assert state['boat']['buildId'] == str(int.from_bytes(bytes.fromhex(build['id'])[16:24], 'little'))
        assert state['boat']['topologyRevision'] == str(int.from_bytes(bytes.fromhex(build['revision']), 'little'))
        assert [row['key'] for row in state['boat']['roots']] == archive_payload(payload, metadata['world'])['rootKeys']

    def prefs(): return json.loads((args.storage_root / 'cove-preferences.json').read_text())
    def binding(action): return next(row for row in prefs()['bindings'] if row['action'] == action)
    def saved(name):
        game_menu(); state = read(); slot = args.storage_root / state['world']
        prior = checkpoint(slot)[0]['generation'] if (slot / 'current').exists() else 0
        choose('Save expedition')
        def acknowledged(s):
            try:
                meta, _ = checkpoint(slot)
                return (s.get('world') == meta['world'] and meta['generation'] > prior
                        and str(meta['tick']) == s['pause']['tick'] and 'Expedition saved' in controls.title(window))
            except (OSError, AssertionError): return False
        wait(acknowledged, name + ' mirrored save and actual host acknowledgment')
        meta, payload = checkpoint(slot)
        (args.output / (name + '.svce')).write_bytes(payload)
        report['checkpoints'].append(dict(name=name, metadata=meta)); persist()
        captured = record(name); bind_saved_identity(captured, meta, payload); return meta

    def stop(expected_quit=False):
        nonlocal child, stream
        if pad: pad.reset()
        requested = killed = False; code = None
        if child:
            if child.poll() is None:
                requested = True; child.send_signal(signal.SIGTERM)
                try: child.wait(timeout=15)
                except subprocess.TimeoutExpired: killed = True; child.kill(); child.wait(timeout=5)
            code = child.returncode
        if stream: stream.close()
        child = stream = None
        path = args.output / f'process-{process}.log'
        if process and process not in checked and path.exists():
            checked.add(process)
            pattern = re.compile(r'uncaptured(?:\s+WebGPU)?(?:\s+GPU)?\s+error|WebGPU validation error|'
                                 r'(?:WGPU|WebGPU)[^\n]{0,100}(?:error|validation failed)|Abandoning undrained authored shape resources|\[(?:error|fatal)\]', re.I)
            errors = [line for line in path.read_text(errors='replace').splitlines() if pattern.search(line)]
            report.setdefault('processChecks', []).append(dict(process=process, exitCode=code,
                requestedTermination=requested, forceKilled=killed, errorLines=errors))
            assert ((not requested and code == 0) if expected_quit else (requested and code in (0, -signal.SIGTERM))) and not killed and not errors, (requested, code, killed, errors)

    def start(world=None):
        nonlocal child, stream, process
        process += 1; stream = (args.output / f'process-{process}.log').open('w')
        command = [str(args.binary), '--config', 'salvage_cove.cfg', '--uncapped', '--width', str(WIDTH), '--height', str(HEIGHT),
                   '--expedition-root', str(args.storage_root), '--expedition-observe', str(args.output / f'observation-{process}')]
        if world: command += ['--expedition-world', world]
        child = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT, env={**os.environ, 'VOXY_WINDOW_BACKEND': 'x11'})
        wait(lambda s: s.get('ready') and s.get('boat', {}).get('active') and s.get('nativeMenu'), 'native Cove ready', 60)
        if world: wait(lambda s: s.get('world') == world and s.get('restore', {}).get('phase') == 'ready' and s['pause']['phase'] == 'paused', 'requested world restored', 60)
        focus()

    def transition(expected=None, previous=None):
        # Staged host admission closes the old window only after Leave drains.
        # Wait for the new observer's world before targeting its new window.
        wait(lambda s: s.get('ready') and s.get('boat', {}).get('active') and s.get('nativeMenu')
             and (s.get('world') == expected if expected else s.get('world') != previous), 'in-process expedition handoff', 60)
        if expected: wait(lambda s: s.get('restore', {}).get('phase') == 'ready' and s['pause']['phase'] == 'paused', 'selected saved world restored', 60)
        focus()

    try:
        controls = Controls()
        for name, args_, result in (
            ('XSetInputFocus', [C.c_void_p, C.c_ulong, C.c_int, C.c_ulong], C.c_int),
            ('XGetInputFocus', [C.c_void_p, C.POINTER(C.c_ulong), C.POINTER(C.c_int)], C.c_int),
            ('XRaiseWindow', [C.c_void_p, C.c_ulong], C.c_int)):
            fn = getattr(controls.x, name); fn.argtypes = args_; fn.restype = result
        pad = VirtualGamepad()
        if args.handoffs_only:
            world_a=args.world;before_test=handoff_source
            report['handoffSource']=handoff_source
            start(world_a);ready=record('saved-world-ready-for-menu-handoffs')
            before_test,handoff_payload=checkpoint(args.storage_root/world_a)
            assert before_test['owned']==handoff_source['owned']
            bind_saved_identity(ready,before_test,handoff_payload)
            game_menu();choose('Job board');page('job')
            assert read()['pause']['phase']=='paused'
            assert not next(row for row in menu()['rows'] if row['label']=='Accept job')['enabled']
            choose('Resume expedition')
            resumed=record('job-resume-retains-page-and-enables-real-permission',lambda state:
                state['pause']['phase']=='running' and menu(state).get('page')=='job'
                and any(row['label']=='Accept job' and row['enabled'] for row in menu(state)['rows']))
            assert resumed['job']['phase']=='available'  # Never accepts the job for this UI check.
            game_menu();choose('Resume and open workshop')
            wait(lambda state:state['workshop']['open'] and state['practice']['canBegin'],'original workshop ready for handoff test')
            pulse('menu');page('main');original_workshop=read()['workshop'];choose('Test sail (temporary copy)')
            record('handoff-setup-temporary-test',lambda state:state['pause']['phase']=='running',practice=True)
            game_menu();choose('Return from test sail')
            returned=record('handoff-returned-paused-workshop',lambda state:
                state['pause']['phase']=='paused' and state['practice']['returns']=='1',workshop=True)
            for field in ('parts','revision','placement','rotation','selected','changed','placedParts'):
                assert returned['workshop'][field]==original_workshop[field]
            game_menu();choose('Return to building')
            editing=record('paused-workshop-resume-keeps-editor-open',lambda state:
                state['pause']['phase']=='running' and not menu(state)['open'],workshop=True)
            assert editing['practice']['returns']=='1' and not editing['workshop']['changed']
            pulse('view');wait(lambda state:not state['workshop']['open'],'leave resumed editor')
            game_menu();choose('Quit game...');page('confirm');target('Confirm')
            pad.button('a',True);time.sleep(.20);pad.button('a',False)
            child.wait(timeout=25);stop(expected_quit=True)
            assert checkpoint(args.storage_root/world_a)[0]['envelopeSha256']==before_test['envelopeSha256']
            report['unchangedSavedOwnership']=before_test['owned']
        else:
            if prior:
                assert prior['status'] == 'failed' and len(prior['checkpoints']) == 1
                before_test = prior['checkpoints'][0]['metadata']; assert prior['checkpoints'][0]['name'] == 'world-a-before-test'
                world_a = before_test['world']; disk_meta, disk_payload = checkpoint(args.storage_root/world_a)
                assert disk_meta['envelopeSha256'] == before_test['envelopeSha256']
                chosen_preferences = prior['preferences']['value']; assert prefs() == chosen_preferences
                report['preferences'] = prior['preferences']
                report['continuation'] = dict(source=str(args.continue_from.resolve()),
                    sourceSummarySha256=hashlib.sha256((args.continue_from/'summary.json').read_bytes()).hexdigest(),
                    completedStages=[stage['name'] for stage in prior['stages']],
                    meaning='Resume the actual durable world and optional preferences; do not replay the verified menu/settings/jump/save prefix.')
                start(world_a)
                resumed = record('retained-world-a-ready-for-test-continuation')
                current_meta, current_payload = checkpoint(args.storage_root/world_a)
                assert current_meta['owned'] == before_test['owned']
                bind_saved_identity(resumed,current_meta,current_payload)
                # A normal successful resume republishes owned physics handles.
                before_test=current_meta;report['checkpoints'].append(dict(name='world-a-before-test',metadata=before_test))
                game_menu()
            else:
                start(); first = record('fresh-owned-world'); world_a = first['world']
                game_menu(); choose('Job board'); page('job'); job = record('controller-job-board')
                assert 'generator' in menu(job)['title'].lower()
                pulse('b'); choose('Nearby map'); page('map'); mapped = record('controller-nearby-map')
                assert {'dock', 'boat', 'generator'} <= {row['id'] for row in mapped['landmarks']}
                assert any('Boarding dock:' in row['label'] for row in menu(mapped)['rows'])
                pulse('b'); choose('Inventory'); page('inventory'); inventory = record('controller-owned-inventory')
                assert any(row['label'] == 'Material: 48' for row in menu(inventory)['rows'])
                pulse('b'); choose('Help'); page('help'); record('controller-context-help')
                pulse('b'); choose('Controls and accessibility'); page('accessibility')
                choose('Text size: 100%'); choose('Text size: 125%'); choose('High contrast: Off')
                choose('Stick speed: 100%'); choose('Move deadzone: 20%'); choose('Look deadzone: 20%')
                choose('Invert camera Y: Off'); choose('Winch: Hold to run'); choose('Mouse orbit: Hold to turn')
                choose('Remap controls'); page('bindings'); choose('Jump: Space'); choose('Primary: Space')
                choose('Choose from key list'); choose('T'); choose('Apply binding')
                assert binding('jump')['key'] == 84
                chosen_preferences = prefs(); assert chosen_preferences['textScale'] == 1.5 and chosen_preferences['highContrast']
                assert chosen_preferences['invertY'] and chosen_preferences['reelToggle'] and chosen_preferences['orbitToggle']
                assert chosen_preferences['padSensitivity'] != 1 and chosen_preferences['moveDeadzone'] != .2 and chosen_preferences['lookDeadzone'] != .2
                settings = record('controller-settings-and-key-picker-applied')
                assert settings['ui']['textScale'] == 1.5 and settings['ui']['highContrast']
                assert next(row for row in settings['ui']['actions'] if row['id'] == 'jump')['keyLabel'] == 'T'
                report['preferences'] = dict(value=chosen_preferences, sha256=hashlib.sha256((args.storage_root/'cove-preferences.json').read_bytes()).hexdigest())
                for expected_page in ('bindings', 'accessibility', 'game'): pulse('b'); page(expected_page)
                choose('Resume'); wait(lambda s: s['pause']['phase'] == 'running' and not menu(s)['open'], 'actual Resume')
                wait(lambda s: s['player']['mode'] == 'walking', 'settled robot before rebound jump')
                controls.state(window, 't', True)
                try: jumped = wait(lambda s: s['player']['mode'] == 'airborne' and s['character']['clip'] in ('jump','fall'), 'actual rebound T jump', 5)
                finally: controls.state(window, 't', False)
                fresh(); report['reboundJump'] = jumped
                wait(lambda s: s['player']['mode'] == 'walking', 'land after rebound jump')
                before_test = saved('world-a-before-test')
            if args.continue_after_return:
                completed_return = prior.get('reusedReturnProof',{}).get('state',prior['lastState']); physical(completed_return,workshop=True)
                assert completed_return['practice']['returns'] == '1' and completed_return['pause']['phase'] == 'paused'
                ticks=completed_return['boat']['physicsTicks']
                assert ticks['scheduled'] == ticks['encoded'] == ticks['submitted'] == ticks['completed'] == completed_return['boat']['joinedTick']
                bind_saved_identity(completed_return,before_test,checkpoint(args.storage_root/world_a)[1])
                report['reusedReturnProof'] = dict(source=prior.get('reusedReturnProof',{}).get('source',str(args.continue_from.resolve())),
                    meaning='The previous run completed the actual physical Return. Its last-use shadow flag was an invalid harness oracle; no new frame-completion claim is invented.',
                    state=completed_return)
                after_test=before_test
            else:
                choose('Resume and open workshop'); wait(lambda s: s['workshop']['open'] and s['practice']['canBegin'], 'actual workshop can test')
                pulse('menu'); page('main'); workshop_before = read()['workshop']; choose('Test sail (temporary copy)')
                tested = record('temporary-test-running', lambda s: s['practice']['phase'] == 'running' and s['pause']['phase'] == 'running' and not s['workshop']['open'], practice=True)
                assert tested['world'] == world_a and tested['practice']['runs'] == '1'
                # A real short movement tick window makes this more than a menu transition.
                player_tick = int(tested['player']['tick']); feet_before = tested['player']['feet']
                pad.axis('ly', -.65)
                try: wait(lambda s: int(s['player']['tick']) >= player_tick + 12 and s['player']['feet'] != feet_before, 'temporary test accepts actual movement')
                finally: pad.axis('ly', 0)
                released_tick = int(read()['player']['tick'])
                wait(lambda s: int(s['player']['tick']) > released_tick, 'fresh movement release packet')
                game_menu(); assert not next(row for row in menu()['rows'] if row['label'] == 'Save expedition')['enabled']
                assert checkpoint(args.storage_root/world_a)[0]['envelopeSha256'] == before_test['envelopeSha256']
                choose('Return from test sail')
                returned = record('test-return-original-workshop-paused', lambda s: not s['practice']['active'] and s['practice']['returns'] == '1' and s['workshop']['open'] and s['pause']['phase'] == 'paused', workshop=True)
                for field in ('parts','revision','placement','rotation','selected','changed','placedParts'):
                    assert returned['workshop'][field] == workshop_before[field], (field, returned['workshop'], workshop_before)
                assert checkpoint(args.storage_root/world_a)[0]['envelopeSha256'] == before_test['envelopeSha256']
                game_menu(); choose('Return to building'); wait(lambda s: s['pause']['phase'] == 'running' and s['workshop']['open'] and not menu(s)['open'], 'paused workshop has real Resume path')
                pulse('view'); wait(lambda s: not s['workshop']['open'], 'close original workshop')
                after_test = saved('world-a-after-test'); assert after_test['owned'] == before_test['owned']
            choose('Expeditions'); choose('New expedition...'); page('confirm')
            assert menu()['rows'][menu()['selected']]['label'] == 'Cancel' and read()['world'] == world_a
            choose('Cancel'); assert read()['world'] == world_a
            choose('New expedition...'); page('confirm'); target('Confirm')
            # The replacement can reset serials. Release physically, then use world identity, not fresh().
            pad.button('a', True); time.sleep(.20); pad.button('a', False); transition(previous=world_a)
            second = record('confirmed-new-expedition-same-process'); world_b = second['world']; assert world_b != world_a
            assert prefs() == chosen_preferences and second['ui']['textScale'] == 1.5
            saved('world-b-own-throwaway-save'); choose('Expeditions'); choose('Load saved Cove'); page('worlds')
            other = [row['label'] for row in menu()['rows'] if row['label'].startswith('Saved Cove ') and '(current)' not in row['label']]
            assert len(other) == 1, menu(); choose(other[0]); page('confirm'); target('Confirm')
            pad.button('a', True); time.sleep(.20); pad.button('a', False); transition(expected=world_a)
            loaded = record('confirmed-saved-cove-reloaded-in-process')
            loaded_meta, loaded_payload = checkpoint(args.storage_root/world_a)
            bind_saved_identity(loaded, loaded_meta, loaded_payload)
            assert loaded['world'] == world_a and loaded_meta['owned'] == after_test['owned']
            assert prefs() == chosen_preferences
            # Quit a second RAM-only practice through the real confirmation screen.
            # The original durable slot must stay exact, then restart it once.
            game_menu(); choose('Resume and open workshop')
            wait(lambda s: s['workshop']['open'] and s['practice']['canBegin'], 'saved original can enter disposable test')
            pulse('menu'); page('main'); pre_quit_meta, _ = checkpoint(args.storage_root/world_a)
            choose('Test sail (temporary copy)')
            record('second-ram-test-before-actual-quit', lambda s: s['practice']['phase'] == 'running' and s['pause']['phase'] == 'running', practice=True)
            game_menu(); choose('Quit game...'); page('confirm'); target('Confirm')
            pad.button('a', True); time.sleep(.20); pad.button('a', False)
            child.wait(timeout=25); stop(expected_quit=True)
            assert checkpoint(args.storage_root/world_a)[0]['envelopeSha256'] == pre_quit_meta['envelopeSha256']
            report['quitPractice'] = dict(confirmedQuit=True, savedEnvelopeUnchanged=True,
                                          sourceEnvelopeSha256=pre_quit_meta['envelopeSha256'])
            start(world_a); restored = record('process-restart-retains-preferences-and-owned-save')
            restored_meta, restored_payload = checkpoint(args.storage_root/world_a)
            bind_saved_identity(restored, restored_meta, restored_payload)
            assert restored_meta['owned'] == after_test['owned'] and not restored['practice']['active']
            assert restored['ui']['textScale'] == 1.5 and restored['ui']['highContrast'] and prefs() == chosen_preferences
            assert next(row for row in restored['ui']['actions'] if row['id'] == 'jump')['keyLabel'] == 'T'
            game_menu(); choose('Controls and accessibility'); page('accessibility')
            labels = {row['label'] for row in menu()['rows']}
            assert {'Invert camera Y: On','Winch: Press to start / stop','Mouse orbit: Press to start / stop'} <= labels
            record('reloaded-settings-visible-through-controller')
        report['status'] = 'passed'
    except BaseException as error:
        report['status'] = 'failed'; report['error'] = repr(error); report['lastState'] = read()
        raise
    finally:
        try: stop()
        except BaseException as cleanup_error:
            report['status'] = 'failed'; report['cleanupError'] = repr(cleanup_error)
        if pad: pad.close()
        if controls: controls.close()
        persist()
    if report['status'] != 'passed': raise RuntimeError(report.get('cleanupError', report.get('error')))


if __name__ == '__main__': main()
