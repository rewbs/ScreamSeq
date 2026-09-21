"""Real native formula drafts, Rich Edit input and guarded parent completion."""
import ctypes
from ctypes import wintypes
import time
import unittest
import private_desktop
import test_envelope_bank_ui as support
from client import ApiError


class FormulaWorkbenchTests(unittest.TestCase):
    setUp = support.EnvelopeBankUITests.setUp
    doc = support.EnvelopeBankUITests.doc
    read = support.EnvelopeBankUITests.read
    write = support.EnvelopeBankUITests.write
    control = support.EnvelopeBankUITests.control
    command = support.EnvelopeBankUITests.command
    field = support.EnvelopeBankUITests.field
    select = support.EnvelopeBankUITests.select
    point = support.EnvelopeBankUITests.point
    mouse = support.EnvelopeBankUITests.mouse
    key = support.EnvelopeBankUITests.key
    state = support.EnvelopeBankUITests.state
    settle = support.EnvelopeBankUITests.settle
    setup_curve = support.EnvelopeBankUITests.setup_curve
    saved = support.EnvelopeBankUITests.saved
    bank = support.EnvelopeBankUITests.bank
    bank_hwnd = support.EnvelopeBankUITests.bank_hwnd
    bcontrol = support.EnvelopeBankUITests.bcontrol
    bcommand = support.EnvelopeBankUITests.bcommand
    bfield = support.EnvelopeBankUITests.bfield
    bselect = support.EnvelopeBankUITests.bselect
    bmouse = support.EnvelopeBankUITests.bmouse
    idle = support.EnvelopeBankUITests.idle
    select_first_point = support.EnvelopeBankUITests.select_first_point

    def workbench(self, bank=False, reference=False):
        context = self.bank() if bank else self.read('workspace.get')
        return context['formulaReference' if reference else 'formulaWorkbench']

    def whwnd(self, bank=False, reference=False):
        owner = self.bank_hwnd() if bank else self.desktop.hwnd(self.pid)
        user = private_desktop.user
        user.GetWindow.argtypes = [wintypes.HWND, wintypes.UINT]
        user.GetWindow.restype = wintypes.HWND
        found = []
        @private_desktop.callback
        def visit(hwnd, _):
            pid = wintypes.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            name = ctypes.create_unicode_buffer(128)
            user.GetClassNameW(hwnd, name, 128)
            expected = 'ScreamSeq.FormulaReference' if reference else 'ScreamSeq.FormulaWorkbench'
            if pid.value == self.pid and name.value == expected and user.GetWindow(hwnd, 4) == owner:
                found.append(hwnd)
            return True
        private_desktop.check(user.EnumDesktopWindows(self.desktop.desktop, visit, 0))
        self.assertEqual(len(found), 1, found)
        return found[0]

    def wcontrol(self, identifier, bank=False, reference=False):
        result = private_desktop.user.GetDlgItem(self.whwnd(bank, reference), identifier)
        self.assertTrue(result)
        return result

    def wcommand(self, identifier, bank=False, reference=False):
        self.desktop.send(self.whwnd(bank, reference), 0x111, identifier, self.wcontrol(identifier, bank, reference))

    def wfield(self, identifier, value, bank=False, reference=False):
        text = ctypes.create_unicode_buffer(value)
        self.desktop.send(self.wcontrol(identifier, bank, reference), 0xC, 0, ctypes.addressof(text))

    def wait_preview(self, bank=False):
        deadline = time.monotonic()+5
        while time.monotonic()<deadline:
            state = self.workbench(bank)
            if not state['checking']:
                return state
            time.sleep(.02)
        self.fail(str(self.workbench(bank)))

    def setup_formula(self):
        target = self.setup_curve()
        self.command(490)
        self.select(481, 8)
        self.field(485, 'mix(start,end,t)')
        self.command(498)  # Expand stages pending native fields first.
        self.assertTrue(self.wait_preview()['valid'], self.workbench())
        return target

    def modified_key(self, hwnd, key, ctrl=False):
        # Both threads are on this owned, never-switched desktop. Their shared
        # input queue does not include a musician's window or the input desktop.
        user = private_desktop.user
        user.AttachThreadInput.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.BOOL]
        user.GetKeyboardState.argtypes = [ctypes.POINTER(ctypes.c_ubyte)]
        user.SetKeyboardState.argtypes = [ctypes.POINTER(ctypes.c_ubyte)]
        thread = private_desktop.kernel.GetCurrentThreadId()
        target = user.GetWindowThreadProcessId(hwnd, None)
        private_desktop.check(user.AttachThreadInput(thread, target, True))
        original = (ctypes.c_ubyte*256)()
        try:
            private_desktop.check(user.GetKeyboardState(original))
            state = (ctypes.c_ubyte*256).from_buffer_copy(original)
            if ctrl:
                state[0x11] = state[0xA2] = 0x80
            private_desktop.check(user.SetKeyboardState(state))
            self.desktop.send(hwnd, 0x100, key)
        finally:
            user.SetKeyboardState(original)
            private_desktop.check(user.AttachThreadInput(thread, target, False))

    def test_use_updates_only_captured_point_then_history_and_reopen(self):
        graph, node = self.setup_formula()
        before = self.doc()
        source = 'mix(start,end,\r\nt*t*t)'
        self.wfield(2001, source)
        preview = self.wait_preview()
        self.assertTrue(preview['valid'], preview)
        self.assertEqual(preview['previewSamples'], 1024)
        points = self.state()['points']
        expected = [dict(p) for p in points]
        expected[0]['formula'] = preview['source']
        actual = self.read('automation.formula.preview', points=expected, rows=64, samples=1024)
        self.assertEqual(preview['values'], actual['values'])
        self.assertEqual(self.doc(), before)
        self.wcommand(2007)
        self.assertFalse(self.workbench()['visible'])
        self.assertEqual(self.state()['points'], expected)
        self.assertEqual(self.doc(), before)
        self.command(486)
        self.assertEqual(self.saved(graph, node)['points'], expected)
        self.write('history.undo', domain='document')
        self.assertEqual(self.saved(graph, node)['points'], [])
        self.write('history.redo', domain='document')
        path = self.folder / 'workbench.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.saved(graph, node)['points'], expected)

    def test_invalid_text_immediately_revokes_use_and_retains_draft(self):
        self.setup_formula()
        parent = self.state()['points']
        before = self.doc()
        self.wfield(2001, 'missing_function(t)')
        self.assertFalse(self.workbench()['valid'])
        self.wcommand(2007)
        result = self.wait_preview()
        self.assertFalse(result['valid'])
        self.assertEqual(result['previewSamples'], 0)
        self.assertIn('missing_function', result['source'])
        self.wcommand(2007)
        self.assertEqual(self.state()['points'], parent)
        self.assertEqual(self.doc(), before)
        self.assertTrue(self.workbench()['visible'])
        self.wfield(2001, 'sqrt(-1)')  # Defined shared evaluator fallback is valid.
        result = self.wait_preview()
        self.assertTrue(result['valid'], result)
        self.assertAlmostEqual(result['values'][512][1], 512/1024, delta=.002)

    def test_multiline_completion_ctrl_space_undo_and_caret_guard(self):
        self.setup_formula()
        field = self.wcontrol(2001)
        self.wfield(2001, '.5 +\r\nsi')
        initial = self.workbench()['source']
        self.desktop.send(field, 0xB1, len(initial), len(initial))  # EM_SETSEL.
        self.modified_key(field, 0x20, ctrl=True)
        self.assertEqual(self.workbench()['completions'], ['sin(tau*beats)'])
        self.desktop.send(field, 0x100, 0xD)
        self.desktop.send(field, 0x102, 0xD)  # TranslateMessage's queued Enter.
        completed = self.workbench()['source']
        self.assertEqual(completed, initial[:-2]+'sin(tau*beats)')
        self.desktop.send(field, 0xC7)  # EM_UNDO stays in the formula editor.
        self.assertEqual(self.workbench()['source'], initial)
        self.desktop.send(field, 0x454)  # EM_REDO.
        self.assertEqual(self.workbench()['source'], completed)
        self.wfield(2001, 'mi')
        self.desktop.send(field, 0xB1, 2, 2)
        self.wcommand(2004)
        self.desktop.send(field, 0xB1, 0, 0)  # Caret changed while menu was open.
        self.desktop.send(field, 0x100, 0xD)
        self.desktop.send(field, 0x102, 0xD)
        self.assertEqual(self.workbench()['source'], 'mi')
        self.assertFalse(self.workbench()['completionVisible'])
        self.assertIn('caret changed', self.workbench()['status'])
        long = ' '*2046+'si'
        self.wfield(2001, long)
        self.desktop.send(field, 0xB1, len(long), len(long))
        self.wcommand(2004)
        self.desktop.send(field, 0x100, 0xD)
        self.desktop.send(field, 0x102, 0xD)
        self.assertEqual(self.workbench()['source'], long)
        self.assertIn('limit', self.workbench()['status'])

    def test_reference_search_insert_local_focus_and_minimum_bounds(self):
        self.setup_formula()
        reference = self.read('automation.formula.reference')
        self.assertEqual(self.workbench()['symbols'], [s['name'] for s in reference['symbols']])
        self.wfield(2001, '.5 + ')
        self.desktop.send(self.wcontrol(2001), 0xB1, 5, 5)
        self.wfield(2002, 'deterministic')
        self.assertEqual(self.workbench()['symbols'], ['noise'])
        self.wcommand(2005)
        self.assertEqual(self.workbench()['source'], '.5 + noise(beats,17)')
        self.wait_preview()
        self.desktop.send(self.wcontrol(2001), 0x100, 0x75)  # F6 to reference search.
        self.assertEqual(self.desktop.focus(self.whwnd()), self.wcontrol(2002))
        self.wcommand(2006)
        self.wait_preview()
        self.assertEqual(self.desktop.focus(self.whwnd()), self.wcontrol(2002))
        self.assert_bounds(self.whwnd(), 840, 600)

    def assert_bounds(self, hwnd, width, height):
        user = private_desktop.user
        user.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
        user.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
        user.IsWindowVisible.argtypes = [wintypes.HWND]
        scale = self.read('workspace.get')['dpi']/96
        self.assertTrue(user.SetWindowPos(hwnd, None, 0, 0, int(width*scale), int(height*scale), 0x16))
        frame = wintypes.RECT()
        user.GetWindowRect(hwnd, ctypes.byref(frame))
        buttons = []
        for identifier in range(2001, 2016):
            control = user.GetDlgItem(hwnd, identifier)
            self.assertTrue(control)
            if user.IsWindowVisible(control):
                with self.subTest(control=identifier):
                    rect = wintypes.RECT()
                    user.GetWindowRect(control, ctypes.byref(rect))
                    self.assertGreater(rect.right, rect.left)
                    self.assertGreaterEqual(rect.left, frame.left)
                    self.assertLessEqual(rect.right, frame.right)
                    self.assertLessEqual(rect.bottom, frame.bottom)
                    if identifier in (2004, 2006, 2008, 2009):
                        buttons.append((rect.top, rect.left, rect.right))
        buttons.sort()
        for left, right in zip(buttons, buttons[1:]):
            if left[0] == right[0]:
                self.assertLessEqual(left[2], right[1])

    def test_stale_parent_selection_document_and_reopen_keep_text(self):
        self.setup_formula()
        hwnd = self.whwnd()
        self.wfield(2001, 'mix(start,end,t*t)')
        self.wait_preview()
        self.field(484, '30')
        self.command(488)
        parent = self.state()['points']
        self.command(498)
        self.assertEqual(self.whwnd(), hwnd)
        self.wcommand(2007)
        self.assertEqual(self.state()['points'], parent)
        self.assertTrue(self.workbench()['visible'])
        self.assertFalse(self.workbench()['sourceCurrent'])
        self.assertEqual(self.workbench()['source'], 'mix(start,end,t*t)')
        self.wcommand(2008)
        self.command(498)
        self.assertEqual(self.whwnd(), hwnd)
        self.assertEqual(self.workbench()['source'], 'mix(start,end,t*t)')
        path = self.folder / 'replacement.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path), discard=True)
        before = self.doc()
        self.wcommand(2007)
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.workbench()['source'], 'mix(start,end,t*t)')
        self.wcommand(2009)
        self.assertFalse(self.workbench()['dirty'])
        self.assertFalse(self.workbench()['visible'])

    def test_bank_formula_is_independent_and_master_use_preserves_other_points(self):
        self.setup_formula()
        graph_formula = self.whwnd()
        self.command(497)
        self.bcommand(1005)
        self.select_first_point()
        self.bcommand(1026)
        self.assertNotEqual(self.whwnd(bank=True), graph_formula)
        self.wfield(2001, 'mix(start,end,t*t*t)', bank=True)
        self.assertTrue(self.wait_preview(bank=True)['valid'])
        self.wcommand(2007, bank=True)
        self.assertTrue(self.bank()['dirty'])
        self.assertEqual(self.bank()['shape']['points'][0]['formula'], 'mix(start,end,t*t*t)')
        self.assertEqual(self.workbench()['source'], 'mix(start,end,t)')
        self.bcommand(1006)
        saved = self.read('envelope.bank.list')['entries'][0]['shape']
        self.assertEqual(saved['points'][0]['formula'], 'mix(start,end,t*t*t)')
        self.select_first_point()
        self.bcommand(1026)
        self.wfield(2001, '.25', bank=True)
        self.wait_preview(bank=True)
        self.bfield(1017, '70')
        self.bcommand(1020)
        retained = self.bank()['shape']
        self.wcommand(2007, bank=True)
        self.assertEqual(self.bank()['shape'], retained)
        self.assertTrue(self.workbench(bank=True)['visible'])
        self.assertEqual(self.workbench(bank=True)['source'], '.25')

    def test_bank_maximum_beat_signature_preview_matches_accepted_shape(self):
        self.setup_curve()
        points = [dict(position=0, value=0, curve='scripted', formula='beat')]
        template = self.write('envelope.bank.save', name='Full beat division',
                              shape=dict(span=16777216, rowsPerBeat=65536, points=points))['id']
        self.command(487)
        self.command(497)
        self.assertEqual(self.bank()['selected'], template)
        self.select_first_point()
        self.bcommand(1026)
        result = self.wait_preview(bank=True)
        self.assertTrue(result['valid'], result)
        self.assertEqual(result['values'][0], [0, 0])
        self.assertEqual(result['values'][-1], [16777216, 1])
        before = self.doc()
        with self.assertRaises(ApiError):
            self.read('automation.formula.preview', points=points, rows=65536,
                      rowsPerBeat=65537, samples=3)
        self.assertEqual(self.doc(), before)

    def test_selection_guard_and_maximized_reopen_preserve_window_and_text(self):
        self.setup_formula()
        hwnd = self.whwnd()
        self.wfield(2001, '.25')
        self.wait_preview()
        parent = self.state()['points']
        last = self.state()['handles'][-1]
        self.mouse(0x201, last, 1)
        self.mouse(0x202, last)
        self.wcommand(2007)
        self.assertEqual(self.state()['points'], parent)
        self.assertEqual(self.workbench()['source'], '.25')
        self.assertFalse(self.workbench()['sourceCurrent'])
        user = private_desktop.user
        user.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
        user.IsZoomed.argtypes = [wintypes.HWND]
        user.ShowWindow(hwnd, 3)
        self.command(498)
        self.assertEqual(self.whwnd(), hwnd)
        self.assertTrue(user.IsZoomed(hwnd))
        self.assertEqual(self.workbench()['source'], '.25')
        first = self.state()['handles'][0]
        self.mouse(0x201, first, 1)
        self.mouse(0x202, first)
        self.wcommand(2007)
        self.assertFalse(self.workbench()['visible'])
        self.assertEqual(self.state()['points'][0]['formula'], '.25')
        self.assertEqual(self.state()['points'][1:], parent[1:])

    def test_standalone_reference_does_not_change_pattern_context(self):
        before = self.read('workspace.get')['focus']
        self.command(499)
        self.assertTrue(self.workbench(reference=True)['visible'])
        self.assertTrue(self.workbench(reference=True)['referenceOnly'])
        self.assertEqual(self.read('workspace.get')['focus'], before)
        self.wfield(2002, 'radians', reference=True)
        self.assertIn('sin', self.workbench(reference=True)['symbols'])
        self.assert_bounds(self.whwnd(reference=True), 440, 450)
        self.wcommand(2008, reference=True)
        self.assertFalse(self.workbench(reference=True)['visible'])
        self.assertEqual(self.read('workspace.get')['focus'], before)


if __name__ == '__main__':
    unittest.main()
