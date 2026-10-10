"""Retained native transform workbench on an owned desktop and actual PID pipe."""
import ctypes
from ctypes import wintypes
import time
import unittest

import private_desktop
import test_graph_mixer_app as support


class PatternToolsAppTests(unittest.TestCase):
    setUp = support.GraphMixerAppTests.setUp
    doc = support.GraphMixerAppTests.doc
    read = support.GraphMixerAppTests.read
    write = support.GraphMixerAppTests.write

    def state(self):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            state = self.read('workspace.get')
            if not state['documentBusy'] and not state['patternTools'].get('pending', False):
                return state['patternTools']
            time.sleep(.01)
        self.fail('Pattern workbench did not settle')

    def window(self):
        found = []

        @private_desktop.callback
        def visit(hwnd, _):
            pid = wintypes.DWORD()
            private_desktop.user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            name = ctypes.create_unicode_buffer(128)
            private_desktop.user.GetClassNameW(hwnd, name, len(name))
            if pid.value == self.pid and name.value == 'ScreamSeq.PatternTools':
                found.append(hwnd)
            return True

        private_desktop.check(private_desktop.user.EnumDesktopWindows(self.desktop.desktop, visit, 0))
        self.assertEqual(len(found), 1)
        return found[0]

    def press(self, identifier):
        window = self.window()
        self.desktop.send(window, 0x111, identifier, private_desktop.user.GetDlgItem(window, identifier))
        return self.state()

    def choose(self, identifier, index):
        window = self.window()
        control = private_desktop.user.GetDlgItem(window, identifier)
        self.desktop.send(control, 0x14E, index)
        self.desktop.send(window, 0x111, identifier | (1 << 16), control)

    def navigate(self, row, channel):
        context = self.client.call('context.get')
        self.client.call('context.set', dict(expectedRevision=context['revision'], expectedContext=context['data']['contextRevision'],
                                             row=row, channel=channel, following=False))

    def cells(self):
        return self.read('pattern.get', pattern=0, rowCount=64)['cells']

    def test_native_preview_matches_api_captured_scope_history_and_reopen(self):
        self.navigate(16, 0)
        main = self.desktop.hwnd(self.pid)
        self.desktop.send(main, 0x111, 591)
        self.assertTrue(self.state()['visible'])
        self.choose(8002, 1)  # Captured current column.
        self.choose(8005, 2)  # All rows, including blank values.
        before, original = self.doc(), self.cells()
        preview = self.press(8017)
        self.assertEqual(self.doc(), before)
        self.assertTrue(preview['applyEnabled'])
        self.assertEqual(preview['preview'], self.client.call('pattern.transform', preview['prepared'])['data'])
        self.navigate(32, 3)
        context = self.read('context.get')
        applied = self.press(8018)
        self.assertTrue(applied['completed'])
        self.assertIsNone(applied['completion'])
        self.assertEqual(self.read('context.get'), context)
        after = self.cells()
        self.assertEqual(next(c for c in after if c['row'] == 0 and c['channel'] == 0)['volume'], 4)
        self.assertEqual(next(c for c in after if c['row'] == 63 and c['channel'] == 0)['volume'], 64)
        self.assertEqual([c for c in after if c['channel'] != 0], [c for c in original if c['channel'] != 0])
        self.write('history.undo', domain='all')
        self.assertEqual(self.cells(), original)
        self.write('history.redo', domain='all')
        self.assertEqual(self.cells(), after)
        path = self.folder / 'pattern-tools.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.cells(), after)
        self.assertFalse(self.state()['visible'])

    def test_invalid_raw_draft_and_stale_preview_survive_reopening(self):
        main = self.desktop.hwnd(self.pid)
        self.desktop.send(main, 0x111, 591)
        self.choose(8005, 2)
        window = self.window()
        control = private_desktop.user.GetDlgItem(window, 8006)
        raw = ctypes.create_unicode_buffer('-')
        self.desktop.send(control, 0xC, 0, ctypes.addressof(raw))
        before = self.doc()
        state = self.press(8017)
        self.assertIsNone(state['preview'])
        self.assertEqual(state['draft']['from'], '-')
        self.assertEqual(self.doc(), before)
        self.press(8021)
        self.desktop.send(main, 0x111, 591)
        self.assertEqual(self.window(), window)
        self.assertEqual(self.state()['draft']['from'], '-')
        raw = ctypes.create_unicode_buffer('4')
        self.desktop.send(control, 0xC, 0, ctypes.addressof(raw))
        self.assertTrue(self.press(8017)['applyEnabled'])
        self.write('document.patch', title='Changed after Preview')
        before = self.doc()
        self.assertTrue(self.press(8018)['stale'])
        self.assertEqual(self.doc(), before)
        self.assertIsNone(self.state()['completion'])
