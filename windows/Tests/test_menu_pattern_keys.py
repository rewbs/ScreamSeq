"""Menu, selector mouse input and pattern shortcuts on an owned private desktop."""
import ctypes
from ctypes import wintypes as w
import unittest
import test_workspace_shortcuts as shortcuts

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

    def cell(self, row=8, channel=0):
        cell = self.read('pattern.get', pattern=0, startRow=row, rowCount=1,
                         startChannel=channel, channelCount=1)['cells'][0]
        return [cell[k] for k in ('note', 'instrument', 'volumeCommand', 'volume', 'effect', 'parameter')]

    def seed(self):
        self.write('pattern.apply', cells=[dict(pattern=0, row=8, channel=0,
            note=61, instrument=1, volumeCommand=1, volume=40, effect=1, parameter=12)])
        self.focus_pattern()

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
