"""Disposable native workspace integration. Never opens audio or a user song."""
import os
import ctypes
from ctypes import wintypes
from pathlib import Path
import subprocess
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'windows/Api'))
from client import Client, ApiError, TransportError

# Match the target HWND's awareness so SendMessage mouse coordinates are not
# virtualized a second time when the test process runs on a scaled desktop.
ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))


class WorkspaceTests(unittest.TestCase):
    def setUp(self):
        exe = Path(os.environ.get('SCREAMSEQ_TEST_EXE', ROOT / 'bin/windows-arm64/Release/ScreamSeq.exe'))
        self.process = subprocess.Popen([str(exe), '--inspection', '--automation', '--seconds', '60'])
        self.addCleanup(self.close_app)
        self.client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(self.process.pid), timeout=1)
        for _ in range(60):
            self.assertIsNone(self.process.poll(), 'app exited before its API was ready')
            try:
                self.client.call('document.get')
                break
            except TransportError:
                time.sleep(.1)
        else:
            self.fail('app pipe did not become ready')

    def close_app(self):
        if self.process.poll() is None:
            self.process.terminate()
        self.process.wait(timeout=10)

    def navigate(self, **fields):
        before = self.client.call('context.get')
        return self.client.call('context.set', {
            'expectedRevision': before['revision'],
            'expectedContext': before['data']['contextRevision'], **fields})

    def native_window(self, class_name='ScreamSeqWindowsDevelopment'):
        user = ctypes.WinDLL('user32', use_last_error=True)
        callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        handles = []
        user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        @callback
        def visit(hwnd, _):
            pid = wintypes.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            name = ctypes.create_unicode_buffer(128)
            user.GetClassNameW(hwnd, name, len(name))
            if pid.value == self.process.pid and name.value == class_name:
                handles.append(hwnd)
            return True
        user.EnumWindows(visit, 0)
        self.assertEqual(len(handles), 1, 'exact task-owned native window')
        return handles[0]

    def button(self, title):
        user = ctypes.WinDLL('user32', use_last_error=True)
        user.FindWindowExW.argtypes = [wintypes.HWND, wintypes.HWND, wintypes.LPCWSTR, wintypes.LPCWSTR]
        user.FindWindowExW.restype = wintypes.HWND
        control = user.FindWindowExW(self.native_window(), None, 'BUTTON', title)
        self.assertTrue(control, 'Missing real native button: ' + title)
        user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        user.SendMessageW(control, 0xF5, 0, 0)  # BM_CLICK, app's actual command handler

    def test_native_controls_use_retained_workspace_path(self):
        self.button('Pattern focus')
        self.assertTrue(self.client.call('workspace.get')['data']['focusLayout'])
        self.button('Compose')
        self.button('Notes')
        self.navigate(row=8)
        self.button('Follow target')
        self.assertTrue(self.client.call('workspace.get')['data']['pins']['notes'])
        self.navigate(row=24)
        self.assertEqual(self.client.call('workspace.get')['data']['inspection']['notes']['row'], 8)
        self.button('Cursor')
        self.assertEqual(self.client.call('workspace.get')['data']['inspection']['notes']['row'], 24)
        self.button('Return')
        self.assertEqual(self.client.call('context.get')['data']['row'], 0)
        self.button('Sound design')
        self.assertEqual(self.client.call('workspace.get')['data']['right'], 'samples')
        self.button('Commands [Ctrl+K]')
        palette = self.native_window('ScreamSeqCommands')
        user = ctypes.WinDLL('user32', use_last_error=True)
        user.FindWindowExW.argtypes = [wintypes.HWND, wintypes.HWND, wintypes.LPCWSTR, wintypes.LPCWSTR]
        user.FindWindowExW.restype = wintypes.HWND
        edit = user.FindWindowExW(palette, None, 'EDIT', None)
        self.assertTrue(edit, 'Palette needs a real native search field')
        user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        query = ctypes.create_unicode_buffer('Pattern focus')
        user.SendMessageW(edit, 0xC, 0, ctypes.addressof(query))
        user.SendMessageW(edit, 0x100, 0x0D, 0)
        self.assertTrue(self.client.call('workspace.get')['data']['focusLayout'])

    def test_grid_selection_and_dock_resize_are_readable(self):
        state = self.client.call('workspace.get')['data']
        self.assertIn('geometry', state)
        user = ctypes.WinDLL('user32', use_last_error=True)
        user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        hwnd = self.native_window()
        scale = state['dpi'] / 96
        def mouse(message, x, y, buttons=0):
            packed = round(x * scale) | (round(y * scale) << 16)
            user.SendMessageW(hwnd, message, buttons, packed)
        grid = state['geometry']['pattern']
        # Two ordinary cells, not platform-specific musical data.
        x, y = grid['x'] + 44, grid['y'] + 56
        mouse(0x201, x, y, 1)
        mouse(0x200, x + 112, y + 54, 1)
        # Capture can queue hover messages at the physical cursor location;
        # no-button motion must not redirect an active selection.
        mouse(0x200, x + 224, y + 90, 0)
        mouse(0x202, x + 112, y + 54)
        context = self.client.call('context.get')['data']
        self.assertEqual(context['selection'], {'startRow': 0, 'endRow': 3, 'startChannel': 0, 'endChannel': 1})
        self.assertFalse(context['following'])
        self.assertEqual(self.client.call('workspace.get')['data']['focus'], 'pattern')
        divider = state['geometry']['verticalDivider']
        x, y = divider['x'] + 3, divider['y'] + 80
        mouse(0x201, x, y, 1)
        mouse(0x200, x - 40, y, 1)
        mouse(0x202, x - 40, y)
        updated = self.client.call('workspace.get')['data']
        self.assertGreater(updated['rightWidth'], state['rightWidth'])
        self.assertEqual(updated['inspection']['notes']['row'], 3)
        self.assertEqual(self.client.call('context.get')['data'], context)

    def mouse(self, message, x, y, buttons=0):
        scale = self.client.call('workspace.get')['data']['dpi'] / 96
        user = ctypes.WinDLL('user32', use_last_error=True)
        user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        packed = round(x * scale) | (round(y * scale) << 16)
        user.SendMessageW(self.native_window(), message, buttons, packed)

    def click_cell(self, row, channel, shift=False):
        state = self.client.call('workspace.get')['data']
        grid, viewport = state['geometry']['pattern'], state['viewport']
        x = grid['x'] + 44 + (channel - viewport['firstChannel']) * 112
        y = grid['y'] + 56 + (row - viewport['firstRow']) * 18
        self.assertLess(x, grid['x'] + grid['width'])
        self.assertLess(y, grid['y'] + grid['height'])
        self.mouse(0x201, x, y, 1 | (4 if shift else 0))  # MK_LBUTTON | MK_SHIFT
        self.mouse(0x202, x, y)

    def test_shift_click_starts_from_cursor_then_retains_anchor(self):
        self.navigate(row=12, channel=1)
        self.click_cell(16, 2, shift=True)
        self.assertEqual(self.client.call('context.get')['data']['selection'],
                         {'startRow': 12, 'endRow': 16, 'startChannel': 1, 'endChannel': 2})
        self.click_cell(10, 0, shift=True)
        self.assertEqual(self.client.call('context.get')['data']['selection'],
                         {'startRow': 10, 'endRow': 12, 'startChannel': 0, 'endChannel': 1})
        self.navigate(row=14, channel=2)
        self.click_cell(17, 3, shift=True)
        self.assertEqual(self.client.call('context.get')['data']['selection'],
                         {'startRow': 14, 'endRow': 17, 'startChannel': 2, 'endChannel': 3})

    def assert_unpin_follows_cursor(self, unpin):
        origin = self.client.call('workspace.get')['data']['returnPoints']['notes']
        self.navigate(row=4)
        self.client.call('workspace.panel', {'panel': 'notes', 'pinned': True})
        self.navigate(row=16)
        self.assertEqual(self.client.call('workspace.get')['data']['inspection']['notes']['row'], 4)
        unpin()
        state = self.client.call('workspace.get')['data']
        self.assertFalse(state['pins']['notes'])
        self.assertEqual(state['inspection']['notes']['row'], 16)
        self.assertEqual(state['returnPoints']['notes'], origin)
        self.client.call('workspace.panel', {'panel': 'notes', 'return': True})
        self.assertEqual(self.client.call('context.get')['data']['row'], origin['row'])

    def test_api_unpin_immediately_follows_cursor(self):
        self.assert_unpin_follows_cursor(lambda: self.client.call(
            'workspace.panel', {'panel': 'notes', 'pinned': False}))

    def test_native_pin_button_unpin_immediately_follows_cursor(self):
        self.assert_unpin_follows_cursor(lambda: self.button('Pinned'))

    def test_placement_only_preserves_selected_panel_and_focus(self):
        self.client.call('workspace.panel', {'panel': 'notes', 'focus': True})
        self.navigate(row=12)
        context = self.client.call('context.get')
        before = self.client.call('workspace.get')['data']
        for fields in ({'placement': 'right'}, {'placement': 'right', 'focus': False}):
            with self.subTest(fields=fields):
                self.client.call('workspace.panel', {'panel': 'samples', **fields})
                state = self.client.call('workspace.get')['data']
                self.assertEqual((state['right'], state['focus'], state['visible']),
                                 (before['right'], before['focus'], before['visible']))
                self.assertEqual(state['returnPoints']['notes'], before['returnPoints']['notes'])
                self.assertEqual(self.client.call('context.get'), context)
        self.client.call('workspace.panel', {'panel': 'samples', 'placement': 'right', 'focus': True})
        state = self.client.call('workspace.get')['data']
        self.assertEqual((state['right'], state['focus'], state['visible']), ('samples', 'samples', ['samples']))
        self.assertEqual(self.client.call('context.get'), context)

    def test_inactive_panel_hide_retains_independent_placement(self):
        self.navigate(row=4)
        self.client.call('workspace.panel', {'panel': 'samples', 'focus': True, 'pinned': True})
        self.client.call('workspace.panel', {'panel': 'notes', 'focus': True})
        self.navigate(row=12)
        before = self.client.call('workspace.get')['data']
        context = self.client.call('context.get')
        self.client.call('workspace.panel', {'panel': 'samples', 'placement': 'hide'})
        state = self.client.call('workspace.get')['data']
        self.assertEqual(state['locations'], {'notes': 'right', 'samples': 'hide'})
        self.assertEqual((state['right'], state['focus'], state['visible']), ('notes', 'notes', ['notes']))
        for name in ('Pattern focus', 'Compose'):
            self.client.call('workspace.layout', {'name': name})
            self.assertEqual(self.client.call('workspace.get')['data']['locations']['samples'], 'hide')
        self.client.call('workspace.panel', {'panel': 'samples', 'placement': 'right'})
        state = self.client.call('workspace.get')['data']
        self.assertEqual(state['locations'], {'notes': 'right', 'samples': 'right'})
        self.assertEqual(state['right'], 'notes')
        for field in ('pins', 'inspection', 'returnPoints'):
            self.assertEqual(state[field], before[field])
        self.assertEqual(self.client.call('context.get'), context)

    def test_hiding_active_panel_uses_only_a_placed_fallback(self):
        self.client.call('workspace.panel', {'panel': 'notes', 'focus': True})
        self.client.call('workspace.panel', {'panel': 'notes', 'placement': 'hide'})
        state = self.client.call('workspace.get')['data']
        self.assertEqual((state['right'], state['focus'], state['visible']), ('samples', 'pattern', ['samples']))
        self.assertEqual(state['locations'], {'notes': 'hide', 'samples': 'right'})
        self.client.call('workspace.panel', {'panel': 'samples', 'placement': 'hide'})
        state = self.client.call('workspace.get')['data']
        self.assertEqual((state['right'], state['focus'], state['visible']), ('', 'pattern', []))
        self.assertEqual(state['locations'], {'notes': 'hide', 'samples': 'hide'})
        self.client.call('workspace.panel', {'panel': 'notes', 'placement': 'right'})
        state = self.client.call('workspace.get')['data']
        self.assertEqual((state['right'], state['focus'], state['visible']), ('notes', 'pattern', ['notes']))
        self.assertEqual(state['locations']['samples'], 'hide')
        # Explicit focus reopens a hidden panel; it does not lose either panel's placement.
        self.client.call('workspace.panel', {'panel': 'samples', 'focus': True})
        state = self.client.call('workspace.get')['data']
        self.assertEqual((state['right'], state['focus'], state['visible']), ('samples', 'samples', ['samples']))
        self.assertEqual(state['locations'], {'notes': 'right', 'samples': 'right'})
        # Complete validation must precede placement changes.
        with self.assertRaises(ApiError) as invalid:
            self.client.call('workspace.panel', {'panel': 'samples', 'placement': 'hide', 'pinned': 1})
        self.assertEqual(invalid.exception.code, -32602)
        self.assertEqual(self.client.call('workspace.get')['data'], state)

    def test_describe_advertises_method_specific_revision_guards(self):
        description = self.client.call('api.describe')['data']
        expected = {'transport.play': ['expectedRevision'], 'transport.stop': ['expectedRevision'],
                    'context.set': ['expectedRevision', 'expectedContext'],
                    'workspace.panel': [], 'workspace.layout': []}
        for method, guards in expected.items():
            self.assertEqual(description.get('revisionGuards', {}).get(method), guards)
        for method in ('pattern.apply', 'history.undo', 'history.redo', 'document.patch',
                       'pattern.create', 'order.edit', 'sequence.select', 'document.save', 'document.open'):
            self.assertEqual(description['revisionGuards'].get(method), ['expectedRevision'])
        self.assertNotIn('Mutations require expectedRevision', description['transport'])
        context = self.client.call('context.get')
        tokens = {'expectedRevision': context['revision'],
                  'expectedContext': context['data']['contextRevision']}
        for method, required in expected.items():
            fields = {'context.set': {'row': 1}, 'workspace.panel': {'panel': 'notes'},
                      'workspace.layout': {'name': 'Compose'}}.get(method, {})
            for token in required:
                with self.subTest(method=method, missing=token):
                    params = {**fields, **{key: tokens[key] for key in required if key != token}}
                    with self.assertRaises(ApiError) as invalid:
                        self.client.call(method, params)
                    self.assertEqual(invalid.exception.code, -32602)
            if not required:
                self.client.call(method, fields)
                for token in tokens:
                    before = self.client.call('workspace.get')['data']
                    with self.assertRaises(ApiError) as invalid:
                        self.client.call(method, {**fields, token: tokens[token]})
                    self.assertEqual(invalid.exception.code, -32602)
                    self.assertEqual(self.client.call('workspace.get')['data'], before)
        self.assertEqual(self.client.call('context.get'), context)
        self.assertFalse(self.client.call('transport.get')['data']['playing'])

    def test_describe_advertises_only_the_supported_workspace_subset(self):
        description = self.client.call('api.describe')['data']
        self.assertEqual(description.get('workspaceSubset'), {
            'panels': ['notes', 'samples'], 'placements': ['right', 'hide'],
            'layouts': ['Compose', 'Pattern focus', 'Sound design']})
        for placement in ('bottom', 'secondary', 'float'):
            before = self.client.call('workspace.get')['data']
            with self.assertRaises(ApiError) as invalid:
                self.client.call('workspace.panel', {'panel': 'notes', 'placement': placement})
            self.assertEqual(invalid.exception.code, -32602)
            self.assertEqual(self.client.call('workspace.get')['data'], before)
        for layout in ('Save custom', 'Restore custom'):
            before = self.client.call('workspace.get')['data']
            with self.assertRaises(ApiError) as invalid:
                self.client.call('workspace.layout', {'name': layout})
            self.assertEqual(invalid.exception.code, -32602)
            self.assertEqual(self.client.call('workspace.get')['data'], before)

    def assert_wheel_steps(self, steps):
        self.resize_client(1057, 719)
        self.navigate(row=40, channel=1, following=True)
        before = self.client.call('context.get')
        first = self.client.call('workspace.get')['data']['viewport']['firstRow']
        user = ctypes.WinDLL('user32', use_last_error=True)
        user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        hwnd = self.native_window()
        for delta, offset in steps:
            with self.subTest(delta=delta, expectedOffset=offset):
                user.SendMessageW(hwnd, 0x20A, (delta & 0xFFFF) << 16, 0)  # WM_MOUSEWHEEL
                state = self.client.call('workspace.get')['data']
                self.assertEqual(state['viewport']['firstRow'], first + offset)
                context = self.client.call('context.get')
                self.assertFalse(context['data']['following'])
                for field in ('pattern', 'row', 'channel', 'column', 'selection'):
                    self.assertEqual(context['data'][field], before['data'][field])
                self.assertEqual(context['revision'], before['revision'])
        self.assertFalse(self.client.call('document.get')['data']['canUndo'])
        self.assertFalse(self.client.call('transport.get')['data']['playing'])

    def test_high_resolution_wheel_accumulates_partial_detents(self):
        self.assert_wheel_steps([(-30, 0), (-30, 0), (-30, 0), (-30, 3), (60, 3), (60, 0)])

    def test_high_resolution_wheel_reversal_cancels_partial_deltas(self):
        self.assert_wheel_steps([(-90, 0), (60, 0), (-60, 0), (-30, 3),
                                 (90, 3), (-30, 3), (60, 0), (-150, 3), (270, -3)])

    def resize_client(self, width, height):
        user = ctypes.WinDLL('user32', use_last_error=True)
        user.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
        user.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
        user.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int,
                                     ctypes.c_int, ctypes.c_int, wintypes.UINT]
        hwnd = self.native_window()
        client, outer = wintypes.RECT(), wintypes.RECT()
        self.assertTrue(user.GetClientRect(hwnd, ctypes.byref(client)))
        self.assertTrue(user.GetWindowRect(hwnd, ctypes.byref(outer)))
        scale = self.client.call('workspace.get')['data']['dpi'] / 96
        self.assertTrue(user.SetWindowPos(hwnd, None, 0, 0,
            round(width * scale) + outer.right - outer.left - client.right,
            round(height * scale) + outer.bottom - outer.top - client.bottom,
            0x2 | 0x4 | 0x10))  # NOMOVE | NOZORDER | NOACTIVATE

    def assert_grid_remainder_ignored(self, axis):
        self.resize_client(1057, 719)  # Complete cells plus a deliberate remainder on both axes.
        self.navigate(row=4, channel=0, following=False)
        state = self.client.call('workspace.get')['data']
        grid = state['geometry']['pattern']
        rows, channels = int((grid['height'] - 50) // 18), int((grid['width'] - 38) // 112)
        self.assertGreater(grid['height'] - 50 - rows * 18, 2)
        self.assertGreater(grid['width'] - 38 - channels * 112, 2)
        # The last wholly rendered cell remains a valid hit.
        self.click_cell(state['viewport']['firstRow'] + rows - 1,
                        state['viewport']['firstChannel'] + channels - 1)
        self.assertEqual(self.client.call('context.get')['data']['row'], rows - 1)
        self.navigate(row=4, channel=0, following=False)
        self.client.call('workspace.panel', {'panel': 'notes', 'focus': True})
        before = self.client.call('context.get')
        x, y = grid['x'] + 44, grid['y'] + 56
        if axis == 'row':
            y = grid['y'] + 50 + rows * 18 + 1
        else:
            x = grid['x'] + 38 + channels * 112 + 1
        self.mouse(0x201, x, y, 1)
        self.mouse(0x202, x, y)
        self.assertEqual(self.client.call('context.get'), before)
        self.assertEqual(self.client.call('workspace.get')['data']['focus'], 'notes')
        # An invalid down must not initiate a selection drag into a valid cell.
        self.mouse(0x201, x, y, 1)
        self.mouse(0x200, grid['x'] + 44, grid['y'] + 56, 1)
        self.mouse(0x202, x, y)
        self.assertEqual(self.client.call('context.get'), before)

    def test_blank_row_remainder_is_not_a_grid_cell(self):
        self.assert_grid_remainder_ignored('row')

    def test_blank_channel_remainder_is_not_a_grid_cell(self):
        self.assert_grid_remainder_ignored('channel')

    def test_guarded_navigation_preserves_document_and_transport(self):
        before = self.client.call('context.get')
        result = self.navigate(row=12, channel=2, column=1, following=False)
        self.assertTrue(result['contextChanged'])
        self.assertFalse(result['changed'])
        after = self.client.call('context.get')
        self.assertEqual((after['data']['row'], after['data']['channel'], after['data']['column']), (12, 2, 1))
        self.assertFalse(after['data']['following'])
        self.assertEqual(after['revision'], before['revision'])
        self.assertFalse(self.client.call('transport.get')['data']['playing'])
        self.assertFalse(self.client.call('document.get')['data']['canUndo'])
        self.assertFalse(self.navigate(row=12)['contextChanged'])
        with self.assertRaises(ApiError) as stale:
            self.client.call('context.set', {'expectedRevision': before['revision'],
                'expectedContext': before['data']['contextRevision'], 'row': 13})
        self.assertEqual(stale.exception.code, -32001)
        for fields in ({'row': True}, {'row': -1}, {'channel': 8}, {'column': 5}, {'following': 1}, {'pattern': 65535}, {'unknown': 0}):
            with self.assertRaises(ApiError) as invalid:
                self.navigate(**fields)
            self.assertEqual(invalid.exception.code, -32602)
            self.assertEqual(self.client.call('context.get')['data'], after['data'])

    def test_retained_inspectors_pin_cursor_return_and_layouts(self):
        original = self.client.call('document.get')['revision']
        self.assertEqual(self.client.call('workspace.get')['data']['focus'], 'pattern')
        self.navigate(row=4, channel=0)
        state = self.client.call('workspace.get')['data']
        self.assertEqual(state['inspection']['notes']['row'], 4)
        self.client.call('workspace.panel', {'panel': 'notes', 'pinned': True})
        self.navigate(row=16, channel=1)
        state = self.client.call('workspace.get')['data']
        self.assertEqual(state['inspection']['notes']['row'], 4)
        self.assertTrue(state['pins']['notes'])
        self.client.call('workspace.panel', {'panel': 'samples', 'focus': True})
        state = self.client.call('workspace.get')['data']
        self.assertEqual(state['inspection']['samples']['sample'], 2)
        self.assertEqual(state['focus'], 'samples')
        for name in ('Pattern focus', 'Sound design', 'Compose'):
            self.client.call('workspace.layout', {'name': name})
            self.assertTrue(self.client.call('workspace.get')['data']['pins']['notes'])
        self.client.call('workspace.panel', {'panel': 'notes', 'follow': True})
        state = self.client.call('workspace.get')['data']
        self.assertFalse(state['pins']['notes'])
        self.assertEqual(state['inspection']['notes']['row'], 16)
        self.client.call('workspace.panel', {'panel': 'notes', 'return': True})
        context = self.client.call('context.get')['data']
        self.assertEqual((context['row'], context['channel']), (0, 0))
        self.assertFalse(context['following'])
        self.assertEqual(self.client.call('workspace.get')['data']['focus'], 'pattern')
        self.client.call('workspace.panel', {'panel': 'samples', 'pinned': True})
        self.client.call('workspace.panel', {'panel': 'samples', 'return': True})
        self.assertEqual(self.client.call('context.get')['data']['row'], 16)
        state = self.client.call('workspace.get')['data']
        for request in ({'panel': 'notes', 'pinned': 1}, {'panel': 'notes', 'placement': 'float', 'pinned': True}, {'panel': 'graph', 'focus': True}):
            with self.assertRaises(ApiError) as invalid:
                self.client.call('workspace.panel', request)
            self.assertEqual(invalid.exception.code, -32602)
            self.assertEqual(self.client.call('workspace.get')['data'], state)
        self.assertEqual(self.client.call('document.get')['revision'], original)
        self.assertFalse(self.client.call('document.get')['data']['canUndo'])


if __name__ == '__main__':
    unittest.main()
