"""Actual native layout workflows. Run through run_isolated.py on its private desktop."""
import ctypes
from ctypes import wintypes as w
import time
import unittest

import test_workspace
from client import ApiError


user = ctypes.WinDLL('user32', use_last_error=True)
user.GetDlgItem.argtypes = [w.HWND, ctypes.c_int]
user.GetDlgItem.restype = w.HWND
user.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
user.SendMessageW.restype = ctypes.c_ssize_t
user.IsWindowVisible.argtypes = [w.HWND]
user.IsWindowEnabled.argtypes = [w.HWND]
user.GetWindowRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, ctypes.c_int]
user.GetWindow.argtypes = [w.HWND, w.UINT]
user.GetWindow.restype = w.HWND
user.ClientToScreen.argtypes = [w.HWND, ctypes.POINTER(w.POINT)]


class WorkspaceLayoutTests(unittest.TestCase):
    # Reuse fixture helpers without inheriting and duplicating its test methods.
    setUp = test_workspace.WorkspaceTests.setUp
    close_app = test_workspace.WorkspaceTests.close_app
    native_window = test_workspace.WorkspaceTests.native_window
    navigate = test_workspace.WorkspaceTests.navigate
    resize_client = test_workspace.WorkspaceTests.resize_client

    def state(self):
        return self.client.call('workspace.get')['data']

    def ready(self):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            state = self.state()
            if not state['documentBusy'] and not state['pendingViewCommands']:
                return state
            time.sleep(.01)
        self.fail('Workspace did not finish its retained view request')

    def command(self, ident):
        user.SendMessageW(self.native_window(), 0x111, ident, 0)
        return self.ready()

    def control(self, ident, window=None):
        result = user.GetDlgItem(window or self.native_window(), ident)
        self.assertTrue(result, f'Missing native control {ident}')
        return result

    def press(self, ident, window=None):
        control = self.control(ident, window)
        self.assertTrue(user.IsWindowVisible(control), f'Control {ident} is hidden')
        self.assertTrue(user.IsWindowEnabled(control), f'Control {ident} is disabled')
        user.SendMessageW(control, 0xF5, 0, 0)  # BM_CLICK uses the real button handler.
        return self.ready()

    def field(self, ident, value, window):
        text = ctypes.create_unicode_buffer(value)
        user.SendMessageW(self.control(ident, window), 0xC, 0, ctypes.addressof(text))

    def select_text(self, ident, value, window):
        control = self.control(ident, window)
        kind = ctypes.create_unicode_buffer(80)
        user.GetClassNameW(control, kind, len(kind))
        combo = kind.value.upper() == 'COMBOBOX'
        self.assertTrue(combo or kind.value.upper() == 'LISTBOX', kind.value)
        text = ctypes.create_unicode_buffer(value)
        index = user.SendMessageW(control, 0x158 if combo else 0x1A2,
                                  -1, ctypes.addressof(text))
        self.assertGreaterEqual(index, 0, f'Missing choice {value!r}')
        user.SendMessageW(control, 0x14E if combo else 0x186, index, 0)
        user.SendMessageW(window, 0x111, ident | (1 << 16), control)
        return self.ready()

    def layout(self, name, **fields):
        self.client.call('workspace.layout', {'name': name, **fields})
        return self.ready()

    def assert_song_unchanged(self, before):
        self.assertEqual(self.client.call('document.get'), before)
        self.assertFalse(self.client.call('transport.get')['data']['playing'])

    def test_named_layout_restores_docks_and_sizes_without_moving_cursor_or_targets(self):
        song = self.client.call('document.get')
        self.navigate(row=8, channel=1, following=False)
        self.client.call('workspace.panel', {'panel': 'notes', 'pinned': True})
        self.press(524)  # Mixer.
        self.command(115)  # Wider inspector.
        self.command(117)  # Taller lower dock.
        saved = self.state()
        self.layout('Save custom', savedName='Writing and routing')
        self.assertIn('Writing and routing', self.state()['savedLayouts'])

        self.navigate(row=28, channel=2, following=False)
        self.layout('Sound design')
        self.command(116)
        self.press(526)  # Collapse.
        self.client.call('workspace.panel', {'panel': 'notes', 'pinned': False})
        before_context = self.client.call('context.get')
        before_targets = self.state()['inspection']
        before_origins = self.state()['returnPoints']
        before_pins = self.state()['pins']
        restored = self.layout('Restore custom', savedName='Writing and routing')
        for key in ('rightWidth', 'lowerHeight', 'lowerVisible', 'lowerEditor', 'locations'):
            self.assertEqual(restored[key], saved[key], key)
        self.assertEqual(restored['pins'], before_pins)
        self.assertEqual(self.client.call('context.get'), before_context)
        self.assertEqual(restored['inspection'], before_targets)
        self.assertEqual(restored['returnPoints'], before_origins)
        self.assert_song_unchanged(song)

    def test_default_layout_save_replace_restore_and_delete(self):
        song = self.client.call('document.get')
        self.press(525)
        self.layout('Save custom')
        self.assertIn('Custom', self.state()['savedLayouts'])
        self.press(523)
        self.assertEqual(self.layout('Restore custom')['lowerEditor'], 'graph')
        self.press(521)
        self.layout('Save custom')
        self.assertEqual(self.state()['savedLayouts'].count('Custom'), 1)
        self.layout('Pattern focus')
        self.assertEqual(self.layout('Restore custom')['lowerEditor'], 'samples')
        self.assertNotIn('Custom', self.layout('Delete custom')['savedLayouts'])
        self.assert_song_unchanged(song)

    def test_layout_api_rejects_invalid_requests_atomically(self):
        self.layout('Save custom', savedName='Keep')
        song = self.client.call('document.get')
        for params in (
                {'name': 'Save custom', 'savedName': ''},
                {'name': 'Save custom', 'savedName': ' '},
                {'name': 'Save custom', 'savedName': 'x' * 1000},
                {'name': 'Save custom', 'savedName': True},
                {'name': 'Save custom', 'savedName': 'x\0y'},
                {'name': 'Restore custom', 'savedName': 'Missing'},
                {'name': 'Delete custom', 'savedName': 'Missing'},
                {'name': 'Compose', 'savedName': 'Keep'},
                {'name': 'Save custom', 'savedName': 'Keep', 'unexpected': 1},
                {'name': False},
                {'name': 'Unrecognized preset'}):
            with self.subTest(params=params):
                before = self.state()
                context = self.client.call('context.get')
                with self.assertRaises(ApiError) as rejected:
                    self.client.call('workspace.layout', params)
                self.assertEqual(rejected.exception.code, -32602)
                self.assertEqual(self.state(), before)
                self.assertEqual(self.client.call('context.get'), context)
        self.assert_song_unchanged(song)

    def test_native_dock_tabs_collapse_and_reopen_keep_edit_context(self):
        song = self.client.call('document.get')
        self.navigate(row=12, channel=1, following=False)
        context = self.client.call('context.get')
        for ident, editor, snapshot in (
                (523, 'plugins', None), (524, 'mixer', 'mixerEditor'),
                (525, 'graph', 'graphEditor'), (522, 'effects', 'effectEditor'),
                (520, 'notes', 'noteEditor'), (521, 'samples', None)):
            with self.subTest(editor=editor):
                state = self.press(ident)
                self.assertEqual(state['lowerEditor'], editor)
                self.assertTrue(state['lowerVisible'])
                self.assertGreater(state['geometry']['lowerTabs']['height'], 0)
                if snapshot:
                    self.assertTrue(state[snapshot]['visible'])
                self.assertEqual(self.client.call('context.get'), context)
        expanded_height = self.state()['geometry']['pattern']['height']
        collapsed = self.press(526)
        self.assertFalse(collapsed['lowerVisible'])
        self.assertGreater(collapsed['geometry']['pattern']['height'], expanded_height)
        self.assertTrue(user.IsWindowVisible(self.control(521)))
        self.assertFalse(user.IsWindowVisible(self.control(230)))  # Sample selection field.
        self.assertEqual(self.press(521)['lowerEditor'], 'samples')
        self.assertTrue(self.state()['lowerVisible'])
        self.press(526)
        reopened = self.press(400)  # The persistent top toolbar's Mixer button.
        self.assertTrue(reopened['lowerVisible'])
        self.assertEqual(reopened['lowerEditor'], 'mixer')
        self.assertTrue(user.IsWindowVisible(self.control(524)))
        self.assertEqual(self.client.call('context.get'), context)
        self.assert_song_unchanged(song)

    def test_native_layout_manager_edits_selects_restores_and_deletes_named_layout(self):
        song = self.client.call('document.get')
        self.press(525)
        self.press(513)
        manager = self.native_window('ScreamSeqWorkspaceLayouts')
        self.field(5, 'Graph study', manager)
        self.press(6, manager)
        self.assertIn('Graph study', self.state()['savedLayouts'])
        self.select_text(1, 'Pattern focus', manager)
        self.press(2, manager)
        self.assertTrue(self.state()['focusLayout'])
        self.select_text(3, 'Graph study', manager)
        self.press(4, manager)
        self.assertEqual(self.state()['lowerEditor'], 'graph')
        self.assertFalse(self.state()['focusLayout'])
        self.press(7, manager)
        self.assertNotIn('Graph study', self.state()['savedLayouts'])
        self.press(8, manager)
        self.assertFalse(user.IsWindowVisible(manager))
        self.press(513)
        self.assertEqual(self.native_window('ScreamSeqWorkspaceLayouts'), manager)
        self.assert_song_unchanged(song)

    def test_layout_switch_keeps_captured_precise_note_draft(self):
        song = self.client.call('document.get')
        self.navigate(row=0, channel=0, following=False)
        self.press(520)
        initial_count = self.state()['noteEditor']['draftCount']
        self.press(369)  # Add a hit to the captured draft without applying it.
        draft = self.state()['noteEditor']
        self.assertEqual(draft['draftCount'], initial_count + 1)
        self.layout('Save custom', savedName='Note detail')
        self.navigate(row=24, channel=1, following=False)
        self.press(525)
        context = self.client.call('context.get')
        restored = self.layout('Restore custom', savedName='Note detail')
        self.assertEqual(restored['lowerEditor'], 'notes')
        self.assertTrue(restored['noteEditor']['visible'])
        for field in ('pattern', 'row', 'channel', 'draftCount', 'selectedEvent', 'expectedRevision'):
            self.assertEqual(restored['noteEditor'][field], draft[field], field)
        self.assertEqual(self.client.call('context.get'), context)
        self.assert_song_unchanged(song)

    def test_lower_dock_tabs_stay_in_bounds_at_compact_window_size(self):
        self.resize_client(1057, 719)
        self.press(524)
        rectangles = []
        for ident in (520, 521, 522, 523, 524, 525, 526):
            control = self.control(ident)
            self.assertTrue(user.IsWindowVisible(control))
            rect = w.RECT()
            self.assertTrue(user.GetWindowRect(control, ctypes.byref(rect)))
            rectangles.append((ident, rect))
        for (left_id, left), (right_id, right) in zip(rectangles, rectangles[1:]):
            self.assertLessEqual(left.right, right.left, (left_id, right_id))
            self.assertEqual(left.top, right.top)
        outer = w.RECT()
        self.assertTrue(user.GetWindowRect(self.native_window(), ctypes.byref(outer)))
        for ident, rect in rectangles:
            self.assertGreaterEqual(rect.left, outer.left, ident)
            self.assertLessEqual(rect.right, outer.right, ident)
            self.assertGreater(rect.bottom, rect.top, ident)

    def test_toolbar_and_pattern_actions_have_distinct_visible_hit_targets(self):
        self.resize_client(1057, 719)
        self.assertFalse(user.IsWindowVisible(self.control(316)),
                         'The old Plugins toolbar button overlaps the layout manager')
        self.assertFalse(user.IsWindowVisible(self.control(430)),
                         'The old Graph toolbar button overlaps Live keys')
        origin = w.POINT(0, 0)
        self.assertTrue(user.ClientToScreen(self.native_window(), ctypes.byref(origin)))
        state = self.state()
        scale = state['dpi'] / 96
        bottom = origin.y + (state['geometry']['pattern']['y'] + 27) * scale
        rectangles = []
        # Enumerate all visible native children in the toolbar/header instead
        # of a curated ID list that could miss a legacy overlapping button.
        control = user.GetWindow(self.native_window(), 5)  # GW_CHILD
        while control:
            rect = w.RECT()
            self.assertTrue(user.GetWindowRect(control, ctypes.byref(rect)))
            if user.IsWindowVisible(control) and rect.top < bottom:
                rectangles.append((user.GetDlgCtrlID(control), rect))
            control = user.GetWindow(control, 2)  # GW_HWNDNEXT
        self.assertGreaterEqual(len(rectangles), 16)
        for index, (left_id, left) in enumerate(rectangles):
            for right_id, right in rectangles[index + 1:]:
                overlaps = (max(left.left, right.left) < min(left.right, right.right)
                            and max(left.top, right.top) < min(left.bottom, right.bottom))
                self.assertFalse(overlaps, f'Native controls {left_id} and {right_id} overlap')


if __name__ == '__main__':
    unittest.main()
