"""Current Mac pattern FX and precise-note contracts through an owned app pipe."""
import os
import ctypes
from ctypes import wintypes
from pathlib import Path
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'windows/Api'))
from client import Client, ApiError, TransportError
from private_desktop import PrivateDesktop
ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))


class PatternPerformanceTests(unittest.TestCase):
    def setUp(self):
        self.folder = Path(self.enterContext(tempfile.TemporaryDirectory(prefix='pattern-fx-', dir=os.environ['TMPDIR'])))
        self.desktop = self.enterContext(PrivateDesktop())
        self.pid = self.desktop.launch([os.environ['SCREAMSEQ_TEST_EXE'], '--inspection', '--automation', '--seconds', '120'])
        self.client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(self.pid), timeout=20)
        for _ in range(100):
            try:
                self.doc()
                break
            except TransportError:
                time.sleep(.1)
        else:
            self.fail('owned pattern test app did not publish its pipe')

    def doc(self):
        return self.client.call('document.get')

    def read(self, method, **fields):
        return self.client.call(method, fields)['data']

    def write(self, method, **fields):
        return self.client.call(method, dict(expectedRevision=self.doc()['revision'], **fields))['data']

    def cells(self, pattern=0):
        rows = next(p['rows'] for p in self.doc()['data']['patterns'] if p['index'] == pattern)
        return self.read('pattern.get', pattern=pattern, rowCount=rows)['cells']

    def navigate(self, **fields):
        current = self.client.call('context.get')
        return self.client.call('context.set', dict(expectedRevision=current['revision'],
            expectedContext=current['data']['contextRevision'], **fields))['data']

    def control(self, identifier):
        user = ctypes.WinDLL('user32')
        user.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
        user.GetDlgItem.restype = wintypes.HWND
        handle = user.GetDlgItem(self.desktop.hwnd(self.pid), identifier)
        self.assertTrue(handle)
        return handle

    def key(self, code):
        self.desktop.send(self.desktop.hwnd(self.pid), 0x100, ord(code) if isinstance(code, str) else code)

    def command(self, identifier, notification=0):
        self.desktop.send(self.desktop.hwnd(self.pid), 0x111, identifier | (notification << 16), self.control(identifier))

    def text(self, identifier, value):
        buffer = ctypes.create_unicode_buffer(str(value))
        self.desktop.send(self.control(identifier), 0xC, 0, ctypes.addressof(buffer))

    def test_native_grid_eight_columns_two_character_entry_scroll_and_delete(self):
        user = ctypes.WinDLL('user32')
        user.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
        dpi = self.read('workspace.get')['dpi'] / 96
        self.assertTrue(user.SetWindowPos(self.desktop.hwnd(self.pid), None, 0, 0, int(900*dpi), int(680*dpi), 0x16))
        self.desktop.send(self.control(134), 0x14E, 7)  # Native FX count chooser.
        self.command(134, 1)
        self.assertEqual(self.read('pattern.effects.get', pattern=0)['columns'][0]['count'], 8)
        self.navigate(row=4, channel=0, column=17, following=False)
        self.assertGreater(self.read('workspace.get')['viewport']['horizontalScroll'], 0)
        speed = next(p for p in self.read('pattern.commands')['effect'] if p['name'] == 'Set Speed')
        for letter in speed['displayCode']:
            self.key(letter)
        self.assertEqual(self.read('context.get')['column'], 18)
        self.key('0')
        self.key('3')
        command = next(c for c in self.read('pattern.effects.get', pattern=0)['commands'] if c['column'] == 7)
        self.assertEqual((command['position'], command['effect'], command['parameter']), (4 * 65536, speed['command'], 3))
        before_cells = self.cells()
        # Delete is scoped to FX 8, and does not erase the channel's note fields.
        self.key(0x2E)
        self.assertEqual(self.cells(), before_cells)
        self.assertFalse(any(c['column'] == 7 for c in self.read('pattern.effects.get', pattern=0)['commands']))
        self.write('history.undo', domain='document')
        self.key(0x27)  # Right from FX 8's value reaches the next channel's note.
        current = self.read('context.get')
        self.assertEqual((current['channel'], current['column']), (1, 0))
        self.key(0x25)
        current = self.read('context.get')
        self.assertEqual((current['channel'], current['column']), (0, 18))
        # Shrinking would hide occupied columns and must reject without mutation.
        before = self.doc()
        self.desktop.send(self.control(134), 0x14E, 0)
        self.command(134, 1)
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.desktop.send(self.control(134), 0x147), 7)
        self.navigate(row=6, channel=1, column=3)
        self.key('N')
        self.navigate(row=7)
        self.key('C')  # Prefix from row 6 must never create an NC on row 7.
        self.assertFalse(any(c['kind'] == 'note-cut' for c in self.read('pattern.effects.get', pattern=0)['commands']))

    def test_native_new_binding_is_atomic_with_parameter_command(self):
        self.write('plugin.add', descriptor=self.read('plugin.discover', format='Built-in')[0])
        self.command(316)
        for _ in range(100):
            if self.desktop.send(self.control(311), 0x146) > 0:
                break
            time.sleep(.05)
        else:
            self.fail('native parameter catalog did not arrive')
        parameters = self.read('plugin.parameters.get', slot=0)
        selected = next(i for i, p in enumerate(parameters) if p['writable'] and p['canSlide'])
        self.desktop.send(self.control(311), 0x14E, selected)
        self.command(311, 1)
        self.desktop.send(self.desktop.hwnd(self.pid), 0x111, 113)  # Palette action: focus grid.
        self.navigate(row=9, channel=1, column=3)
        self.key('P')
        self.key('S')
        self.assertEqual(self.read('pattern.effects.get', pattern=0)['bindings'], [])
        self.assertGreater(self.desktop.send(self.control(346), 0x146), 0)
        self.text(342, .375)
        self.text(343, 12345)
        self.command(347)
        effects = self.read('pattern.effects.get', pattern=0)
        self.assertEqual(len(effects['bindings']), 1)
        binding = effects['bindings'][0]
        self.assertEqual(binding['parameter'], parameters[selected]['id'])
        self.assertTrue(binding['resolved'])
        command = next(c for c in effects['commands'] if c['kind'] == 'parameter-set')
        self.assertEqual((command['binding'], command['value'], command['position']), (binding['id'], .375, 9*65536+12345))
        self.write('history.undo', domain='document')
        self.assertEqual(self.read('pattern.effects.get', pattern=0)['bindings'], [])
        self.assertEqual(len(self.doc()['data']['nativePlugins']), 1)

    def test_native_precise_fx_draft_search_captured_target_stale_and_reopen(self):
        self.write('pattern.effects.set', pattern=0, columns=[dict(channel=0, count=8)])
        self.navigate(row=8, channel=0, column=17)
        self.key('N')
        self.key('C')
        state = self.read('workspace.get')['effectEditor']
        self.assertTrue(state['visible'])
        self.assertEqual((state['row'], state['column']), (8, 7))
        self.text(343, 16384)
        self.navigate(row=10, channel=1, column=0)
        self.command(347)
        cut = next(c for c in self.read('pattern.effects.get', pattern=0)['commands'] if c['kind'] == 'note-cut')
        self.assertEqual((cut['channel'], cut['position'], cut['column']), (0, 8 * 65536 + 16384, 7))
        self.assertEqual((self.read('context.get')['row'], self.read('context.get')['channel']), (10, 1))
        self.write('history.undo', domain='document')
        self.assertTrue(self.read('workspace.get')['effectEditor']['stale'])
        before = self.doc()
        self.command(347)
        self.assertEqual(self.doc(), before)
        self.assertTrue(self.read('workspace.get')['effectEditor']['visible'])
        self.command(348)  # Explicitly reload the current cursor.
        self.text(349, 'pitch slide')
        self.assertEqual(self.desktop.send(self.control(341), 0x146), 1)
        self.desktop.send(self.control(341), 0x14E, 0)
        self.command(341, 1)
        self.text(342, 7.5)
        self.text(343, 32768)
        self.text(344, 98304)
        self.text(345, 12)
        self.command(347)
        effects = self.read('pattern.effects.get', pattern=0)
        slide = next(c for c in effects['commands'] if c['kind'] == 'pitch-slide')
        self.assertEqual((slide['channel'], slide['position'], slide['duration'], slide['value']), (1, 10 * 65536 + 32768, 98304, 7.5))
        before = self.doc()
        self.command(347)
        self.assertEqual(self.doc(), before)
        path = self.folder / 'native-fx.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path))
        self.assertEqual(self.read('pattern.effects.get', pattern=0), effects)

    def test_shared_row_transform_preserves_other_columns_notes_and_history(self):
        self.write('pattern.effects.set', pattern=0, columns=[dict(channel=0, count=8)], commands=[
            dict(channel=0, position=65536+12345, duration=0, column=7, kind='note-cut'),
            dict(channel=1, position=2*65536, column=0, kind='pitch-set', value=3)])
        self.write('pattern.notes.set', pattern=0, events=[dict(channel=0, position=65536+500, note=61)])
        before = self.read('pattern.effects.get', pattern=0)
        before_cells = self.cells()
        notes = self.read('pattern.notes.get', pattern=0)
        fields = dict(pattern=0, startRow=0, rowCount=4, startChannel=0, channelCount=1, operation='reverse')
        doc = self.doc()
        preview = self.write('pattern.transform', **fields, dryRun=True)
        self.assertTrue(preview['effectsChanged'])
        self.assertEqual(self.doc(), doc)
        self.write('pattern.transform', **fields)
        after = self.read('pattern.effects.get', pattern=0)
        self.assertEqual(next(c['position'] for c in after['commands'] if c['column'] == 7), 2*65536+12345)
        self.assertEqual([c for c in before['commands'] if c['channel'] == 1], [c for c in after['commands'] if c['channel'] == 1])
        self.assertEqual(self.read('pattern.notes.get', pattern=0), notes)
        self.write('history.undo', domain='document')
        self.assertEqual(self.read('pattern.effects.get', pattern=0), before)
        self.assertEqual(self.cells(), before_cells)
        self.write('history.redo', domain='document')
        doc = self.doc()
        with self.assertRaises(ApiError):
            self.write('pattern.transform', **dict(fields, operation='expand'), amount=3)
        self.assertEqual(self.doc(), doc)
        self.write('pattern.transform', operation='clear', scope='song', fields=['effect'])
        self.assertEqual(self.read('pattern.effects.get', pattern=0)['commands'], [])
        self.assertEqual(self.read('pattern.notes.get', pattern=0), notes)

    def test_all_fx_columns_atomic_cell_edits_history_and_reopen(self):
        original_cells = self.cells()
        original = self.read('pattern.effects.get', pattern=0)
        catalog = self.read('pattern.commands')['effect']
        speed = next(p for p in catalog if p['name'] == 'Set Speed')
        self.assertEqual(len(speed['displayCode']), 2)
        changes = dict(pattern=0, columns=[dict(channel=0, count=8)], commands=[
            dict(channel=0, position=0, column=0, kind='tracker', effect=speed['command'], parameter=6),
            dict(channel=0, position=65536, column=7, kind='tracker', effect=speed['command'], parameter=3),
            dict(channel=0, position=2 * 65536 + 16384, column=2, kind='note-cut'),
            dict(channel=0, position=3 * 65536, duration=65536, column=3, kind='pitch-slide', value=7, pitchRange=12)])
        before = self.doc()
        self.assertTrue(self.write('pattern.effects.set', **changes, dryRun=True)['wouldChange'])
        self.assertEqual(self.doc(), before)
        self.write('pattern.performance.set', **changes)
        configured = self.read('pattern.effects.get', pattern=0)
        self.assertEqual(configured, self.read('pattern.performance.get', pattern=0))
        self.assertEqual(configured['columns'][0]['count'], 8)
        self.assertEqual(len(configured['commands']), 4)
        for old, new in zip(original_cells, self.cells()):
            for key in ('note', 'instrument', 'volumeCommand', 'volume'):
                self.assertEqual(old[key], new[key])
        self.assertEqual(self.cells()[0]['effect'], speed['command'])
        before = self.doc()
        self.assertFalse(self.write('pattern.effects.set', **changes)['wouldChange'])
        self.assertEqual(self.doc(), before)
        for fields in [dict(columns=[dict(channel=0, count=1)]),
                       dict(commands=[changes['commands'][1], changes['commands'][1]]),
                       dict(commands=[dict(channel=0, position=1, column=0, kind='tracker', effect=speed['command'])]),
                       dict(columns=[dict(channel=0, count=True)]),
                       dict(commands=[dict(channel=0, position=0, column=0, kind='note-cut', value=1)])]:
            with self.subTest(fields=fields), self.assertRaises(ApiError):
                self.write('pattern.effects.set', pattern=0, **fields)
            self.assertEqual(self.doc(), before)
        self.write('pattern.effect.set', pattern=0, row=1, channel=0, column=7, command=None)
        remaining = self.read('pattern.effects.get', pattern=0)
        self.assertEqual(len(remaining['commands']), 3)
        self.write('history.undo', domain='document')
        self.assertEqual(self.read('pattern.effects.get', pattern=0), configured)
        self.write('history.undo', domain='document')
        self.assertEqual(self.read('pattern.effects.get', pattern=0), original)
        self.assertEqual(self.cells(), original_cells)
        self.write('history.redo', domain='document')
        path = self.folder / 'all-fx.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path))
        self.assertEqual(self.read('pattern.effects.get', pattern=0), configured)

    def test_parameter_bindings_unresolved_preservation_and_whole_batch_rejection(self):
        descriptor = self.read('plugin.discover', format='Built-in')[0]
        self.write('plugin.add', descriptor=descriptor)
        plugin = self.doc()['data']['nativePlugins'][0]['instanceID']
        parameter = next(p for p in self.read('plugin.parameters.get', slot=0) if p['canSlide'] and p['writable'])
        self.write('pattern.effects.set', pattern=0, columns=[dict(channel=0, count=3)],
                   bindings=[dict(id=7, plugin=plugin, parameter=parameter['id'], name='Gain')],
                   commands=[dict(channel=0, position=0, column=1, kind='parameter-set', binding=7, value=.25),
                             dict(channel=0, position=65536, duration=65536, column=2, kind='parameter-slide', binding=7, value=.75)])
        value = self.read('pattern.effects.get', pattern=0)
        self.assertTrue(value['bindings'][0]['resolved'])
        self.assertTrue(value['bindings'][0]['canSlide'])
        before = self.doc()
        for fields in [dict(removeBindings=[7]),
                       dict(bindings=[dict(id=7, plugin=plugin, parameter=4294967295)]),
                       dict(bindings=[dict(id=8, plugin=plugin, parameter=parameter['id']), dict(id=8, plugin=plugin, parameter=parameter['id'])])]:
            with self.subTest(fields=fields), self.assertRaises(ApiError):
                self.write('pattern.effects.set', pattern=0, **fields)
            self.assertEqual(self.doc(), before)
        self.write('plugin.remove', slot=0)
        unresolved = self.read('pattern.effects.get', pattern=0)
        self.assertFalse(unresolved['bindings'][0]['resolved'])
        self.assertEqual(unresolved['commands'], value['commands'])
        self.write('pattern.effects.set', pattern=0, columns=[dict(channel=1, count=2)])
        before = self.doc()
        with self.assertRaises(ApiError):
            self.write('pattern.effect.set', pattern=0, row=1, channel=0, column=2,
                       command=dict(kind='parameter-slide', binding=7, value=.2, duration=65536))
        self.assertEqual(self.doc(), before)
        self.write('history.undo', domain='plugins')
        self.assertTrue(self.read('pattern.effects.get', pattern=0)['bindings'][0]['resolved'])

    def test_precise_note_offsets_local_effects_order_noop_clear_and_reopen(self):
        before_cells = self.cells()
        empty = self.read('pattern.notes.get', pattern=0)
        self.assertEqual(empty['events'], [])
        self.assertTrue(all('allowedParameters' in p and 'displayCode' in p for p in empty['effects']))
        events = [dict(channel=0, row=0, offsetRows=.25, note=61, instrument=1, velocity=100),
                  dict(channel=0, position=65536, note=255),
                  dict(channel=1, row=0, offsetBeats=.125 / empty['rowsPerBeat'], note=68, instrument=2)]
        before = self.doc()
        self.assertTrue(self.write('pattern.notes.set', pattern=0, events=events, clearLegacy=True, dryRun=True)['wouldChange'])
        self.assertEqual(self.doc(), before)
        self.write('pattern.notes.set', pattern=0, events=events, clearLegacy=True)
        saved = self.read('pattern.notes.get', pattern=0)
        self.assertEqual(sorted(n['position'] for n in saved['events']), [8192, 16384, 65536])
        for cell in self.cells()[:2]:
            self.assertEqual(cell['note'], 0)
            self.assertEqual(cell['instrument'], 0)
        before = self.doc()
        self.assertFalse(self.write('pattern.notes.set', pattern=0, events=list(reversed(saved['events'])))['wouldChange'])
        self.assertEqual(self.doc(), before)
        for invalid in [dict(channel=0, row=0, offsetRows=1, note=61),
                        dict(channel=0, row=0, offsetRows=.1, offsetBeats=.1, note=61),
                        dict(channel=0, position=0, note=255, instrument=1),
                        dict(channel=0, position=0, note=61, velocity=True)]:
            with self.subTest(event=invalid), self.assertRaises(ApiError):
                self.write('pattern.notes.set', pattern=0, events=[invalid])
            self.assertEqual(self.doc(), before)
        self.write('history.undo', domain='document')
        self.assertEqual(self.read('pattern.notes.get', pattern=0), empty)
        self.assertEqual(self.cells(), before_cells)
        self.write('history.redo', domain='document')
        path = self.folder / 'precise.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path))
        self.assertEqual(self.read('pattern.notes.get', pattern=0), saved)


if __name__ == '__main__':
    unittest.main()
