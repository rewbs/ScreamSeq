"""Native fixture helpers for the sole retained Precise Notes HWND.

No TestCase base and no alternate application/API path. Discovery and keyboard
messages stay on the fixture PID/private desktop. Mutations are never retried.
"""
import ctypes
from ctypes import wintypes as w
import time

import private_desktop


user = private_desktop.user
user.GetDlgItem.argtypes = [w.HWND, ctypes.c_int]
user.GetDlgItem.restype = w.HWND
user.GetDpiForWindow.argtypes = [w.HWND]
user.GetParent.argtypes = [w.HWND]
user.GetParent.restype = w.HWND
user.IsChild.argtypes = [w.HWND, w.HWND]
user.IsWindow.argtypes = [w.HWND]
user.IsWindowVisible.argtypes = [w.HWND]
user.IsWindowEnabled.argtypes = [w.HWND]
user.GetClientRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
user.GetWindowRect.argtypes = user.GetClientRect.argtypes
user.SetWindowPos.argtypes = [w.HWND, w.HWND, ctypes.c_int, ctypes.c_int,
                            ctypes.c_int, ctypes.c_int, w.UINT]
user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
user.EnumChildWindows.argtypes = [w.HWND, private_desktop.callback, w.LPARAM]


class PreciseNoteNativeMixin:
    @staticmethod
    def note_identifier(identifier):
        return 360 <= identifier <= 377 or 9200 <= identifier <= 9205

    def note_windows(self):
        found = set()

        @private_desktop.callback
        def child(hwnd, _):
            pid = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            name = ctypes.create_unicode_buffer(128)
            user.GetClassNameW(hwnd, name, len(name))
            if pid.value == self.pid and name.value == 'ScreamSeq.PreciseNotes':
                found.add(int(hwnd))
            return True

        @private_desktop.callback
        def root(hwnd, unused):
            child(hwnd, unused)
            pid = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value == self.pid:
                user.EnumChildWindows(hwnd, child, 0)
            return True

        private_desktop.check(user.EnumDesktopWindows(self.desktop.desktop, root, 0))
        return sorted(found)

    def note_hwnd(self):
        found = self.note_windows()
        self.assertEqual(len(found), 1, found)
        return found[0]

    def editor(self):
        return self.read('workspace.get')['preciseNotes']

    def note_idle(self, control=None):
        # Only cached workspace reads; the deadline also bounds each pipe call.
        # No busy/API/transport errors are swallowed, and no action is repeated.
        deadline = time.monotonic() + 5
        state = None
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            observer = self.client.__class__(self.client.pipe, timeout=min(.5, remaining))
            state = observer.call('workspace.get')['data']
            pending = state['documentBusy'] or state['pendingViewCommands']
            pending |= any(state[key].get('pending', False) for key in
                           ('preciseNotes', 'graphEditor', 'mixerEditor',
                            'parameterAutomation', 'instrumentEnvelope', 'graphCurve'))
            usable = control is None or (user.IsWindow(control) and
                user.IsWindowVisible(control) and user.IsWindowEnabled(control))
            if not pending and usable:
                return state
            remaining = deadline - time.monotonic()
            if remaining > 0:
                time.sleep(min(.01, remaining))
        self.fail(str({'workspace': state, 'control': control}))

    def raw_note_control(self, identifier):
        result = user.GetDlgItem(self.note_hwnd(), identifier)
        self.assertTrue(result, f'Missing precise-note control {identifier}')
        return result

    def note_page(self, page):
        state = self.note_idle()['preciseNotes']
        self.assertTrue(state['visible'], 'Explicitly reveal the retained owner before acting')
        expected = ('timeline', 'hit', 'tools')[page]
        if state['page'] != expected:
            tab = self.raw_note_control(9200 + page)
            self.note_idle(tab)
            self.desktop.send(tab, 0xF5)  # One native page button action.
        self.assertEqual(self.note_idle()['preciseNotes']['page'], expected)

    def control(self, identifier):
        if not self.note_identifier(identifier):
            return self.main_control(identifier)
        control = self.raw_note_control(identifier)
        if not user.IsWindowVisible(control):
            page = (2 if identifier in (373, 374, 375, 376, 377, 9204, 9205)
                    else 1 if identifier in (360, 361, 362, 367, 368) else 0)
            self.note_page(page)
        self.assertTrue(user.IsWindowVisible(control), identifier)
        return control

    def main_control(self, identifier):
        control = user.GetDlgItem(self.desktop.hwnd(self.pid), identifier)
        self.assertTrue(control, f'Missing Main control {identifier}')
        return control

    def main_command(self, identifier, notification=0):
        self.note_idle()
        owner = self.desktop.hwnd(self.pid)
        # Some public shell commands have no permanent Main button HWND.
        control = user.GetDlgItem(owner, identifier)
        self.desktop.send(owner, 0x111, identifier | (notification << 16), control or 0)
        return self.note_idle()

    def command(self, identifier, notification=0):
        if not self.note_identifier(identifier):
            return self.main_command(identifier, notification)
        control = self.control(identifier)
        self.note_idle(control)
        if notification:
            self.desktop.send(self.note_hwnd(), 0x111,
                              identifier | (notification << 16), control)
        else:
            self.desktop.send(control, 0xF5)  # Real visible/enabled native button.
        return self.note_idle()

    def combo(self, identifier, index):
        control = self.control(identifier)
        self.note_idle(control)
        self.assertGreaterEqual(index, 0)
        self.assertLess(index, self.desktop.send(control, 0x146))
        self.desktop.send(control, 0x14E, index)
        self.desktop.send(self.note_hwnd(), 0x111, identifier | (1 << 16), control)
        self.note_idle()

    def focus_control(self, control):
        self.assertTrue(user.IsWindowVisible(control))
        self.assertTrue(user.IsWindowEnabled(control))
        self.desktop.send(control, 0x201, 1, 4 | (4 << 16))
        self.desktop.send(control, 0x202, 0, 4 | (4 << 16))
        self.assertEqual(self.desktop.focus(control), control)

    def text(self, identifier, value):
        control = self.control(identifier)
        self.note_idle(control)
        self.focus_control(control)
        before = self.editor()['generation']
        buffer = ctypes.create_unicode_buffer(str(value))
        self.desktop.send(control, 0xB1, 0, -1)  # Select all, then actual EDIT change.
        self.desktop.send(control, 0xC2, 1, ctypes.addressof(buffer))
        self.assertEqual(self.field_text(control), str(value))
        self.assertGreater(self.editor()['generation'], before)
        return control

    def field_text(self, control):
        result = ctypes.create_unicode_buffer(self.desktop.send(control, 0xE) + 1)
        self.desktop.send(control, 0xD, len(result), ctypes.addressof(result))
        return result.value

    def mouse(self, message, x, y, buttons=0):
        self.note_page(0)
        owner = self.note_hwnd()
        self.assertEqual(self.editor()['coordinateSpace'], 'ownerClientDIP')
        scale = user.GetDpiForWindow(owner) / 96
        self.desktop.send(owner, message, buttons,
                          (int(x * scale) & 0xFFFF) | ((int(y * scale) & 0xFFFF) << 16))

    def focus_canvas(self):
        self.note_page(0)
        # Native Timeline tab focuses the canvas without changing its selection.
        self.desktop.send(self.raw_note_control(9200), 0xF5)
        self.assertEqual(self.desktop.focus(self.note_hwnd()), self.note_hwnd())

    def key(self, code):
        key = ord(code) if isinstance(code, str) else code
        windows = self.note_windows()
        if windows:
            owner = self.note_hwnd()
            target = self.desktop.focus(owner)
            self.assertTrue(target == owner or user.IsChild(owner, target), target)
            self.assertTrue(user.IsWindowVisible(target))
            self.assertTrue(user.IsWindowEnabled(target))
        else:
            target = self.desktop.hwnd(self.pid)  # Original grid-only Delete case.
        private_desktop.check(user.PostMessageW(target, 0x100, key, 1))
        private_desktop.check(user.PostMessageW(target, 0x101, key, 0xC0000001))
        return self.note_idle()

    def open_row(self, row=4, channel=0):
        self.navigate(row=row, channel=channel, column=0)
        self.main_command(107)
        self.assertTrue(self.editor()['visible'])
        self.assertTrue(user.IsWindowVisible(self.note_hwnd()))

    def native_panel(self, identifier='preciseNotes', **fields):
        self.note_idle()
        self.client.call('workspace.panel', dict(panel=identifier, **fields))
        return self.note_idle()

    def resize_main(self, width, height):
        owner = self.desktop.hwnd(self.pid)
        client, outer = w.RECT(), w.RECT()
        private_desktop.check(user.GetClientRect(owner, ctypes.byref(client)))
        private_desktop.check(user.GetWindowRect(owner, ctypes.byref(outer)))
        scale = user.GetDpiForWindow(owner) / 96
        private_desktop.check(user.SetWindowPos(owner, None, 0, 0,
            round(width * scale) + outer.right - outer.left - client.right,
            round(height * scale) + outer.bottom - outer.top - client.bottom, 0x16))
        private_desktop.check(user.GetClientRect(owner, ctypes.byref(client)))
        self.assertAlmostEqual(client.right / scale, width, delta=.5)
        self.assertAlmostEqual(client.bottom / scale, height, delta=.5)
        return self.note_idle()
