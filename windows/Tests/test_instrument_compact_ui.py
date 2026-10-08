"""Retained compact instrument pages through actual HWNDs on an isolated desktop."""
import ctypes
from ctypes import wintypes as w
import unittest

import private_desktop
import test_instrument_envelope_ui as support


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


class InstrumentCompactUITests(unittest.TestCase):
    # Reuse fixture methods without inheriting its tests or exporting a TestCase alias.
    setUp = support.InstrumentEnvelopeUITests.setUp
    doc = support.InstrumentEnvelopeUITests.doc
    read = support.InstrumentEnvelopeUITests.read
    write = support.InstrumentEnvelopeUITests.write
    state = support.InstrumentEnvelopeUITests.state
    window = support.InstrumentEnvelopeUITests.window
    control = support.InstrumentEnvelopeUITests.control
    idle = support.InstrumentEnvelopeUITests.idle
    start = support.InstrumentEnvelopeUITests.start
    saved = support.InstrumentEnvelopeUITests.saved
    settings = support.InstrumentEnvelopeUITests.settings

    def press(self, ident):
        self.idle()
        control = self.control(ident)
        self.assertTrue(user.IsWindowVisible(control), f'Hidden button {ident}')
        self.assertTrue(user.IsWindowEnabled(control), f'Disabled button {ident}')
        self.desktop.send(control, 0xF5)  # BM_CLICK: native button notification path.
        self.idle()

    def field(self, ident, value):
        self.assertTrue(user.IsWindowVisible(self.control(ident)), ident)
        self.assertTrue(user.IsWindowEnabled(self.control(ident)), ident)
        support.InstrumentEnvelopeUITests.field(self, ident, value)

    def select(self, ident, index):
        self.assertTrue(user.IsWindowVisible(self.control(ident)), ident)
        self.assertTrue(user.IsWindowEnabled(self.control(ident)), ident)
        support.InstrumentEnvelopeUITests.select(self, ident, index)

    def text(self, ident):
        text = ctypes.create_unicode_buffer(512)
        self.desktop.send(self.control(ident), 0xD, len(text), ctypes.addressof(text))
        return text.value

    def create(self):
        self.start()
        if self.state()['compactLayout']:
            self.press(4603)
        self.press(4403)
        self.assertTrue(self.state()['instrument'])

    def preset(self):
        self.create()
        if self.state()['compactLayout']:
            self.press(4600)
        self.press(4410)
        self.press(4450)
        self.assertEqual(len(self.saved()['points']), 5)

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

    def visible_rectangles(self):
        window = self.window()
        rectangles = []
        child = user.GetWindow(window, 5)  # Enumerate every direct child, not curated IDs.
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
        return rectangles

    def assert_visible_geometry(self):
        client = w.RECT()
        private_desktop.check(user.GetClientRect(self.window(), ctypes.byref(client)))
        rectangles = self.visible_rectangles()
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

    def test_every_compact_page_and_wide_view_has_distinct_visible_controls(self):
        self.create()
        handles = {ident: self.control(ident) for ident in range(4401, 4457)}
        before = self.doc()
        for width, height in ((440, 500), (650, 800)):
            self.resize_client(width, height)
            self.assertTrue(self.state()['compactLayout'])
            self.press(4602)
            self.select(4429, 5)  # Sine exposes all five parameter fields.
            seen = set()
            for page, name in enumerate(('envelope', 'points', 'tools', 'properties', 'keymap')):
                self.press(4600 + page)
                self.assertEqual(self.state()['page'], name)
                self.assertEqual(self.state()['canvasVisible'], page == 0)
                seen.update(self.assert_visible_geometry())
            self.assertTrue(set(handles).issubset(seen), set(handles) - seen)
        self.resize_client(1100, 820)
        self.assertFalse(self.state()['compactLayout'])
        self.assertTrue(set(handles).issubset(self.assert_visible_geometry()))
        self.assertEqual({ident: self.control(ident) for ident in handles}, handles)
        self.assertEqual(self.doc(), before)

    def test_pages_preserve_raw_fields_staged_music_selection_and_keymap_scroll(self):
        self.preset()
        original = self.settings()
        before = self.doc()
        self.resize_client(440, 500)
        handles = {ident: self.control(ident) for ident in (4414, 4439, 4447, 4456)}
        self.press(4601)
        self.select(4412, 0)
        self.field(4414, 37)
        self.press(4604)
        self.field(4446, 60)
        self.field(4447, 71)
        self.select(4448, 2)
        self.press(4603)
        self.field(4439, 'Retained compact draft')
        self.desktop.send(self.control(4439), 0xB1, 2, 8)  # EM_SETSEL.
        captured = self.state()
        for page in (4600, 4602, 4604, 4601, 4603):
            self.press(page)
            self.assertEqual(self.text(4414), '37')
            self.assertEqual(self.text(4447), '71')
            self.assertEqual(self.text(4439), 'Retained compact draft')
            for key in ('document', 'expectedRevision', 'instrument', 'envelope', 'mapping'):
                self.assertEqual(self.state()[key], captured[key], key)
        self.assertEqual(self.desktop.send(self.control(4439), 0xB0), 2 | (8 << 16))
        self.press(4601)
        self.press(4415)
        self.press(4604)
        self.press(4449)
        self.desktop.send(self.control(4456), 0x197, 50)  # LB_SETTOPINDEX.
        scroll = self.desktop.send(self.control(4456), 0x18E)
        staged = self.state()
        self.assertEqual(staged['envelope']['points'][0], [0, 37])
        self.assertEqual(staged['mapping'][60:72], [2] * 12)
        for page in (4600, 4602, 4603, 4601, 4604):
            self.press(page)
            self.assertEqual(self.state()['envelope'], staged['envelope'])
            self.assertEqual(self.state()['mapping'], staged['mapping'])
            self.assertEqual(self.desktop.send(self.control(4456), 0x18E), scroll)
        self.resize_client(1100, 820)
        self.resize_client(440, 500)
        self.assertEqual(self.state()['page'], 'keymap')
        self.assertEqual({ident: self.control(ident) for ident in handles}, handles)
        self.assertEqual(self.state()['envelope'], staged['envelope'])
        self.assertEqual(self.state()['mapping'], staged['mapping'])
        self.assertEqual(self.text(4439), 'Retained compact draft')
        self.assertEqual(self.doc(), before)
        self.press(4450)
        saved = self.settings()
        self.assertEqual(saved['name'], 'Retained compact draft')
        self.assertEqual(saved['mapping'][60:72], [2] * 12)
        self.assertEqual(saved['envelopes'][0]['points'][0], [0, 37])
        self.write('history.undo', domain='document')
        self.assertEqual(self.settings(), original)

    def test_hidden_canvas_does_not_receive_mouse_wheel_or_edit_keys(self):
        self.preset()
        self.resize_client(440, 500)
        self.press(4600)
        scene = self.state()
        x, y, width, height = scene['canvas']
        self.press(4603)
        self.assertFalse(self.state()['canvasVisible'])
        scale = user.GetDpiForWindow(self.window()) / 96
        point = w.POINT(round((x + width * .4) * scale), round((y + height * .4) * scale))
        position = (point.x & 65535) | ((point.y & 65535) << 16)
        for message in (0x201, 0x200, 0x202, 0x203):
            self.desktop.send(self.window(), message, 0, position)
        private_desktop.check(user.ClientToScreen(self.window(), ctypes.byref(point)))
        self.desktop.send(self.window(), 0x20A, 120 << 16,
                          (point.x & 65535) | ((point.y & 65535) << 16))
        self.desktop.send(self.control(4603), 0x100, 0x75)  # F6 focuses the editor.
        self.assertEqual(self.desktop.focus(self.window()), self.window())
        for key in (0x2D, 0x2E, 0x25, 0x26, 0x6B):
            self.desktop.send(self.window(), 0x100, key)
        self.idle()
        for key in ('envelope', 'mapping', 'selectedPoint', 'start', 'end', 'dirty'):
            self.assertEqual(self.state()[key], scene[key], key)

    def test_raw_tool_fields_survive_guarded_follow_close_and_reopen(self):
        self.preset()
        self.resize_client(440, 500)
        cursor_instrument = self.state()['instrument']
        self.select(4401, 1)  # Deliberately inspect another retained instrument.
        self.assertNotEqual(self.state()['instrument'], cursor_instrument)
        self.press(4602)
        self.select(4429, 5)
        self.field(4430, 13)
        self.field(4431, 77)
        self.field(4432, '2.75')
        captured = self.state()
        before = self.doc()
        self.assertTrue(captured['toolFieldDraft'])
        self.assertTrue(captured['retainedDraft'])
        self.client.call('workspace.panel', dict(panel='instruments', follow=True))
        self.idle()
        self.assertFalse(self.read('workspace.get')['pins']['instruments'])
        self.assertEqual(self.state()['instrument'], captured['instrument'])
        self.assertEqual(self.state()['expectedRevision'], captured['expectedRevision'])
        self.press(4453)
        self.start()
        self.assertEqual(self.state()['page'], 'tools')
        self.assertEqual(self.state()['instrument'], captured['instrument'])
        self.assertEqual((self.text(4430), self.text(4431), self.text(4432)),
                         ('13', '77', '2.75'))
        self.assertTrue(self.state()['toolFieldDraft'])
        self.assertEqual(self.doc(), before)
        self.press(4452)  # Explicit From cursor releases the raw-tool guard.
        self.assertEqual(self.state()['instrument'], cursor_instrument)
        self.assertFalse(self.state()['toolFieldDraft'])

    def test_page_navigation_f6_and_tab_keep_focus_on_visible_native_controls(self):
        self.preset()
        self.resize_client(440, 500)
        for page, entry in ((4600, 4402), (4601, 4412), (4602, 4429),
                            (4603, 4439), (4604, 4456)):
            self.press(page)
            if self.desktop.focus(self.window()) != self.window():
                self.desktop.send(self.desktop.focus(self.window()), 0x100, 0x75)
            self.assertEqual(self.desktop.focus(self.window()), self.window())
            self.desktop.send(self.window(), 0x100, 0x75)
            self.assertEqual(self.desktop.focus(self.window()), self.control(entry))
            for _ in range(18):
                focused = self.desktop.focus(self.window())
                self.assertTrue(user.IsChild(self.window(), focused))
                self.assertTrue(user.IsWindowVisible(focused))
                self.assertTrue(user.IsWindowEnabled(focused))
                self.desktop.send(focused, 0x100, 0x09)
        self.press(4600)
        if self.desktop.focus(self.window()) != self.window():
            self.desktop.send(self.desktop.focus(self.window()), 0x100, 0x75)
        self.desktop.send(self.window(), 0x100, 0x2D)  # Insert opens point fields.
        self.assertEqual(self.state()['page'], 'points')
        self.assertEqual(self.desktop.focus(self.window()), self.control(4413))
        self.assertTrue(user.IsWindowVisible(self.control(4413)))

    def test_keyboard_reload_preserves_focus_after_pending_operation(self):
        self.preset()
        self.resize_client(440, 500)
        self.press(4600)
        if self.desktop.focus(self.window()) != self.window():
            self.desktop.send(self.desktop.focus(self.window()), 0x100, 0x75)
        self.desktop.send(self.window(), 0x100, 0x75)  # F6 reaches the visible selector.
        reload = self.control(4451)
        for _ in range(40):
            focused = self.desktop.focus(self.window())
            if focused == reload:
                break
            self.assertTrue(user.IsWindowVisible(focused))
            self.desktop.send(focused, 0x100, 0x09)
        self.assertEqual(self.desktop.focus(self.window()), reload)
        before = self.doc()
        captured = self.state()['instrument']
        self.desktop.send(reload, 0x100, 0x0D)  # Native Enter invokes async Reload.
        self.idle()
        self.assertEqual(self.desktop.focus(self.window()), reload)
        self.assertTrue(user.IsWindowVisible(reload))
        self.assertTrue(user.IsWindowEnabled(reload))
        self.assertEqual(self.state()['instrument'], captured)
        self.assertEqual(self.doc(), before)


if __name__ == '__main__':
    unittest.main()
