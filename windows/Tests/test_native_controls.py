"""Real HWND redraw regression. Run with run_isolated.py, never on a user desktop."""
import ctypes
from ctypes import wintypes as w
import time
import unittest
import test_workspace

user = ctypes.WinDLL('user32', use_last_error=True)
user.GetDlgItem.argtypes = [w.HWND, ctypes.c_int]
user.GetDlgItem.restype = w.HWND
user.GetPropW.argtypes = [w.HWND, w.LPCWSTR]
user.GetPropW.restype = w.HANDLE
user.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
user.SendMessageW.restype = ctypes.c_ssize_t
user.UpdateWindow.argtypes = [w.HWND]
user.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, ctypes.c_int]
user.IsWindowVisible.argtypes = [w.HWND]
user.FindWindowExW.argtypes = [w.HWND, w.HWND, w.LPCWSTR, w.LPCWSTR]
user.FindWindowExW.restype = w.HWND
user.GetParent.argtypes = [w.HWND]
user.GetParent.restype = w.HWND
user.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]


class NativeControlTests(unittest.TestCase):
    setUp = test_workspace.WorkspaceTests.setUp
    close_app = test_workspace.WorkspaceTests.close_app
    native_window = test_workspace.WorkspaceTests.native_window
    navigate = test_workspace.WorkspaceTests.navigate
    button = test_workspace.WorkspaceTests.button

    def precise_window(self):
        # These cases open the default secondary dock, not a floating editor.
        main = self.native_window()
        owner = user.FindWindowExW(main, None, 'ScreamSeq.PreciseNotes', None)
        self.assertTrue(owner, 'Missing retained Precise Notes dock owner')
        self.assertFalse(user.FindWindowExW(main, owner, 'ScreamSeq.PreciseNotes', None),
                         'More than one Precise Notes dock owner')
        pid = w.DWORD()
        self.assertTrue(user.GetWindowThreadProcessId(owner, ctypes.byref(pid)))
        self.assertEqual(pid.value, self.process.pid)
        self.assertEqual(user.GetParent(owner), main)
        return owner

    def precise_ready(self):
        deadline = time.monotonic() + 5
        last = None
        while (remaining := deadline - time.monotonic()) > 0:
            # Observe cached owner state; never retry a command or a failed read.
            observer = test_workspace.Client(self.client.pipe, timeout=min(.5, remaining))
            last = observer.call('workspace.get')['data']
            if (not last['documentBusy'] and not last['pendingViewCommands']
                    and not last['noteEditor'].get('pending', False)):
                return last
            time.sleep(min(.01, max(0, deadline - time.monotonic())))
        self.fail(f'Precise Notes did not settle: {last}')

    def control(self, ident, window=None):
        h = user.GetDlgItem(window or self.native_window(), ident)
        self.assertTrue(h, ident)
        self.assertTrue(user.GetPropW(h, 'ScreamSeq.ControlInspection'), ident)
        return h

    def counts(self, handles):
        # Flush only already-invalid areas. This does not force a new paint.
        for h in handles.values():
            user.UpdateWindow(h)
        return {ident: {name: user.GetPropW(h, 'ScreamSeq.' + name) or 0
                        for name in ('PaintCount', 'DrawCount', 'LayoutCount',
                                     'TextCount', 'SelectionCount', 'EnableCount')}
                for ident, h in handles.items()}

    def test_cursor_navigation_does_not_repaint_unchanged_controls(self):
        self.navigate(row=4, following=False)
        # The six prior Notes controls still participate in this exact-flat
        # repaint oracle, now hidden under their sole retained native owner.
        main = self.native_window()
        self.assertFalse(user.FindWindowExW(main, None, 'ScreamSeq.PreciseNotes', None))
        self.assertFalse(user.GetDlgItem(main, 107))  # Entry is a command; 569 is the inspector.
        for ident in range(360, 378):
            self.assertFalse(user.GetDlgItem(main, ident), f'Duplicate Main Notes control {ident}')
        song = self.client.call('document.get')
        user.SendMessageW(main, 0x111, 107, 0)
        opened = self.precise_ready()
        self.assertTrue(opened['noteEditor']['visible'])
        owner = self.precise_window()
        note_ids = (363,367,368,370,374,375)
        note_handles = {i: self.control(i, owner) for i in note_ids}
        self.client.call('workspace.panel', {'panel': 'preciseNotes', 'placement': 'hide'})
        hidden = self.precise_ready()
        self.assertFalse(hidden['noteEditor']['visible'])
        self.assertFalse(user.IsWindowVisible(owner))
        self.assertEqual(self.client.call('document.get'), song)
        # Include all the original Main controls, retaining legacy inspector
        # coverage via 569 instead of the former editable Notes button 107.
        ids = [101,102,103,104,105,106,569,108,109,110,111,112,
               119,120,121,122,123,130,131,132,133,134,135,200,
               202,203,204,205,206,207,208,214,215,217,218,230,231,
               304,305,307,308,317,322,402,407,408,507]
        handles = {i: self.control(i) for i in ids}
        handles.update(note_handles)
        ids += list(note_ids)
        before = self.counts(handles)
        revision = self.client.call('document.get')['revision']
        for _ in range(32):
            user.SendMessageW(self.native_window(), 0x100, 0x28, 0)  # real Down key handler
            self.counts(handles)
        self.assertEqual(self.client.call('context.get')['data']['row'], 36)
        self.assertEqual(self.client.call('document.get')['revision'], revision)
        after = self.counts(handles)
        self.assertEqual(after, before, {i: (before[i], after[i]) for i in ids if before[i] != after[i]})
        self.assertEqual(self.precise_window(), owner)
        self.assertEqual({i: self.control(i, owner) for i in note_ids}, note_handles)
        self.assertFalse(user.IsWindowVisible(owner))

    def test_changed_labels_layout_and_active_buttons_still_update(self):
        self.navigate(row=4, following=False)
        handles = {i: self.control(i) for i in (103,104,105,569,108,130,230)}
        before = self.counts(handles)
        self.button('Pattern focus')
        after = self.counts(handles)
        for ident in (104,105):
            self.assertGreater(after[ident]['DrawCount'], before[ident]['DrawCount'])
        self.assertTrue(self.client.call('workspace.get')['data']['focusLayout'])
        self.button('Compose')
        self.button('Detached [F]')
        text = ctypes.create_unicode_buffer(128)
        user.GetWindowTextW(handles[103], text, 128)
        self.assertEqual(text.value, 'Follow on [F]')
        self.assertGreater(self.counts(handles)[103]['TextCount'], before[103]['TextCount'])

    def test_flat_selectors_keep_native_selection_and_popup(self):
        combo = self.control(132)  # octave: predictable native list, no song mutation
        initial = user.SendMessageW(combo, 0x147, 0, 0)  # CB_GETCURSEL
        count = user.SendMessageW(combo, 0x146, 0, 0)
        self.assertGreater(count, initial + 1)
        user.SendMessageW(combo, 0x100, 0x28, 0)
        self.assertEqual(user.SendMessageW(combo, 0x147, 0, 0), initial + 1)
        user.SendMessageW(combo, 0x100, 0x73, 0)  # F4 opens native popup
        self.assertEqual(user.SendMessageW(combo, 0x157, 0, 0), 1)
        user.SendMessageW(combo, 0x100, 0x1B, 0)  # Escape dismisses
        self.assertEqual(user.SendMessageW(combo, 0x157, 0, 0), 0)
        self.navigate(row=8)
        self.assertEqual(user.SendMessageW(combo, 0x147, 0, 0), initial + 1)

    def test_modeless_selectors_keep_native_keyboard_and_retained_bounds(self):
        self.button('Instrument…')
        window = self.native_window('ScreamSeq.InstrumentEnvelope')
        combo = self.control(4402, window)  # volume / pan / pitch envelope
        self.assertGreaterEqual(user.SendMessageW(combo, 0x146, 0, 0), 3)
        before = self.counts({4402: combo})[4402]
        initial = user.SendMessageW(combo, 0x147, 0, 0)
        user.SendMessageW(combo, 0x100, 0x28, 0)
        self.assertEqual(user.SendMessageW(combo, 0x147, 0, 0), initial + 1)
        self.assertEqual(self.counts({4402: combo})[4402]['LayoutCount'], before['LayoutCount'])
        user.SendMessageW(combo, 0x100, 0x73, 0)
        self.assertEqual(user.SendMessageW(combo, 0x157, 0, 0), 1)
        user.SendMessageW(combo, 0x100, 0x1B, 0)
        self.assertEqual(user.SendMessageW(combo, 0x157, 0, 0), 0)


if __name__ == '__main__':
    unittest.main()
