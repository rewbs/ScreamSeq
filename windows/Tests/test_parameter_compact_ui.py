"""Retained compact parameter pages through actual HWNDs on an isolated desktop."""
import ctypes
from ctypes import wintypes as w
import os
import time
import unittest

import private_desktop
import test_parameter_automation_ui as support
from client import Client, TransportError


user = private_desktop.user
user.GetWindow.argtypes = [w.HWND, w.UINT]
user.GetWindow.restype = w.HWND
user.GetDlgCtrlID.argtypes = [w.HWND]
user.GetWindowRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
user.GetClientRect.argtypes = user.GetWindowRect.argtypes
user.MapWindowPoints.argtypes = [w.HWND, w.HWND, ctypes.POINTER(w.POINT), w.UINT]
user.ClientToScreen.argtypes = [w.HWND, ctypes.POINTER(w.POINT)]
user.SetWindowPos.argtypes = [w.HWND, w.HWND, ctypes.c_int, ctypes.c_int,
                             ctypes.c_int, ctypes.c_int, w.UINT]
user.GetDpiForWindow.argtypes = [w.HWND]
user.IsWindowVisible.argtypes = [w.HWND]
user.IsWindowEnabled.argtypes = [w.HWND]
user.IsChild.argtypes = [w.HWND, w.HWND]


class ParameterCompactUITests(unittest.TestCase):
    # Reuse the owned-process fixture without inheriting its unrelated tests.
    setUp = support.ParameterAutomationUITests.setUp
    doc = support.ParameterAutomationUITests.doc
    read = support.ParameterAutomationUITests.read
    write = support.ParameterAutomationUITests.write
    add_gain = support.ParameterAutomationUITests.add_gain
    state = support.ParameterAutomationUITests.state
    window = support.ParameterAutomationUITests.window
    control = support.ParameterAutomationUITests.control
    idle = support.ParameterAutomationUITests.idle
    start = support.ParameterAutomationUITests.start
    saved = support.ParameterAutomationUITests.saved
    mouse = support.ParameterAutomationUITests.mouse

    def modified_key(self, hwnd, key, ctrl=False, alt=False):
        # Attach only the two threads on this owned, never-switched desktop.
        user.AttachThreadInput.argtypes = [w.DWORD, w.DWORD, w.BOOL]
        user.GetKeyboardState.argtypes = [ctypes.POINTER(ctypes.c_ubyte)]
        user.SetKeyboardState.argtypes = [ctypes.POINTER(ctypes.c_ubyte)]
        thread = private_desktop.kernel.GetCurrentThreadId()
        target = user.GetWindowThreadProcessId(hwnd, None)
        private_desktop.check(user.AttachThreadInput(thread, target, True))
        original = (ctypes.c_ubyte * 256)()
        try:
            private_desktop.check(user.GetKeyboardState(original))
            keys = (ctypes.c_ubyte * 256).from_buffer_copy(original)
            if ctrl:
                keys[0x11] = keys[0xA2] = 0x80
            if alt:
                keys[0x12] = keys[0xA4] = 0x80
            private_desktop.check(user.SetKeyboardState(keys))
            self.desktop.send(hwnd, 0x100, key)
        finally:
            user.SetKeyboardState(original)
            private_desktop.check(user.AttachThreadInput(thread, target, False))

    def press(self, ident, kind='ScreamSeq.ParameterAutomation'):
        self.idle()
        control = self.control(ident, kind)
        self.assertTrue(user.IsWindowVisible(control), f'Hidden button {ident}')
        self.assertTrue(user.IsWindowEnabled(control), f'Disabled button {ident}')
        self.desktop.send(control, 0xF5)  # BM_CLICK: native notification path.
        self.idle()

    def field(self, ident, value, kind='ScreamSeq.ParameterAutomation'):
        control = self.control(ident, kind)
        self.assertTrue(user.IsWindowVisible(control), ident)
        self.assertTrue(user.IsWindowEnabled(control), ident)
        support.ParameterAutomationUITests.field(self, ident, value, kind)

    def select(self, ident, index):
        self.assertTrue(user.IsWindowVisible(self.control(ident)), ident)
        self.assertTrue(user.IsWindowEnabled(self.control(ident)), ident)
        support.ParameterAutomationUITests.select(self, ident, index)

    def text(self, ident, kind='ScreamSeq.ParameterAutomation'):
        text = ctypes.create_unicode_buffer(2049)
        self.desktop.send(self.control(ident, kind), 0xD, len(text), ctypes.addressof(text))
        return text.value

    def setup_curve(self):
        plugin = self.add_gain()
        self.start()
        self.resize_client(1100, 800)
        self.select(4204, 1)
        self.press(4212)
        self.assertEqual(self.state()['parameter'], 1)
        return plugin

    def resize_client(self, width, height):
        window = self.window()
        scale = user.GetDpiForWindow(window) / 96
        client, outer = w.RECT(), w.RECT()
        private_desktop.check(user.GetClientRect(window, ctypes.byref(client)))
        private_desktop.check(user.GetWindowRect(window, ctypes.byref(outer)))
        private_desktop.check(user.SetWindowPos(
            window, None, 0, 0,
            round(width * scale) + outer.right - outer.left - client.right,
            round(height * scale) + outer.bottom - outer.top - client.bottom,
            0x16))  # Preserve position, Z order and activation.
        self.idle()
        private_desktop.check(user.GetClientRect(window, ctypes.byref(client)))
        self.assertAlmostEqual(client.right / scale, width, delta=1)
        self.assertAlmostEqual(client.bottom / scale, height, delta=1)

    def assert_visible_geometry(self):
        window = self.window()
        client = w.RECT()
        private_desktop.check(user.GetClientRect(window, ctypes.byref(client)))
        rectangles = []
        child = user.GetWindow(window, 5)  # Every direct child, not curated IDs.
        while child:
            if user.IsWindowVisible(child):
                rect = w.RECT()
                private_desktop.check(user.GetWindowRect(child, ctypes.byref(rect)))
                points = (w.POINT * 2)(w.POINT(rect.left, rect.top),
                                       w.POINT(rect.right, rect.bottom))
                user.MapWindowPoints(None, window, points, 2)
                rectangles.append((user.GetDlgCtrlID(child), points[0].x, points[0].y,
                                   points[1].x, points[1].y))
            child = user.GetWindow(child, 2)
        self.assertTrue(rectangles)
        for ident, left, top, right, bottom in rectangles:
            self.assertGreater(right, left, ident)
            self.assertGreater(bottom, top, ident)
            self.assertGreaterEqual(left, 0, ident)
            self.assertGreaterEqual(top, 0, ident)
            self.assertLessEqual(right, client.right, ident)
            self.assertLessEqual(bottom, client.bottom, ident)
        for index, first in enumerate(rectangles):
            for second in rectangles[index + 1:]:
                self.assertFalse(max(first[1], second[1]) < min(first[3], second[3])
                                 and max(first[2], second[2]) < min(first[4], second[4]),
                                 (first, second))
        return {rect[0] for rect in rectangles}

    def test_compact_pages_and_wide_view_expose_every_control_without_overlap(self):
        self.setup_curve()
        self.select(4205, 8)  # Scripted reveals the full formula workflow.
        self.press(4210)
        self.select(4230, 5)  # Sine reveals all four tool fields.
        handles = {ident: self.control(ident) for ident in range(4201, 4241)}
        before = self.doc()
        for width, height in ((440, 500), (650, 800)):
            self.resize_client(width, height)
            self.assertTrue(self.state()['compact'])
            seen = set()
            for page, name in enumerate(('target', 'curve', 'formula', 'tools')):
                self.press(4241 + page)
                self.assertEqual(self.state()['page'], name)
                self.assertEqual(self.state()['canvasVisible'], page == 1)
                seen.update(self.assert_visible_geometry())
            self.assertTrue(set(handles).issubset(seen), set(handles) - seen)
        self.resize_client(1100, 800)
        self.assertFalse(self.state()['compact'])
        self.assertTrue(set(handles).issubset(self.assert_visible_geometry()))
        self.assertEqual({ident: self.control(ident) for ident in handles}, handles)
        self.assertEqual(self.doc(), before)

    def test_pages_preserve_raw_fields_staged_points_and_native_selection(self):
        self.setup_curve()
        before = self.doc()
        self.resize_client(440, 500)
        self.press(4242)
        self.field(4208, '37.25')
        self.desktop.send(self.control(4208), 0xB1, 1, 4)  # EM_SETSEL.
        raw = self.state()
        for page in (4241, 4244, 4243, 4242):
            self.press(page)
            self.assertEqual(self.text(4208), '37.25')
            for key in ('document', 'expectedRevision', 'patternID', 'plugin',
                        'parameter', 'points', 'selectedPoint', 'fieldDraft'):
                self.assertEqual(self.state()[key], raw[key], key)
        self.assertEqual(self.desktop.send(self.control(4208), 0xB0), 1 | (4 << 16))
        self.press(4210)
        self.assertEqual(self.state()['points'][0]['value'], .3725)
        self.press(4244)
        self.select(4230, 5)
        self.field(4233, 'unfinished center')
        self.field(4231, '0.25')
        staged = self.state()
        self.assertTrue(staged['toolFieldDraft'])
        self.assertTrue(staged['retainedDraft'])
        for page in (4242, 4243, 4241, 4244):
            self.press(page)
            self.assertEqual(self.state()['points'], staged['points'])
            self.assertEqual(self.text(4233), 'unfinished center')
            self.assertEqual(self.text(4231), '0.25')
        self.resize_client(1100, 800)
        self.resize_client(440, 500)
        self.assertEqual(self.state()['page'], 'tools')
        self.assertEqual(self.text(4233), 'unfinished center')
        self.assertEqual(self.doc(), before)
        self.press(4215)
        self.assertEqual(self.saved()[0]['points'], staged['points'])
        self.assertTrue(self.state()['toolFieldDraft'])
        self.write('history.undo', domain='document')
        self.assertEqual(self.saved(), [])

    def test_hidden_canvas_ignores_pointer_wheel_and_canvas_edit_keys(self):
        self.setup_curve()
        self.resize_client(440, 500)
        self.press(4242)
        scene = self.state()
        x, y, width, height = scene['canvas']
        self.press(4244)
        self.assertFalse(self.state()['canvasVisible'])
        for message in (0x201, 0x200, 0x202, 0x203):
            self.mouse(message, x + width * .4, y + height * .4)
        scale = user.GetDpiForWindow(self.window()) / 96
        point = w.POINT(round((x + width * .4) * scale), round((y + height * .4) * scale))
        private_desktop.check(user.ClientToScreen(self.window(), ctypes.byref(point)))
        self.desktop.send(self.window(), 0x20A, (120 << 16) | 8,
                          (point.x & 65535) | ((point.y & 65535) << 16))
        for key in (0x2E, 0x25, 0x26, 0x6B):
            self.desktop.send(self.window(), 0x100, key)
        self.idle()
        for key in ('points', 'selectedPoint', 'start', 'end', 'valueLow', 'valueHigh', 'dirty'):
            self.assertEqual(self.state()[key], scene[key], key)

    def test_raw_tool_fields_block_follow_and_survive_close_reopen(self):
        self.setup_curve()
        self.press(4215)
        other = self.write('pattern.create', rows=32)['pattern']
        self.press(4218)
        cursor_pattern = self.read('context.get')['pattern']
        self.resize_client(440, 500)
        self.press(4241)
        self.select(4201, 1)
        self.assertEqual(self.state()['pattern'], other)
        self.assertNotEqual(other, cursor_pattern)
        self.press(4244)
        self.field(4231, '0.25')
        self.select(4230, 5)
        self.assertTrue(self.state()['toolFieldDraft'])
        self.field(4233, 'unfinished center')
        captured = self.state()
        before = self.doc()
        self.assertFalse(captured['dirty'])
        self.assertFalse(captured['fieldDraft'])
        self.assertTrue(captured['toolFieldDraft'])
        self.client.call('workspace.panel', dict(panel='automation', follow=True))
        self.idle()
        self.assertEqual(self.state()['patternID'], captured['patternID'])
        self.assertEqual(self.state()['expectedRevision'], captured['expectedRevision'])
        self.press(4239)
        self.start()
        self.assertEqual(self.state()['page'], 'tools')
        self.assertEqual(self.state()['patternID'], captured['patternID'])
        self.assertEqual((self.text(4231), self.text(4233)), ('0.25', 'unfinished center'))
        self.assertTrue(self.state()['retainedDraft'])
        self.assertEqual(self.doc(), before)
        self.press(4219)  # Explicit From cursor releases the retained tool fields.
        self.assertEqual(self.state()['pattern'], cursor_pattern)
        self.assertFalse(self.state()['toolFieldDraft'])

    def test_page_shortcuts_f6_enter_and_tab_keep_focus_local(self):
        self.setup_curve()
        self.resize_client(440, 500)
        self.press(4242)
        self.assertEqual(self.desktop.focus(self.window()), self.window())
        self.desktop.send(self.window(), 0x100, 0x75)  # F6 enters the point fields.
        self.assertEqual(self.desktop.focus(self.window()), self.control(4207))
        self.field(4207, 1)
        before = self.doc()
        self.desktop.send(self.control(4207), 0x100, 0x0D)
        self.desktop.send(self.control(4207), 0x102, 0x0D)
        self.idle()
        self.assertEqual(self.state()['points'][0]['position'], 256)
        self.assertEqual(float(self.text(4207)), 1)
        self.assertNotIn('\r', self.text(4207))
        self.assertNotIn('\n', self.text(4207))
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.desktop.focus(self.window()), self.control(4207))
        self.field(4207, '1.')
        row_field = self.control(4207)
        live_keys = self.read('workspace.get')['liveKeyboard']
        for expected in (not live_keys, live_keys):
            self.modified_key(row_field, ord('L'), ctrl=True, alt=True)
            self.desktop.send(row_field, 0x102, 12)  # Queued Ctrl+L character.
            self.idle()
            self.assertEqual(self.read('workspace.get')['liveKeyboard'], expected)
            self.assertEqual(self.desktop.focus(self.window()), row_field)
            self.assertEqual(self.text(4207), '1.')
            self.assertTrue(self.state()['fieldDraft'])
            self.assertEqual(self.doc(), before)
        self.desktop.send(row_field, 0x100, 0x1B)  # Discard only the raw point field.
        self.assertFalse(self.state()['fieldDraft'])
        for name in ('formula', 'tools', 'target', 'curve'):
            self.modified_key(self.desktop.focus(self.window()), 0x22, ctrl=True)
            self.assertEqual(self.state()['page'], name)
            focused = self.desktop.focus(self.window())
            self.assertTrue(focused == self.window() or user.IsChild(self.window(), focused))
            self.assertTrue(user.IsWindowVisible(focused))
            self.assertTrue(user.IsWindowEnabled(focused))
        self.press(4244)
        for _ in range(20):
            focused = self.desktop.focus(self.window())
            self.assertTrue(user.IsChild(self.window(), focused))
            self.assertTrue(user.IsWindowVisible(focused))
            self.assertTrue(user.IsWindowEnabled(focused))
            self.desktop.send(focused, 0x100, 0x09)
        if os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO') == '1':
            # A separate owned process renders into explicitly silent output.
            # Inspection mode cannot start hardware playback; never relax it.
            path = self.folder / 'compact-keyboard-focus.screamseq'
            self.write('document.save', path=str(path))
            self.pid = self.desktop.launch([
                os.environ['SCREAMSEQ_TEST_EXE'], '--audio-test-silent',
                '--audio-test-allow-stop', '--automation', '--seconds', '120',
                '--project', str(path), '--vst3-test-cache', str(self.cache)])
            self.client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(self.pid), timeout=20)
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                try:
                    transport = self.read('transport.get')
                    if (transport['playing'] and transport['audioActive']
                            and not self.read('workspace.get')['documentBusy']):
                        break
                except TransportError:
                    pass
                process = self.desktop.processes[-1]
                exit_code = w.DWORD()
                private_desktop.check(private_desktop.kernel.GetExitCodeProcess(
                    process.hProcess, ctypes.byref(exit_code)))
                self.assertEqual(exit_code.value, 259,
                                 f'Silent keyboard app exited: {exit_code.value:#x}')
                time.sleep(.1)
            else:
                self.fail('Silent compact keyboard app did not start')
            self.write('transport.stop')
            self.start()
            self.resize_client(440, 500)
            self.press(4242)
            window = self.window()
            self.assertEqual(self.desktop.focus(window), window)
            self.assertFalse(self.read('transport.get')['playing'])
            song = self.doc()
            for playing in (True, False):
                self.desktop.send(window, 0x100, 0x20)
                self.desktop.send(window, 0x101, 0x20)
                self.idle()
                self.assertEqual(self.read('transport.get')['playing'], playing)
                self.assertEqual(self.desktop.focus(window), window)
                self.assertEqual(self.doc(), song)

    def test_tools_bank_and_formula_workbench_remain_reachable_and_retain_drafts(self):
        self.setup_curve()
        self.press(4215)
        saved = self.saved()
        self.resize_client(440, 500)
        self.press(4244)
        self.select(4230, 1)
        self.press(4238)
        self.assertEqual(self.state()['page'], 'curve')
        self.assertEqual(self.state()['points'][0]['value'], 1)
        self.assertEqual(self.saved(), saved)
        self.press(4215)
        self.press(4244)
        self.field(4231, 0)
        self.field(4232, 2)
        self.press(4237)
        self.select(4230, 7)
        self.field(4231, 4)
        self.field(4232, 8)
        self.press(4238)
        self.assertTrue(any(p['position'] == 1024 for p in self.state()['points']))
        self.press(4218)
        self.press(4242)
        self.desktop.send(self.window(), 0x100, 0x09)  # Select the first saved point.
        self.press(4243)
        self.select(4205, 8)
        self.field(4209, 'mix(start,end,t)')
        self.press(4210)
        self.press(4221)
        workbench = 'ScreamSeq.FormulaWorkbench'
        handle = self.window(workbench)
        self.field(2001, 'start + (end-start)*t*t', workbench)
        self.desktop.send(handle, 0x10)  # Close retains the workbench draft.
        for page in (4241, 4242, 4244, 4243):
            self.press(page)
        self.assertTrue(self.state()['formulaWorkbench']['dirty'])
        self.assertTrue(self.state()['formulaWorkbench']['sourceCurrent'])
        self.press(4221)
        self.assertEqual(self.window(workbench), handle)
        self.press(2006, workbench)
        self.press(2007, workbench)
        self.assertEqual(self.state()['points'][0]['formula'], 'start + (end-start)*t*t')
        self.press(4215)
        self.press(4220)
        bank = 'ScreamSeq.EnvelopeBank'
        self.field(1004, 'Compact parameter rise', bank)
        self.press(1005, bank)
        bank_handle = self.window(bank)
        self.field(1003, 'Retained bank name', bank)
        self.assertTrue(self.state()['envelopeBank']['dirty'])
        self.desktop.send(bank_handle, 0x10)
        for page in (4244, 4241, 4242):
            self.press(page)
        self.assertTrue(self.state()['retainedDraft'])
        self.press(4220)
        self.assertEqual(self.window(bank), bank_handle)
        self.assertEqual(self.text(1003, bank), 'Retained bank name')
        self.assertTrue(self.state()['envelopeBank']['sourceCurrent'])


if __name__ == '__main__':
    unittest.main()
