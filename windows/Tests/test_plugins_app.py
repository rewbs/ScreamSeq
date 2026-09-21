"""Guarded rack edits and actual native controls on an owned, hidden desktop."""
import ctypes
import base64
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'windows/Api'))
from client import Client, ApiError, TransportError
from private_desktop import PrivateDesktop, user


class PluginAppTests(unittest.TestCase):
    def setUp(self):
        self.folder = Path(self.enterContext(tempfile.TemporaryDirectory(prefix='plugins-ui-', dir=os.environ['TMPDIR'])))
        self.desktop = self.enterContext(PrivateDesktop())
        self.report = self.folder / 'wasapi.json'
        args = [os.environ['SCREAMSEQ_TEST_EXE'], '--inspection', '--automation', '--seconds', '120', '--report', str(self.report)]
        cache = os.environ.get('SCREAMSEQ_TEST_INSTRUMENT_CACHE') if self._testMethodName == 'test_installed_instrument_trigger_native_controls_and_reopen' else os.environ.get('SCREAMSEQ_TEST_PLUGIN_CACHE')
        if cache:
            args += ['--vst3-test-cache', cache]
        self.pid = self.desktop.launch(args)
        self.client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(self.pid), timeout=20)
        for _ in range(100):
            try:
                self.client.call('document.get')
                break
            except TransportError:
                time.sleep(.1)
        else:
            self.fail('owned app did not publish its pipe')
        self.hwnd = self.desktop.hwnd(self.pid)
        self.catalog = self.client.call('plugin.discover', {'format': 'Built-in'})['data']
        self.assertGreater(len(self.catalog), 1)

    def write(self, method, **fields):
        return self.client.call(method, dict(expectedRevision=self.doc()['revision'], **fields))

    def doc(self):
        return self.client.call('document.get')

    def rack(self):
        return self.doc()['data']['nativePlugins']

    def add(self, index=0):
        self.write('plugin.add', descriptor=self.catalog[index])

    def parameters(self, slot=0):
        return self.client.call('plugin.parameters.get', {'slot': slot})['data']

    def state(self, slot=0):
        return self.client.call('plugin.state.get', {'slot': slot})['data']['data']

    def command(self, control_id):
        self.desktop.send(user.GetDlgItem(self.hwnd, control_id), 0xF5)

    def tick(self):
        self.desktop.send(self.hwnd, 0x113, 1)

    def test_empty_trigger_conversion_guards_history_and_persistence(self):
        before = self.doc()
        self.assertEqual(before['data']['instruments'], [])
        sample_count = len(before['data']['samples'])
        predicted = self.write('instrument.create', empty=True, name='Lead trigger', dryRun=True)['data']['instrument']
        self.assertEqual(predicted, sample_count + 1)
        self.assertEqual(self.doc(), before)
        for params in [dict(empty=True, sample=1), dict(empty='yes'), dict(name='Wrong mode'), dict(dryRun=True), dict(empty=True, name='x' * 201)]:
            with self.subTest(params=params), self.assertRaises(ApiError):
                self.write('instrument.create', **params)
            self.assertEqual(self.doc(), before)
        created = self.write('instrument.create', empty=True, name='Lead trigger')['data']['instrument']
        self.assertEqual(created, predicted)
        def info(slot):
            return self.client.call('instrument.get', {'instrument': slot})['data']
        for slot in range(1, sample_count + 1):
            self.assertEqual(set(info(slot)['mapping']), {slot})
        self.assertEqual(set(info(created)['mapping']), {0})
        self.assertEqual(info(created)['name'], 'Lead trigger')
        instruments = self.doc()['data']['instruments']
        self.write('history.undo', domain='document')
        self.assertEqual(self.doc()['data']['instruments'], [])
        self.write('history.redo', domain='document')
        self.assertEqual(self.doc()['data']['instruments'], instruments)
        path = self.folder / 'empty-trigger.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path))
        self.assertEqual(self.doc()['data']['instruments'], instruments)
        self.assertEqual(set(info(created)['mapping']), {0})

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_INSTRUMENT_CACHE'), 'opt-in installed instrument cache')
    def test_installed_instrument_trigger_native_controls_and_reopen(self):
        descriptor = next(d for d in self.client.call('plugin.discover', {'format': 'VST3'})['data'] if d['isInstrument'])
        self.write('plugin.add', descriptor=descriptor)
        plugin_id = self.rack()[0]['instanceID']
        self.tick()
        self.command(317)  # Native New trigger action: create, then assign.
        self.tick()
        assigned = self.rack()[0]['instrument']
        self.assertGreater(assigned, 0)
        self.assertEqual(set(self.client.call('instrument.get', {'instrument': assigned})['data']['mapping']), {0})
        instruments = self.doc()['data']['instruments']
        self.write('history.undo', domain='plugins')
        self.assertEqual(self.rack()[0]['instrument'], 0)
        self.assertEqual(self.doc()['data']['instruments'], instruments)
        self.write('history.redo', domain='plugins')
        self.assertEqual(self.rack()[0]['instrument'], assigned)
        document = self.doc()['data']
        rows = next(p['rows'] for p in document['patterns'] if p['index'] == 0)
        cells = [dict(pattern=0, row=row, channel=channel, note=0, instrument=0,
                      volumeCommand=0, volume=0, effect=0, parameter=0)
                 for row in range(rows) for channel in range(document['channels'])]
        cells[0].update(note=61, instrument=assigned)
        cells[4 * document['channels']].update(note=255)  # Tracker key-off.
        self.write('pattern.apply', cells=cells)
        self.write('plugin.editor.open', slot=0)
        self.tick()
        self.write('plugin.editor.close', slot=0)
        self.assertEqual(self.doc()['data']['openPluginEditors'], 0)
        path = self.folder / 'instrument-trigger.screamseq'
        self.write('document.save', path=str(path))
        saved = self.state()
        self.write('document.open', path=str(path))
        self.assertEqual(self.rack()[0]['instanceID'], plugin_id)
        self.assertEqual(self.rack()[0]['instrument'], assigned)
        self.assertEqual(self.doc()['data']['instruments'], instruments)
        self.assertEqual(self.state(), saved)
        if os.environ.get('SCREAMSEQ_PLUGIN_EVIDENCE_DIR'):
            folder = Path(os.environ['SCREAMSEQ_PLUGIN_EVIDENCE_DIR'])
            folder.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, folder / 'instrument-trigger.screamseq')
            (folder / 'instrument-app.json').write_text(json.dumps(dict(
                executableSHA256=hashlib.sha256(Path(os.environ['SCREAMSEQ_TEST_EXE']).read_bytes()).hexdigest(),
                descriptor=descriptor, instrument=assigned, pluginID=plugin_id,
                emptyKeymap=True, nativeButton=True, independentHistory=True, exactBaselineReopened=True), indent=2))

    def test_discovery_guards_atomic_parameters_and_independent_histories(self):
        methods = self.client.call('api.describe')['data']
        self.assertIn('plugin.parameters.set', methods['writes'])
        before = self.doc()
        self.write('history.undo', domain='document')  # Empty history is a no-op.
        self.write('plugin.add', descriptor=self.catalog[0], dryRun=True)
        self.assertEqual(self.doc(), before)
        self.add()
        added = self.doc()
        stable = self.rack()[0]['instanceID']
        self.assertTrue(added['data']['canUndoPlugins'])
        self.write('plugin.bypass', slot=0, bypass=False)
        self.assertEqual(self.doc(), added)
        with self.assertRaises(ApiError) as error:
            self.client.call('plugin.remove', {'slot': 0, 'expectedRevision': before['revision']})
        self.assertEqual(error.exception.code, -32001)
        parameters = self.parameters()
        p = next(p for p in parameters if p['writable'] and p['max'] > p['min'])
        new_value = p['min'] if p['value'] != p['min'] else p['max']
        original = self.state()
        for values in [[{'id': p['id'], 'value': new_value}, {'id': 4294967295, 'value': 0}],
                       [{'id': p['id'], 'value': new_value}, {'id': p['id'], 'value': p['value']}],
                       [{'id': p['id'], 'value': p['max'] + 1}]]:
            with self.subTest(values=values):
                with self.assertRaises(ApiError):
                    self.write('plugin.parameters.set', slot=0, values=values)
                self.assertEqual(self.doc(), added)
                self.assertEqual(self.state(), original)
        self.write('plugin.parameters.set', slot=0, values=[{'id': p['id'], 'value': new_value}], dryRun=True)
        self.assertEqual(self.doc(), added)
        self.write('document.patch', title='Independent document edit')
        self.write('plugin.parameters.set', slot=0, values=[{'id': p['id'], 'value': new_value}])
        changed = self.state()
        self.assertNotEqual(changed, original)
        self.assertEqual(self.rack()[0]['instanceID'], stable)
        self.write('history.undo', domain='plugins')
        self.assertEqual(self.state(), original)
        self.assertEqual(self.doc()['data']['title'], 'Independent document edit')
        self.write('history.redo', domain='plugins')
        self.assertEqual(self.state(), changed)
        self.write('history.undo', domain='document')
        self.assertEqual(self.state(), changed)
        self.assertNotEqual(self.doc()['data']['title'], 'Independent document edit')

    def test_move_remove_state_restore_and_save_reopen(self):
        self.add(); self.add(1)
        ids = [p['instanceID'] for p in self.rack()]
        initial = self.state(0)
        self.write('plugin.move', slot=0, direction=1)
        self.assertEqual([p['instanceID'] for p in self.rack()], list(reversed(ids)))
        self.write('plugin.bypass', slot=1, bypass=True)
        path = self.folder / 'rack.screamseq'
        self.write('document.save', path=str(path))
        recipe = plistlib.loads(path.read_bytes())['plugins']
        self.assertFalse(self.client.call('context.get')['data']['dirty'])
        before = self.doc()
        for data in ['not base64!', 'YWJj']:
            with self.assertRaises(ApiError):
                self.write('plugin.state.set', slot=1, data=data)
            self.assertEqual(self.doc(), before)
        self.write('plugin.remove', slot=1)
        self.write('history.undo', domain='plugins')
        self.assertEqual(self.rack()[1]['instanceID'], ids[0])
        self.assertEqual(self.state(1), initial)
        self.write('document.open', path=str(path), discard=True)
        self.assertFalse(self.doc()['data']['canUndoPlugins'])
        self.assertTrue(self.rack()[1]['bypass'])
        again = self.folder / 'again.screamseq'
        self.write('document.save', path=str(again))
        self.assertEqual(plistlib.loads(again.read_bytes())['plugins'], recipe)

    def test_native_rack_buttons_and_stale_parameter_draft(self):
        self.tick()
        self.command(302)  # Add the library's selected built-in.
        self.assertEqual(len(self.rack()), 1)
        self.tick()
        original = self.state()
        p = self.parameters()[0]
        value = p['min'] if p['value'] != p['min'] else p['max']
        field = user.GetDlgItem(self.hwnd, 312)
        text = ctypes.create_unicode_buffer(str(value))
        self.desktop.send(field, 0xC, 0, ctypes.addressof(text))
        self.write('document.patch', title='Changed while typing')
        self.command(313)  # Stale draft must remain unapplied.
        self.assertEqual(self.state(), original)
        retained = ctypes.create_unicode_buffer(64)
        self.desktop.send(field, 0xD, 64, ctypes.addressof(retained))
        self.assertEqual(retained.value, str(value))
        self.desktop.send(self.hwnd, 0x100, 0x1B)  # Escape discards only the draft.
        self.tick()
        self.desktop.send(field, 0xC, 0, ctypes.addressof(text))
        self.command(313)
        self.assertNotEqual(self.state(), original)
        self.command(309)
        self.assertEqual(self.state(), original)
        self.command(310)
        self.assertNotEqual(self.state(), original)
        self.command(305)
        self.assertTrue(self.rack()[0]['bypass'])
        self.command(306)
        self.assertEqual(self.rack(), [])
        self.command(309)
        self.assertEqual(len(self.rack()), 1)

    def test_unavailable_plugin_opaque_state_and_removed_reference_survive(self):
        self.add()
        path = self.folder / 'unavailable.screamseq'
        self.write('document.save', path=str(path))
        tree = plistlib.loads(path.read_bytes())
        first = tree['plugins'][0]
        stable = first['instanceID']
        first.update(format='AU', type=int.from_bytes(b'aufx', 'big'), classID='', state=b'opaque-mac-state')
        first['futureField'] = {'value': b'unknown bytes'}
        native = tree['native']
        lane_id = 'n' + str(native['nextID'])
        native['nextID'] += 1
        native['automation'] = [dict(id=lane_id, pattern=native['patterns'][0][1]['id'], plugin=stable,
            parameter=0, enabled=True, points=[[0, .5, 1]])]
        # Use a valid current template from a live project for unrelated fields.
        path.write_bytes(plistlib.dumps(tree, fmt=plistlib.FMT_BINARY))
        self.write('document.open', path=str(path))
        self.write('plugin.bypass', slot=0, bypass=True)
        self.write('plugin.remove', slot=0)
        removed = self.folder / 'removed.screamseq'
        self.write('document.save', path=str(removed))
        saved = plistlib.loads(removed.read_bytes())
        self.assertEqual(saved['native']['automation'], tree['native']['automation'])
        self.write('history.undo', domain='plugins')
        restored = self.folder / 'restored.screamseq'
        self.write('document.save', path=str(restored))
        self.assertEqual(plistlib.loads(restored.read_bytes())['plugins'][0]['futureField'], first['futureField'])
        self.write('document.open', path=str(removed))
        self.assertEqual(self.rack(), [])

    def test_instrument_alias_noop_channel_history_and_persistence(self):
        self.write('instrument.create', sample=1)
        self.write('instrument.create', sample=2)
        self.add()
        path = self.folder / 'aliases.screamseq'
        self.write('document.save', path=str(path))
        tree = plistlib.loads(path.read_bytes())
        plugin = tree['plugins'][0]
        plugin.update(format='AU', type=int.from_bytes(b'aumu', 'big'), classID='',
                      isInstrument=True, state=b'opaque-instrument', instrument=1,
                      instrumentAssignments=[dict(instrument=1, channel=1), dict(instrument=2, channel=9)])
        path.write_bytes(plistlib.dumps(tree, fmt=plistlib.FMT_BINARY))
        self.write('document.open', path=str(path))
        before = self.doc()
        self.write('instrument.plugin.set', instrument=1, plugin=plugin['instanceID'], channel=1)
        self.assertEqual(self.doc(), before)
        self.write('instrument.plugin.set', instrument=1, plugin=plugin['instanceID'], channel=3)
        aliases = self.client.call('plugin.instruments.get', {'plugin': plugin['instanceID']})['data']['assignments']
        self.assertEqual([(a['instrument'], a['channel']) for a in aliases], [(1, 3), (2, 9)])
        self.write('history.undo', domain='plugins')
        restored = self.folder / 'aliases-restored.screamseq'
        self.write('document.save', path=str(restored))
        self.assertEqual(plistlib.loads(restored.read_bytes())['plugins'], tree['plugins'])

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PLUGIN_CACHE'), 'opt-in installed VST3 cache')
    def test_installed_plugins_editor_parameter_state_remove_undo_reopen(self):
        descriptors = self.client.call('plugin.discover', {'format': 'VST3'})['data']
        self.assertGreater(len(descriptors), 0)
        for descriptor in descriptors:
            with self.subTest(plugin=descriptor['name']):
                self.write('plugin.add', descriptor=descriptor)
                stable = self.rack()[0]['instanceID']
                initial = self.state()
                parameters = self.parameters()
                self.assertGreater(len(parameters), 0)
                p = next(p for p in parameters if p['writable'] and p['canSlide'] and p['max'] > p['min'])
                value = p['min'] if p['value'] != p['min'] else p['max']
                self.write('plugin.parameters.set', slot=0, values=[{'id': p['id'], 'value': value}])
                changed = self.state()
                self.assertNotEqual(initial, changed)
                self.write('history.undo', domain='plugins')
                self.assertEqual(self.state(), initial)
                self.write('history.redo', domain='plugins')
                self.assertEqual(self.state(), changed)
                before_open_parameters = self.parameters()
                self.write('plugin.editor.open', slot=0)
                self.assertEqual(self.doc()['data']['openPluginEditors'], 1)
                self.tick()
                after_editor = self.state()
                def xml_parameters(data):
                    component = plistlib.loads(base64.b64decode(data))['component'].decode('utf-8', errors='replace')
                    return dict(re.findall(r'<PARAM id="([^"]+)"(?: value="([^"]*)")?\s*/>', component))
                expected, actual = xml_parameters(changed), xml_parameters(after_editor)
                delta = {key: (expected.get(key), actual.get(key)) for key in expected.keys() | actual.keys() if expected.get(key) != actual.get(key)}
                if after_editor != changed and os.environ.get('SCREAMSEQ_PLUGIN_EVIDENCE_DIR'):
                    import json
                    folder = Path(os.environ['SCREAMSEQ_PLUGIN_EVIDENCE_DIR'])
                    folder.mkdir(parents=True, exist_ok=True)
                    for name, data in [('before', changed), ('after', after_editor)]:
                        (folder / (descriptor['name'] + '-' + name + '.plist')).write_bytes(base64.b64decode(data))
                    (folder / (descriptor['name'] + '-parameters.json')).write_text(json.dumps(dict(before=before_open_parameters, after=self.parameters()), indent=2))
                self.assertEqual(after_editor, changed, f'Opening {descriptor["name"]} changed saved parameters: {delta}')
                self.write('plugin.remove', slot=0)  # Destroy with native window open.
                self.assertEqual(self.doc()['data']['openPluginEditors'], 0)
                self.write('history.undo', domain='plugins')
                self.assertEqual(self.rack()[0]['instanceID'], stable)
                self.assertEqual(self.state(), changed)
                path = self.folder / (descriptor['name'] + '.screamseq')
                self.write('document.save', path=str(path))
                self.write('document.open', path=str(path))
                self.assertEqual(self.rack()[0]['instanceID'], stable)
                self.assertEqual(self.state(), changed)
                self.write('plugin.editor.open', slot=0)
                self.write('plugin.editor.close', slot=0)
                self.assertEqual(self.doc()['data']['openPluginEditors'], 0)
                self.write('plugin.remove', slot=0)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PLUGIN_CACHE'), 'opt-in installed VST3 cache')
    def test_orbit_editor_generated_eq_change_is_captured_and_undoable(self):
        descriptors = self.client.call('plugin.discover', {'format': 'VST3'})['data']
        orbit = next((d for d in descriptors if d['classID'] == 'ABCDEF019182FAEB446361744F726274'), None)
        if orbit is None:
            self.skipTest('OrbitCab is not in the private cache')
        self.write('plugin.add', descriptor=orbit)
        parameter = next(p for p in self.parameters() if p['name'] == 'Amp EQ')
        self.write('plugin.parameters.set', slot=0, values=[dict(id=parameter['id'], value=1)])
        expected = self.state()
        before = self.doc()['revision']
        self.write('plugin.editor.open', slot=0)
        # OrbitCab 2.5.0's updatePreampRow explicitly sends an eqOn edit during
        # editor construction. Host must capture this as an undoable edit.
        self.tick()
        changed = self.state()  # Forces the pending editor gesture boundary.
        self.assertNotEqual(changed, expected)
        self.assertNotEqual(self.doc()['revision'], before)
        self.assertEqual(next(p['value'] for p in self.parameters() if p['id'] == parameter['id']), 0)
        self.write('history.undo', domain='plugins')
        self.assertEqual(self.state(), expected)
        self.assertEqual(self.doc()['data']['openPluginEditors'], 0)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO'), 'opt-in silent hardware audio')
    def test_live_parameter_batches_preserve_playback_and_baseline(self):
        descriptors = [self.catalog[0]]
        if os.environ.get('SCREAMSEQ_TEST_PLUGIN_CACHE'):
            descriptors += self.client.call('plugin.discover', {'format': 'VST3'})['data']
        evidence = dict(executableSHA256=hashlib.sha256(Path(os.environ['SCREAMSEQ_TEST_EXE']).read_bytes()).hexdigest(),
                        silentOutput=True, plugins=[])
        dwell = float(os.environ.get('SCREAMSEQ_TEST_LIVE_SECONDS', '1'))
        self.assertTrue(0 <= dwell <= 120)
        def transport():
            value = self.client.call('transport.get')['data']
            self.assertTrue(value['audioActive'], value)
            self.assertFalse(value['fault'], value)
            return value
        for descriptor in descriptors:
            with self.subTest(plugin=descriptor['name']):
                self.write('plugin.add', descriptor=descriptor)
                available = self.parameters()
                chosen = [p for p in available if p['writable'] and p['canSlide'] and p['max'] > p['min']][:2]
                self.assertTrue(chosen)
                # Exercise a real native editor notification. OrbitCab emits
                # this EQ edit from its own editor construction callback.
                editor_gesture = descriptor['classID'] == 'ABCDEF019182FAEB446361744F726274'
                if editor_gesture:
                    eq = next(p for p in available if p['name'] == 'Amp EQ')
                    self.write('plugin.parameters.set', slot=0, values=[dict(id=eq['id'], value=1)])
                source = self.folder / (descriptor['name'] + '-source.screamseq')
                self.write('document.save', path=str(source))
                report = self.folder / (descriptor['name'] + '-wasapi.json')
                args = [os.environ['SCREAMSEQ_TEST_EXE'], '--audio-test-silent', '--automation', '--seconds', str(dwell + 30),
                        '--project', str(source), '--report', str(report)]
                if os.environ.get('SCREAMSEQ_TEST_PLUGIN_CACHE'):
                    args += ['--vst3-test-cache', os.environ['SCREAMSEQ_TEST_PLUGIN_CACHE']]
                pid = self.desktop.launch(args)
                inspection_client, inspection_hwnd = self.client, self.hwnd
                self.client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(pid), timeout=20)
                for _ in range(100):
                    try:
                        if self.client.call('transport.get')['data']['audioActive']:
                            break
                    except TransportError:
                        pass
                    time.sleep(.1)
                else:
                    self.fail('owned silent audio test did not start')
                self.hwnd = self.desktop.hwnd(pid)
                samples = [transport()]
                if editor_gesture:
                    self.write('plugin.editor.open', slot=0)
                    self.tick()
                    self.state()  # Force the saved gesture/history boundary.
                    self.assertEqual(next(p['value'] for p in self.parameters() if p['id'] == eq['id']), 0)
                    samples.append(transport())
                    self.write('plugin.editor.close', slot=0)
                for fraction in (.25, .75, .5):
                    values = [dict(id=p['id'], value=p['min'] + fraction * (p['max'] - p['min'])) for p in chosen]
                    before = self.doc()
                    self.write('plugin.parameters.set', slot=0, values=values, dryRun=True)
                    self.assertEqual(self.doc(), before)
                    with self.assertRaises(ApiError):
                        self.write('plugin.parameters.set', slot=0, values=values + [dict(id=4294967295, value=0)])
                    self.assertEqual(self.doc(), before)
                    self.write('plugin.parameters.set', slot=0, values=values)
                    saved = self.state()
                    after = self.doc()
                    self.write('plugin.parameters.set', slot=0, values=values)
                    self.assertEqual(self.doc(), after)
                    self.assertEqual(self.state(), saved)
                    time.sleep(.35)
                    samples.append(transport())
                    self.assertGreater(samples[-1]['frames'], samples[-2]['frames'])
                    self.assertGreaterEqual(samples[-1]['callbacks'], samples[-2]['callbacks'])
                    self.assertEqual(samples[-1]['overruns'], 0)
                if descriptor['format'] == 'VST3':
                    self.write('plugin.editor.open', slot=0)
                end = time.monotonic() + dwell
                while time.monotonic() < end:
                    time.sleep(.2)
                    samples.append(transport())
                    self.assertGreater(samples[-1]['frames'], samples[-2]['frames'])
                    self.assertEqual(samples[-1]['overruns'], 0)
                if descriptor['format'] == 'VST3':
                    self.assertEqual(self.state(), saved)
                    self.write('plugin.editor.close', slot=0)
                path = self.folder / (descriptor['name'] + '-live.screamseq')
                self.write('document.save', path=str(path))
                self.assertTrue(transport()['audioActive'])
                self.desktop.send(self.hwnd, 0x10)  # Clean Quit while DSP is running.
                for _ in range(100):
                    if report.exists():
                        break
                    time.sleep(.1)
                audio_report = json.loads(report.read_text())
                self.assertTrue(audio_report['audio']['silentOutput'])
                self.assertEqual(audio_report['audio']['deadlineOverruns'], 0)
                self.client, self.hwnd = inspection_client, inspection_hwnd
                self.write('document.open', path=str(path))
                self.assertEqual(self.state(), saved)
                evidence['plugins'].append(dict(name=descriptor['name'], classID=descriptor['classID'],
                    nativeEditorGesture=editor_gesture, snapshots=samples, baselineReopened=True, report=audio_report))
                self.write('plugin.remove', slot=0)
        self.write('document.save', path=str(self.folder / 'closed.screamseq'))
        self.desktop.send(self.hwnd, 0x10)  # Clean, task-owned application Quit.
        for _ in range(100):
            if self.report.exists():
                break
            time.sleep(.1)
        if os.environ.get('SCREAMSEQ_PLUGIN_EVIDENCE_DIR'):
            folder = Path(os.environ['SCREAMSEQ_PLUGIN_EVIDENCE_DIR'])
            folder.mkdir(parents=True, exist_ok=True)
            (folder / 'live-parameters.json').write_text(json.dumps(evidence, indent=2))


if __name__ == '__main__':
    unittest.main()
