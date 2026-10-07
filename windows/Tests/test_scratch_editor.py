"""Scratch paired-curve editor on an owned, never-switched Windows desktop.

SCREAMSEQ_TEST_EXE must name the exact native build under qualification. This
suite never opens a musician's process, switches desktops, or starts audio.
"""
import ctypes
from ctypes import wintypes
import json
import time
import unittest
import private_desktop
import test_pattern_performance as support


class ScratchEditorTests(unittest.TestCase):
    setUp = support.PatternPerformanceTests.setUp
    doc = support.PatternPerformanceTests.doc
    read = support.PatternPerformanceTests.read
    write = support.PatternPerformanceTests.write
    navigate = support.PatternPerformanceTests.navigate
    control = support.PatternPerformanceTests.control
    command = support.PatternPerformanceTests.command

    def state(self):
        return self.read('workspace.get')['scratchGestures']

    def hwnd(self, kind='ScreamSeq.ScratchGestures'):
        found = []
        @private_desktop.callback
        def visit(hwnd, _):
            pid = wintypes.DWORD()
            private_desktop.user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            name = ctypes.create_unicode_buffer(128)
            private_desktop.user.GetClassNameW(hwnd, name, 128)
            if pid.value == self.pid and name.value == kind:
                found.append(hwnd)
            return True
        private_desktop.check(private_desktop.user.EnumDesktopWindows(self.desktop.desktop, visit, 0))
        self.assertEqual(len(found), 1, found)
        return found[0]

    def child(self, identifier, kind='ScreamSeq.ScratchGestures'):
        child = private_desktop.user.GetDlgItem(self.hwnd(kind), identifier)
        self.assertTrue(child, identifier)
        return child

    def idle(self, previews=True):
        deadline, quiet = time.monotonic()+8, None
        while time.monotonic() < deadline:
            workspace = self.read('workspace.get')
            state = workspace['scratchGestures']
            ready = not workspace['documentBusy'] and not state.get('pending')
            if previews and state.get('selected'):
                ready = ready and all(l['previewSamples'] for l in state['lanes'])
            if not ready:
                quiet = None
            elif quiet is None:
                quiet = time.monotonic()
            elif time.monotonic()-quiet > .18:
                return state
            time.sleep(.02)
        self.fail(json.dumps(self.state()))

    def press(self, identifier, notification=0, settle=True):
        self.idle(False)
        # Menu IDs have no child HWND. They use the same WM_COMMAND handler as
        # TrackPopupMenu's returned selection, without global keyboard input.
        child = 0 if identifier >= 4001 or identifier in (3105, 3106, 3107, 3127, 3128) else self.child(identifier)
        self.desktop.send(self.hwnd(), 0x111, identifier | (notification << 16), child)
        return self.idle(False) if settle else self.state()

    def field(self, identifier, value, kind='ScreamSeq.ScratchGestures'):
        text = ctypes.create_unicode_buffer(str(value))
        self.desktop.send(self.child(identifier, kind), 0xC, 0, ctypes.addressof(text))

    def select_point(self, index, lane=0):
        self.desktop.send(self.child(3109), 0x14E, lane)
        self.press(3109, 1)
        self.desktop.send(self.child(3110), 0x186, index)
        self.press(3110, 1)

    def mouse(self, message, point, buttons=0):
        scale = self.read('workspace.get')['dpi']/96
        packed = (round(point['x']*scale) & 65535) | ((round(point['y']*scale) & 65535) << 16)
        self.desktop.send(self.hwnd(), message, buttons, packed)

    def setup_phrase(self, preset=1):
        self.navigate(pattern=0, row=3, channel=1, column=3, following=False)
        self.command(513)
        self.assertTrue(self.state()['visible'])
        self.press(4001+preset)  # Direct New phrase preset menu choice.
        state = self.idle()
        self.assertTrue(state['selected'], state)
        return state['selected']

    def bank(self):
        return self.read('scratch.gestures.get')['gestures']

    def command_at(self, row=3, channel=1):
        return next(c for c in self.read('pattern.effects.get', pattern=0)['commands']
                    if c['position']//65536 == row and c['channel'] == channel and c['column'] == 0)

    def test_pair_edit_captured_target_unique_one_undo_and_reopen(self):
        phrase = self.setup_phrase()
        self.press(3125)  # Use in captured pattern cell.
        self.assertEqual(self.command_at()['parameters']['gesture'], phrase)
        linkage = ctypes.create_unicode_buffer(512)
        self.desktop.send(self.child(3202), 0xD, len(linkage), ctypes.addressof(linkage))
        self.assertIn('1 linked SK uses', linkage.value)
        # Cursor navigation does not redirect a later Use or Make unique.
        self.navigate(row=9, channel=2, column=3)
        self.select_point(0)
        self.field(3112, 23.5)
        self.press(3115)
        self.assertEqual(self.bank()[0]['motion'][0]['value'], .235)
        self.assertEqual(self.state()['target']['row'], 3)
        prior_bank, prior_cell = self.bank(), self.command_at()
        self.press(3106)  # More / Make unique at captured row.
        copied = self.state()['selected']
        self.assertNotEqual(copied, phrase)
        after = self.command_at()
        expected = dict(prior_cell, parameters=dict(prior_cell['parameters'], gesture=copied))
        self.assertEqual(after, expected)
        self.assertEqual(len(self.bank()), 2)
        self.press(3127)  # More / Undo: phrase and cell together.
        self.assertEqual(self.bank(), prior_bank)
        self.assertEqual(self.command_at(), prior_cell)
        restored = self.state()
        self.assertEqual(restored['selected'], phrase)
        self.assertTrue(all(lane['points'] for lane in restored['lanes']), restored)
        self.press(3128)
        self.assertEqual(self.command_at(), after)
        path = self.folder/'scratch-editor.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.command_at(), after)
        self.assertEqual(len(self.bank()), 2)
        self.assertTrue(self.state()['stale'])
        self.press(3108)  # Explicit Reload adopts the reopened document.
        self.assertFalse(self.state()['stale'])

    def test_native_fields_drag_cancel_endpoints_and_formula_workbench(self):
        self.setup_phrase(0)
        initial = self.bank()
        self.select_point(0)
        self.press(3116)  # Endpoints cannot be deleted.
        self.assertEqual(self.bank(), initial)
        self.assertIn('endpoint', self.state()['status'].lower())
        self.press(3129)  # Add point: accessible alternative to canvas click.
        self.field(3111, .25)
        self.field(3112, 37.5)
        self.press(3115)
        self.assertEqual(self.bank()[0]['motion'][1]['position'], 16384)
        self.assertEqual(self.bank()[0]['motion'][1]['value'], .375)
        self.assertEqual(self.bank()[0]['fader'], initial[0]['fader'])
        state = self.idle()
        handle = state['lanes'][0]['handles'][1]
        self.mouse(0x201, handle, 1)
        self.mouse(0x200, dict(x=handle['x']+20, y=handle['y']+30), 1)
        self.desktop.send(self.hwnd(), 0x100, 0x1B)
        self.assertEqual(self.bank()[0]['motion'], state['draft']['motion'])
        self.assertEqual(self.state()['draft']['motion'], state['draft']['motion'])
        self.select_point(1)
        self.desktop.send(self.child(3113), 0x14E, 8)
        self.press(3113, 1)  # Scripted, default valid expression.
        self.press(3117)
        formula = 'ScreamSeq.FormulaWorkbench'
        self.field(2001, 'mix(start,end,t*t)', formula)
        deadline = time.monotonic()+5
        while time.monotonic()<deadline:
            state = self.state()['formulaWorkbench']
            if not state['checking'] and state['valid']:
                break
            time.sleep(.03)
        self.assertTrue(state['valid'], state)
        self.desktop.send(self.hwnd(formula), 0x111, 2007, self.child(2007, formula))
        self.idle()
        self.assertEqual(self.bank()[0]['motion'][1]['formula'], 'mix(start,end,t*t)')
        self.assertFalse(self.state()['formulaWorkbench']['visible'])
        self.field(3114, 'invalid(')
        self.press(3115)
        self.assertTrue(self.state()['fieldDraft'])
        self.assertEqual(self.bank()[0]['motion'][1]['formula'], 'mix(start,end,t*t)')
        # Reject a native lane-selection notification without lying about the
        # retained point or losing the invalid text.
        self.desktop.send(self.child(3109), 0x14E, 1)
        self.press(3109, 1)
        self.assertEqual(self.state()['activeLane'], 0)
        self.assertEqual(self.desktop.send(self.child(3109), 0x147), 0)
        self.assertEqual(self.state()['fields']['formula'], 'invalid(')

    def test_external_revision_and_document_switch_retain_draft_until_reload(self):
        self.setup_phrase()
        path = self.folder/'captured.screamseq'
        self.write('document.save', path=str(path))
        self.press(3108)
        self.select_point(1)
        self.field(3112, 63.75)
        self.write('document.patch', title='Edited elsewhere')
        old_bank = self.bank()
        self.press(3115)
        self.assertEqual(self.bank(), old_bank)
        self.assertTrue(self.state()['dirty'])
        captured = self.state()['document']
        self.write('document.open', path=str(path), discard=True)
        reopened = self.bank()
        self.command(513)
        self.assertEqual(self.state()['document'], captured)
        self.assertTrue(self.state()['dirty'])
        self.press(3125)  # A stale draft cannot be used in the new song.
        self.assertEqual(self.bank(), reopened)
        self.press(3108)
        self.assertFalse(self.state()['dirty'])
        self.assertFalse(self.state()['stale'])
        self.assertNotEqual(self.state()['document'], captured)
        self.assertEqual(self.state()['draft']['motion'], reopened[0]['motion'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
