"""Menu, selector mouse input and pattern shortcuts on an owned private desktop."""
import ctypes
from ctypes import wintypes as w
import unittest
import test_workspace_shortcuts as shortcuts
from client import ApiError

user = shortcuts.user
user.GetMenu.argtypes = [w.HWND]
user.GetMenu.restype = w.HMENU
user.GetSubMenu.argtypes = [w.HMENU, ctypes.c_int]
user.GetSubMenu.restype = w.HMENU
user.GetMenuItemCount.argtypes = [w.HMENU]
user.GetMenuStringW.argtypes = [w.HMENU, w.UINT, w.LPWSTR, ctypes.c_int, w.UINT]


class MenuPatternTests(unittest.TestCase):
    setUp = shortcuts.WorkspaceShortcutTests.setUp
    doc = shortcuts.WorkspaceShortcutTests.doc
    read = shortcuts.WorkspaceShortcutTests.read
    write = shortcuts.WorkspaceShortcutTests.write
    state = shortcuts.WorkspaceShortcutTests.state
    ready = shortcuts.WorkspaceShortcutTests.ready
    navigate = shortcuts.WorkspaceShortcutTests.navigate
    native_window = shortcuts.WorkspaceShortcutTests.native_window
    command = shortcuts.WorkspaceShortcutTests.command
    key = shortcuts.WorkspaceShortcutTests.key
    focus_pattern = shortcuts.WorkspaceShortcutTests.focus_pattern
    focus_control = shortcuts.WorkspaceShortcutTests.focus_control

    def test_edit_menu_keeps_text_undo_and_redo_local(self):
        self.seed()
        self.write('history.undo', domain='all')
        self.command(340)
        edit = user.GetDlgItem(self.native_window(), 342)
        self.focus_control(edit)
        before = self.doc()
        self.assertTrue(before['data']['canRedo'])
        self.desktop.send(edit, 0xB1, 0, -1)  # EM_SETSEL
        text = ctypes.create_unicode_buffer('123')
        self.desktop.send(edit, 0xC2, 1, ctypes.addressof(text))  # EM_REPLACESEL, undoable
        self.command(122)
        self.assertEqual(self.doc(), before)
        self.command(123)
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.desktop.focus(edit), edit)

    def cell(self, row=8, channel=0):
        cell = self.read('pattern.get', pattern=0, startRow=row, rowCount=1,
                         startChannel=channel, channelCount=1)['cells'][0]
        return [cell[k] for k in ('note', 'instrument', 'volumeCommand', 'volume', 'effect', 'parameter')]

    def seed(self):
        self.write('pattern.apply', cells=[dict(pattern=0, row=8, channel=0,
            note=61, instrument=1, volumeCommand=1, volume=40, effect=1, parameter=12)])
        self.focus_pattern()

    def test_menus_and_new_song_guard_save_and_history(self):
        menu = user.GetMenu(self.native_window())
        self.assertTrue(menu)
        titles = []
        for i in range(user.GetMenuItemCount(menu)):
            text = ctypes.create_unicode_buffer(128)
            user.GetMenuStringW(menu, i, text, 128, 0x400)
            titles.append(text.value.replace('&', ''))
        self.assertEqual(titles, ['File', 'Edit', 'View', 'Pattern', 'Playback', 'Help'])
        text = ctypes.create_unicode_buffer(128)
        user.GetMenuStringW(user.GetSubMenu(menu, 0), 700, text, 128, 0)
        self.assertIn('Ctrl+N', text.value)
        self.write('document.patch', title='Keep this dirty song')
        before = self.doc()
        for params in ({}, {'discard': 1}, {'discard': True, 'unknown': 1}):
            with self.assertRaises(ApiError):
                self.client.call('document.new', dict(expectedRevision=before['revision'], **params))
            self.assertEqual(self.doc(), before)
        with self.assertRaises(ApiError):
            self.client.call('document.new', dict(expectedRevision='stale', discard=True))
        self.write('document.new', discard=True, dryRun=True)
        self.assertEqual(self.doc(), before)
        self.write('document.new', discard=True)
        self.assertEqual(self.cell(), [0, 0, 0, 0, 0, 0])
        self.assertFalse(self.doc()['data']['canUndo'])
        self.assertFalse(self.doc()['data']['canRedo'])
        self.assertNotEqual(self.doc()['revision'], before['revision'])
        # New through the actual Ctrl+N binding on a clean song needs no prompt.
        self.focus_pattern()
        clean = self.doc()
        self.key(ord('N'), ctrl=True)
        self.assertNotEqual(self.doc()['revision'], clean['revision'])
        self.seed()
        filled = self.cell()
        self.write('history.undo', domain='all')
        self.assertEqual(self.cell(), [0, 0, 0, 0, 0, 0])
        self.write('history.redo', domain='all')
        self.assertEqual(self.cell(), filled)
        path = self.folder / 'new-song.screamseq'
        self.write('document.save', path=str(path))
        self.command(700)
        self.assertEqual(self.cell(), [0, 0, 0, 0, 0, 0])
        self.write('document.open', path=str(path))
        self.assertEqual(self.cell(), filled)

    def test_backspace_delete_and_dot_preserve_other_fields(self):
        self.seed()
        filled = self.cell()
        for key in (0x08, 0x2E, 0xBE, 0x6E):
            for column, cleared in ((1, [1]), (2, [2, 3]), (3, [4, 5]), (4, [4, 5]), (0, range(6))):
                self.navigate(row=8, channel=0, column=column, following=False)
                self.key(key)
                expected = list(filled)
                for index in cleared:
                    expected[index] = 0
                self.assertEqual(self.cell(), expected, (key, column))
                self.key(ord('Z'), ctrl=True)
                self.assertEqual(self.cell(), filled)

    def test_selection_and_input_navigation(self):
        self.seed()
        self.navigate(row=8, channel=0, column=0, following=False)
        self.key(0x28, shift=True)
        selection = self.read('context.get')['selection']
        self.assertEqual((selection['startRow'], selection['endRow']), (8, 9))
        self.key(ord('L'), ctrl=True)
        selection = self.read('context.get')['selection']
        self.assertEqual((selection['startRow'], selection['endRow'], selection['startChannel'], selection['endChannel']), (0, 63, 0, 0))
        self.key(ord('A'), ctrl=True)
        selection = self.read('context.get')['selection']
        self.assertEqual(selection['startChannel'], 0)
        self.assertGreater(selection['endChannel'], 0)
        self.navigate(row=8, channel=0, column=1, following=False)
        self.key(0x0D)
        self.key(0x27, alt=True)
        self.assertEqual(self.read('context.get')['octave'], 5)
        self.key(0x6F)
        self.assertEqual(self.read('context.get')['octave'], 4)
        self.key(0x09)
        self.assertEqual(self.read('context.get')['column'], 1)

    def test_fx_selection_and_precise_note_clear_preserve_other_data(self):
        self.seed()
        self.write('pattern.effects.set', pattern=0, columns=[dict(channel=0, count=2)], commands=[
            dict(channel=0, position=8*65536+123, column=1, kind='note-cut'),
            dict(channel=1, position=8*65536, column=0, kind='pitch-set', value=3)])
        self.write('pattern.notes.set', pattern=0, events=[
            dict(channel=0, position=8*65536+500, note=61),
            dict(channel=1, position=8*65536+700, note=65)])
        notes = self.read('pattern.notes.get', pattern=0)
        effects = self.read('pattern.effects.get', pattern=0)
        cell = self.cell()
        self.navigate(row=8, channel=0, column=5, following=False)
        self.key(ord('L'), ctrl=True)
        self.key(0x08)
        self.assertEqual(self.read('pattern.notes.get', pattern=0), notes)
        self.assertEqual(self.cell()[:4], cell[:4])
        remaining = self.read('pattern.effects.get', pattern=0)['commands']
        self.assertEqual(remaining, [c for c in effects['commands'] if c['channel'] != 0])
        self.key(ord('Z'), ctrl=True)
        self.assertEqual(self.read('pattern.effects.get', pattern=0), effects)
        self.navigate(row=8, channel=0, column=0, following=False)
        self.key(0xBE)
        self.assertEqual(self.read('pattern.notes.get', pattern=0)['events'],
                         [n for n in notes['events'] if n['channel'] != 0])
        self.assertEqual(self.read('pattern.effects.get', pattern=0), effects)
        self.key(ord('Z'), ctrl=True)
        self.assertEqual(self.read('pattern.notes.get', pattern=0), notes)

    def test_main_dropdown_mouse_click_stays_open(self):
        main = self.native_window()
        for ident in (130, 131, 132, 133, 134, 135):
            combo = user.GetDlgItem(main, ident)
            self.assertTrue(combo)
            rect = w.RECT()
            user.GetClientRect(combo, ctypes.byref(rect))
            point = (rect.right - 8) | ((rect.bottom // 2) << 16)
            self.desktop.send(combo, 0x201, 1, point)
            self.desktop.send(combo, 0x202, 0, point)
            self.assertTrue(self.desktop.send(combo, 0x157), ident)
            self.key(0x1B, hwnd=combo)
            self.assertFalse(self.desktop.send(combo, 0x157), ident)


if __name__ == '__main__':
    unittest.main()
