"""Real native instrument menus, retained targets and guarded musical transactions."""
import ctypes
from ctypes import wintypes as w
import time
import unittest

import private_desktop
import native_context_menu
import test_instrument_compact_ui as compact
import test_workspace_docking as docking


user = private_desktop.user


class InstrumentContextUITests(unittest.TestCase):
    setUp = compact.InstrumentCompactUITests.setUp
    doc = compact.InstrumentCompactUITests.doc
    read = compact.InstrumentCompactUITests.read
    write = compact.InstrumentCompactUITests.write
    state = compact.InstrumentCompactUITests.state
    control = compact.InstrumentCompactUITests.control
    idle = compact.InstrumentCompactUITests.idle
    start = compact.InstrumentCompactUITests.start
    saved = compact.InstrumentCompactUITests.saved
    settings = compact.InstrumentCompactUITests.settings
    press = compact.InstrumentCompactUITests.press
    field = compact.InstrumentCompactUITests.field
    select = compact.InstrumentCompactUITests.select
    text = compact.InstrumentCompactUITests.text
    create = compact.InstrumentCompactUITests.create
    resize_client = compact.InstrumentCompactUITests.resize_client

    def window(self, kind='ScreamSeq.InstrumentEnvelope'):
        return docking.WorkspaceDockingTests.native_window(self, kind)

    def screen(self, x, y):
        scale = user.GetDpiForWindow(self.window()) / 96
        point = w.POINT(round(x * scale), round(y * scale))
        private_desktop.check(user.ClientToScreen(self.window(), ctypes.byref(point)))
        return point.x, point.y

    def point(self, index):
        point = self.state()['handles'][index]
        return self.screen(point['x'], point['y'])

    def menu(self, point=None, source=None):
        return native_context_menu.Menu(self, self.window(), point=point, source=source)

    def commands(self, items):
        result = {}
        for item in items:
            if item['command']:
                result[item['command']] = item
            result.update(self.commands(item.get('children', [])))
        return result

    def prepare(self):
        self.create()
        if self.state()['compactLayout']:
            self.press(4600)
        self.press(4410)
        self.wait_saved()
        self.assertEqual(len(self.saved()['points']), 5)
        self.resize_client(440, 500)
        self.press(4600)

    def wait_saved(self):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            state = self.state()
            if not state['dirty'] and not state['pending']:
                return
            time.sleep(.025)
        self.fail(str(state))

    def test_canvas_add_delete_automatic_history_and_native_save(self):
        self.prepare()
        instrument_index = self.state()['index']
        original = self.settings()
        x, y, width, height = self.state()['canvas']
        with self.menu(self.screen(x + width * .375, y + height * .75)) as menu:
            commands = self.commands(menu.items())
            self.assertTrue(commands[4700]['enabled'])
            menu.choose(4700)
        self.idle()
        self.assertIn([24, 16], self.state()['envelope']['points'])
        self.wait_saved()
        added = self.settings()
        expected_added = dict(original)
        expected_added['envelopes'] = [dict(e) for e in original['envelopes']]
        expected_added['envelopes'][0]['points'] = sorted(original['envelopes'][0]['points'] + [[24, 16]])
        self.assertEqual(added, expected_added)
        with self.menu(self.point(1)) as menu:
            menu.choose(4702)
        self.idle()
        self.assertNotIn([2, 64], self.state()['envelope']['points'])
        self.wait_saved()
        with self.menu() as menu:
            self.assertNotIn(4450, self.commands(menu.items()), 'Saved edits must not advertise Apply')
            menu.cancel()
        self.idle()
        saved = self.settings()
        expected_saved = dict(added)
        expected_saved['envelopes'] = [dict(e) for e in added['envelopes']]
        expected_saved['envelopes'][0]['points'] = [p for p in added['envelopes'][0]['points'] if p != [2, 64]]
        self.assertEqual(saved, expected_saved)
        self.assertIn([24, 16], saved['envelopes'][0]['points'])
        self.assertNotIn([2, 64], saved['envelopes'][0]['points'])
        self.write('history.undo', domain='document')
        self.assertEqual(self.settings(), added)
        self.write('history.undo', domain='document')
        self.assertEqual(self.settings(), original)
        self.write('history.redo', domain='document')
        self.assertEqual(self.settings(), added)
        self.write('history.redo', domain='document')
        self.assertEqual(self.settings(), saved)
        path = self.folder / 'instrument-context.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.read('instrument.get', instrument=instrument_index), saved)

    def test_bank_handoff_saves_properties_and_keeps_separate_history(self):
        self.prepare()
        original = self.settings()
        self.press(4603)
        self.field(4439, 'Automatic bank handoff')
        with self.menu(self.screen(20, 20)) as menu:
            self.assertTrue(self.commands(menu.items())[4404]['enabled'])
            menu.choose(4404)
        self.idle()
        captured = self.state()
        self.assertTrue(captured['envelopeBank']['visible'])
        self.assertTrue(captured['envelopeBank']['sourceCurrent'])
        self.assertFalse(captured['dirty'])
        expected = dict(original, name='Automatic bank handoff')
        self.assertEqual(self.settings(), expected)
        bank = self.window('ScreamSeq.EnvelopeBank')
        save = user.GetDlgItem(bank, 1005)
        self.assertTrue(user.IsWindowVisible(save) and user.IsWindowEnabled(save))
        self.desktop.send(save, 0xF5)
        self.idle()
        entries = self.read('envelope.bank.list')['entries']
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0]['shape']['points'], [dict(position=tick*256, value=value/64, curve='linear') for tick, value in original['envelopes'][0]['points']])
        self.assertEqual(self.settings(), expected)
        self.write('history.undo', domain='document')
        self.assertEqual(self.read('envelope.bank.list')['entries'], [])
        self.assertEqual(self.settings(), expected)
        self.write('history.undo', domain='document')
        self.assertEqual(self.settings(), original)

    def test_point_hit_edit_and_cancel_preserve_retained_target_and_raw_fields(self):
        self.prepare()
        target = self.state()['instrument']
        before = self.state()
        document = self.doc()
        with self.menu(self.point(3)) as menu:
            self.assertTrue(self.commands(menu.items())[4701]['enabled'])
            menu.cancel()
        self.idle()
        for key in ('instrument', 'selectedPoint', 'envelope', 'dirty', 'fieldDraft'):
            self.assertEqual(self.state()[key], before[key], key)
        with self.menu(self.point(3)) as menu:
            menu.choose(4701)
        self.idle()
        self.assertEqual(self.state()['selectedPoint'], 3)
        self.assertEqual(self.state()['page'], 'points')
        self.assertEqual(self.desktop.focus(self.window()), self.control(4413))
        info = private_desktop.GUI(cbSize=ctypes.sizeof(private_desktop.GUI))
        private_desktop.check(user.GetGUIThreadInfo(user.GetWindowThreadProcessId(self.window(), None), ctypes.byref(info)))
        self.assertFalse(info.hwndCapture)
        self.field(4414, 29)
        with self.menu() as menu:
            commands = self.commands(menu.items())
            self.assertFalse(commands[4700]['enabled'])
            self.assertFalse(commands[4702]['enabled'])
            self.assertTrue(commands[4415]['enabled'])
            menu.choose(4415)
        self.idle()
        self.assertEqual(self.state()['envelope']['points'][3], [32, 29])
        self.assertEqual(self.state()['instrument'], target)
        self.assertEqual(self.doc(), document)

    def test_menu_rejects_revision_and_draft_changes_during_modal_tracking(self):
        self.prepare()
        before = self.state()
        with self.menu(self.point(1)) as menu:
            self.write('document.patch', title='Changed during native instrument menu')
            changed = self.doc()
            menu.choose(4702)
        self.idle()
        self.assertEqual(self.doc(), changed)
        self.assertEqual(self.state()['envelope'], before['envelope'])
        self.assertEqual(self.state()['selectedPoint'], before['selectedPoint'])
        self.assertIn('menu was open', self.state()['status'])
        self.press(4451)
        before = self.state()
        with self.menu(self.point(1)) as menu:
            # The retained HWND remains alive while another nested event edits its text.
            text = ctypes.create_unicode_buffer('23')
            self.desktop.send(self.control(4414), 0xC, 0, ctypes.addressof(text))
            menu.choose(4702)
        self.idle()
        self.assertEqual(self.text(4414), '23')
        self.assertTrue(self.state()['fieldDraft'])
        self.assertEqual(self.state()['envelope'], before['envelope'])
        self.assertIn('menu was open', self.state()['status'])
        self.assertEqual(self.doc(), changed)

    def test_tools_bank_check_states_and_hidden_canvas_context(self):
        self.prepare()
        before = self.doc()
        with self.menu() as menu:
            commands = self.commands(menu.items())
            self.assertTrue(commands[4405]['checked'])
            self.assertFalse(commands[4406]['checked'])
            self.assertFalse(commands[4409]['enabled'])
            menu.choose(4602)
        self.idle()
        self.assertEqual(self.state()['page'], 'tools')
        self.select(4429, 5)
        self.field(4432, '27.5')
        with self.menu(self.screen(20, 20)) as menu:
            commands = self.commands(menu.items())
            self.assertNotIn(4700, commands)
            self.assertFalse(commands[4403]['enabled'])  # New must not discard raw tool fields.
            self.assertFalse(commands[4455]['enabled'])  # Neither may Import.
            menu.cancel()
        self.assertEqual(self.text(4432), '27.5')
        self.press(4600)
        with self.menu() as menu:
            menu.choose(4404)
        self.idle()
        self.assertTrue(self.state()['envelopeBank']['visible'])
        self.assertEqual(self.doc(), before)

    def test_dock_float_menu_retains_same_native_editor_and_draft(self):
        self.prepare()
        self.press(4601)
        self.field(4414, 31)
        self.press(4415)
        before = self.state()
        document = self.doc()
        window = self.window()
        with self.menu() as menu:
            self.assertIn('Dock', self.commands(menu.items())[4703]['label'])
            menu.choose(4703)
        self.idle()
        self.assertEqual(self.read('workspace.get')['locations']['instruments'], 'right')
        self.assertEqual(self.window(), window)
        self.assertEqual(self.state()['envelope'], before['envelope'])
        with self.menu() as menu:
            self.assertIn('Float', self.commands(menu.items())[4703]['label'])
            menu.choose(4703)
        self.idle()
        self.assertEqual(self.read('workspace.get')['locations']['instruments'], 'float')
        self.assertEqual(self.window(), window)
        self.assertEqual(self.state()['envelope'], before['envelope'])
        self.assertEqual(self.doc(), document)

    def test_native_text_menu_is_preserved(self):
        self.prepare()
        self.press(4603)
        self.field(4439, 'Native text remains editable')
        before = self.doc()
        with self.menu(source=self.control(4439)) as menu:
            commands = self.commands(menu.items())
            self.assertFalse(set(commands).intersection({4700, 4701, 4702, 4703, 4450}))
            self.assertTrue(any('Copy' in item['label'] or 'Select All' in item['label'] for item in commands.values()))
            menu.cancel()
        self.assertEqual(self.text(4439), 'Native text remains editable')
        self.assertEqual(self.doc(), before)


if __name__ == '__main__':
    unittest.main()
