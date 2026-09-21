"""Modeless envelope reuse through native HWNDs and the owned app's PID pipe."""
import ctypes
from ctypes import wintypes
import time
import unittest
import private_desktop
import test_graph_curves as support


class EnvelopeBankUITests(unittest.TestCase):
    setUp = support.GraphCurveTests.setUp
    doc = support.GraphCurveTests.doc
    read = support.GraphCurveTests.read
    write = support.GraphCurveTests.write
    control = support.GraphCurveTests.control
    command = support.GraphCurveTests.command
    field = support.GraphCurveTests.field
    select = support.GraphCurveTests.select
    point = support.GraphCurveTests.point
    mouse = support.GraphCurveTests.mouse
    key = support.GraphCurveTests.key
    state = support.GraphCurveTests.state
    settle = support.GraphCurveTests.settle
    setup_curve = support.GraphCurveTests.setup_curve
    saved = support.GraphCurveTests.saved

    def bank(self):
        return self.read('workspace.get')['envelopeBank']

    def bank_hwnd(self):
        found = []
        @private_desktop.callback
        def visit(hwnd, _):
            owner = wintypes.DWORD()
            private_desktop.user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            name = ctypes.create_unicode_buffer(128)
            private_desktop.user.GetClassNameW(hwnd, name, 128)
            if owner.value == self.pid and name.value == 'ScreamSeq.EnvelopeBank':
                found.append(hwnd)
            return True
        private_desktop.check(private_desktop.user.EnumDesktopWindows(self.desktop.desktop, visit, 0))
        self.assertEqual(len(found), 1, found)
        return found[0]

    def bcontrol(self, identifier):
        control = private_desktop.user.GetDlgItem(self.bank_hwnd(), identifier)
        self.assertTrue(control)
        return control

    def bcommand(self, identifier, notification=0):
        self.idle()
        self.desktop.send(self.bank_hwnd(), 0x111, identifier | (notification << 16), self.bcontrol(identifier))
        self.idle()

    def idle(self):
        # Timed preview and main-window refreshes can pump messages during a
        # synchronous SendMessage. Observe a quiet interval; never retry writes.
        deadline = time.monotonic()+5
        quiet = None
        while time.monotonic()<deadline:
            state = self.read('workspace.get')
            if state['documentBusy'] or state['envelopeBank'].get('pending'):
                quiet = None
            elif quiet is None:
                quiet = time.monotonic()
            elif time.monotonic()-quiet >= .18:
                return
            time.sleep(.02)
        self.fail(str(self.bank()))

    def bfield(self, identifier, value):
        text = ctypes.create_unicode_buffer(str(value))
        self.desktop.send(self.bcontrol(identifier), 0xC, 0, ctypes.addressof(text))

    def bselect(self, identifier, index):
        self.desktop.send(self.bcontrol(identifier), 0x14E, index)
        self.bcommand(identifier, 1)

    def bmouse(self, message, point, buttons=0):
        self.desktop.send(self.bank_hwnd(), message, buttons, self.point(point))

    def select_first_point(self):
        self.idle()
        handle = self.bank()['handles'][0]
        self.bmouse(0x201, handle, 1)
        self.bmouse(0x202, handle)
        self.assertEqual(self.bank()['selectedPoint'], 0)

    def capture(self):
        target = self.setup_curve()
        self.command(490)
        self.command(497)
        self.assertTrue(self.bank()['visible'])
        self.bfield(1004, 'Native ramp')
        self.bcommand(1005)
        self.assertTrue(self.bank()['selected'], self.bank())
        return target

    def test_capture_link_master_history_and_native_reopen(self):
        graph, node = self.capture()
        template = self.bank()['selected']
        self.assertTrue(self.state()['dirty'])  # Capturing doesn't implicitly Apply.
        self.assertEqual(self.saved(graph, node)['points'], [])
        self.bcommand(1008)  # Use linked.
        self.assertFalse(self.bank()['visible'])
        first = self.saved(graph, node)['points']
        self.assertFalse(self.state()['dirty'])
        self.command(497)
        self.assertEqual(self.bank()['linkedTemplate'], template)
        self.select_first_point()
        self.bfield(1017, '25')
        self.bcommand(1020)
        self.bcommand(1006)
        changed = self.saved(graph, node)['points']
        self.assertEqual(changed[0]['value'], .25, self.bank())
        self.assertEqual(self.state()['points'], changed)
        self.assertFalse(self.bank()['dirty'])
        self.write('history.undo', domain='document')
        self.assertEqual(self.saved(graph, node)['points'], first)
        self.write('history.redo', domain='document')
        self.assertEqual(self.saved(graph, node)['points'], changed)
        path = self.folder / 'native-bank.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path), discard=True)
        target = dict(kind='graph', graph=graph, node=node, pattern=0)
        self.assertEqual(self.read('envelope.bank.list', target=target)['linkedTemplate'], template)
        self.assertEqual(self.saved(graph, node)['points'], changed)

    def test_catalogue_explicit_copies_replace_import_and_revision_guard(self):
        graph, node = self.capture()
        template = self.bank()['selected']
        before = self.doc()
        self.bcommand(1010)  # Publish independent copy, outside document Undo.
        self.assertEqual(self.doc(), before)
        cat = self.read('envelope.catalogue.list')
        self.assertEqual(len(cat['entries']), 1, self.bank())
        self.bfield(1003, 'Revised song master')
        self.bcommand(1006)
        self.assertEqual(self.read('envelope.catalogue.list'), cat)
        self.bselect(1015, 0)
        self.bcommand(1011)  # Explicit selected-copy replacement.
        updated = self.read('envelope.catalogue.list')
        self.assertEqual(len(updated['entries']), 1)
        self.assertEqual(updated['entries'][0]['id'], cat['entries'][0]['id'])
        self.assertEqual(updated['entries'][0]['name'], 'Revised song master')
        self.bselect(1001, 1)
        self.bcommand(1013)  # Import allocates independent song identity.
        imported = self.bank()['selected']
        self.assertNotEqual(imported, template)
        self.assertEqual(self.bank()['scope'], 'song')
        self.bcommand(1007)
        self.assertEqual(self.saved(graph, node)['points'], self.state()['points'])
        self.command(497)
        self.assertEqual(self.bank()['linkedTemplate'], '')
        stale = self.bank()['catalogueRevision']
        self.write('envelope.catalogue.publish', template=template, expectedCatalogueRevision=stale)
        external = self.read('envelope.catalogue.list')
        before = self.doc()
        self.bcommand(1010)
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.read('envelope.catalogue.list'), external)
        self.assertIn('catalogue', self.bank()['status'].lower())

    def test_source_changes_and_reopening_retain_both_drafts(self):
        graph, node = self.capture()
        hwnd = self.bank_hwnd()
        self.select_first_point()
        self.bfield(1017, '30')
        self.bcommand(1020)
        shape = self.bank()['shape']
        self.field(484, '65')
        self.command(488)
        parent = self.state()['points']
        self.command(497)
        self.assertEqual(self.bank_hwnd(), hwnd)
        self.assertEqual(self.bank()['shape'], shape)
        self.assertFalse(self.bank()['sourceCurrent'])
        self.bcommand(1006)  # Song master can still save; source draft cannot be replaced.
        self.assertEqual(self.state()['points'], parent)
        self.assertFalse(self.bank()['dirty'])
        before = self.doc()
        self.bcommand(1008)
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.saved(graph, node)['points'], [])
        self.assertEqual(self.state()['points'], parent)
        self.assertIn('Source editor changed', self.bank()['status'])
        self.select_first_point()
        self.bfield(1017, '90')
        self.bcommand(1020)
        retained = self.bank()['shape']
        self.write('document.patch', title='Other editor changed the song')
        before = self.doc()
        self.bcommand(1006)
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.bank()['shape'], retained)
        self.assertTrue(self.bank()['dirty'])
        self.desktop.send(hwnd, 0x10)  # Close retains the unsaved modeless draft.
        self.command(497)
        self.assertEqual(self.bank_hwnd(), hwnd)
        self.assertEqual(self.bank()['shape'], retained)
        self.bcommand(1014)  # Explicit Reload/discard replaces only bank draft.
        self.assertFalse(self.bank()['dirty'])
        self.assertEqual(self.state()['points'], parent)

    def test_unlink_retains_unsaved_source_and_closed_document_draft(self):
        graph, node = self.capture()
        self.bcommand(1008)
        original = self.saved(graph, node)['points']
        self.field(483, '0')
        self.field(484, '60')
        # Reload leaves no point selected; select the existing first handle.
        self.key(0x1B)
        handle = self.state()['handles'][0]
        self.mouse(0x201, handle, 1)
        self.mouse(0x202, handle)
        self.field(484, '60')
        self.command(488)
        retained = self.state()['points']
        self.assertTrue(self.state()['dirty'])
        self.command(497)
        self.bcommand(1009)
        self.assertEqual(self.state()['points'], retained)
        self.assertTrue(self.state()['dirty'])
        self.assertEqual(self.saved(graph, node)['points'], original)
        self.command(486)
        self.assertEqual(self.saved(graph, node)['points'], retained)
        self.command(497)
        self.select_first_point()
        self.bfield(1017, '45')
        self.bcommand(1020)
        bank_draft = self.bank()['shape']
        path = self.folder / 'new-document.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path), discard=True)
        before = self.doc()
        self.bcommand(1006)
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.bank()['shape'], bank_draft)
        self.assertTrue(self.bank()['dirty'])
        self.bcommand(1014)  # Explicitly discard the closed document's bank draft.
        self.assertFalse(self.bank()['visible'])
        self.command(441)
        self.command(487)
        self.command(497)
        self.assertTrue(self.bank()['visible'])
        self.assertFalse(self.bank()['dirty'])

    def test_canvas_keyboard_formula_validation_timing_and_focus(self):
        self.capture()
        self.select_first_point()
        original = self.bank()['shape']
        handle = self.bank()['handles'][0]
        moved = dict(x=handle['x']+30, y=handle['y']-20)
        self.bmouse(0x201, handle, 1)
        self.bmouse(0x200, moved, 1)
        self.desktop.send(self.bank_hwnd(), 0x100, 0x1B)
        self.assertEqual(self.bank()['shape'], original)
        self.desktop.send(self.bank_hwnd(), 0x100, 0x26)
        self.assertAlmostEqual(self.bank()['shape']['points'][0]['value'], .01)
        self.bselect(1018, 8)
        self.bfield(1019, 'mix(start,end,t*t)')
        self.bcommand(1020)
        field = self.bcontrol(1019)
        self.desktop.send(field, 0x201, 1, 5 | (5 << 16))
        self.desktop.send(field, 0x202, 0, 5 | (5 << 16))
        self.bcommand(1024)
        self.assertEqual(self.bank()['previewSamples'], 1024, self.bank())
        self.assertEqual(self.desktop.focus(self.bank_hwnd()), field)
        self.desktop.send(field, 0x100, 0x75)  # F6 moves from fields to the canvas.
        self.assertEqual(self.desktop.focus(self.bank_hwnd()), self.bank_hwnd())
        self.desktop.send(self.bank_hwnd(), 0x100, 0x75)
        self.assertEqual(self.desktop.focus(self.bank_hwnd()), self.bcontrol(1002))
        self.bcommand(1006)
        self.select_first_point()
        self.bfield(1019, 'not_a_function(t)')
        self.bcommand(1020)
        before = self.doc()
        self.bcommand(1024)
        self.bcommand(1006)
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.bank()['previewSamples'], 0)
        self.assertTrue(self.bank()['dirty'], self.bank())
        self.bcommand(1014)
        self.bfield(1022, '1')  # Must not truncate existing points silently.
        self.bcommand(1025)
        self.assertEqual(self.bank()['shape']['span'], 64*256)
        self.assertTrue(self.bank()['fieldDraft'])
        self.bfield(1022, '128')
        self.bcommand(1025)
        self.bcommand(1006)
        entry = next(e for e in self.read('envelope.bank.list')['entries'] if e['id'] == self.bank()['selected'])
        self.assertEqual(entry['shape']['span'], 128*256)

    def test_modeless_controls_fit_minimum_size_and_unlink_from_catalogue(self):
        graph, node = self.capture()
        self.bcommand(1008)
        self.command(497)
        self.bcommand(1010)
        self.bselect(1001, 1)
        self.bcommand(1009)
        self.assertFalse(self.bank()['visible'], self.bank())
        self.assertEqual(self.read('envelope.bank.list', target=dict(kind='graph', graph=graph, node=node, pattern=0))['linkedTemplate'], '')
        self.command(497)
        self.bselect(1018, 8)
        user = private_desktop.user
        user.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
        user.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
        user.IsWindowVisible.argtypes = [wintypes.HWND]
        hwnd = self.bank_hwnd()
        scale = self.read('workspace.get')['dpi']/96
        self.assertTrue(user.SetWindowPos(hwnd, None, 0, 0, int(900*scale), int(620*scale), 0x16))
        frame = wintypes.RECT()
        user.GetWindowRect(hwnd, ctypes.byref(frame))
        row = []
        for identifier in range(1001, 1026):
            control = self.bcontrol(identifier)
            if user.IsWindowVisible(control):
                with self.subTest(control=identifier):
                    rect = wintypes.RECT()
                    user.GetWindowRect(control, ctypes.byref(rect))
                    self.assertGreater(rect.right, rect.left)
                    self.assertGreaterEqual(rect.left, frame.left)
                    self.assertLessEqual(rect.right, frame.right)
                    self.assertLessEqual(rect.bottom, frame.bottom)
                    if identifier in (1016, 1017, 1018, 1020, 1021):
                        row.append((rect.left, rect.right))
        row.sort()
        for left, right in zip(row, row[1:]):
            self.assertLessEqual(left[1], right[0])
        self.assertTrue(self.state()['visible'])  # Both editors remain available.

    def test_unavailable_catalogue_keeps_song_bank_usable(self):
        self.capture()
        self.catalogue.parent.mkdir(parents=True, exist_ok=True)
        broken = b'{broken catalogue'
        self.catalogue.write_bytes(broken)
        self.bcommand(1014)
        self.assertIn('Catalogue unavailable', self.bank()['status'])
        self.assertTrue(self.bank()['selected'])
        self.bfield(1003, 'Song bank is still editable')
        self.bcommand(1006)
        self.assertFalse(self.bank()['dirty'])
        self.assertEqual(self.read('envelope.bank.list')['entries'][0]['name'], 'Song bank is still editable')
        before = self.doc()
        self.bcommand(1010)
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.catalogue.read_bytes(), broken)

    def test_editing_master_preserves_instrument_flags_and_marker_positions(self):
        self.setup_curve()
        shape = dict(span=16384, rowsPerBeat=8, instrument=True, flags=31,
                     markers=[1024, 8192, 2048, 4096, 8192],
                     points=[dict(position=0, value=.2, curve='linear'),
                             dict(position=16383, value=.8, curve='linear')])
        template = self.write('envelope.bank.save', name='Instrument template', shape=shape)['id']
        self.command(487)
        self.command(497)
        self.assertEqual(self.bank()['selected'], template)
        self.select_first_point()
        self.bfield(1017, '35')
        self.bcommand(1020)
        self.bcommand(1006)
        saved = self.read('envelope.bank.list')['entries'][0]['shape']
        self.assertEqual(saved['points'][0]['value'], .35)
        for field in ('instrument', 'flags', 'markers', 'rowsPerBeat', 'span'):
            self.assertEqual(saved[field], shape[field])


if __name__ == '__main__':
    unittest.main()
