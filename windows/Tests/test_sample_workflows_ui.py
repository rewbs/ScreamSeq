"""Captured loop drafts, reviewed paste and boundary snapping through native controls."""
import base64
import ctypes
import struct
import unittest

import private_desktop
import test_sample_detail_ui as support
import test_workspace_shortcuts as shortcuts


class SampleWorkflowsUITests(unittest.TestCase):
    setUp = support.SampleDetailUITests.setUp
    doc = support.SampleDetailUITests.doc
    read = support.SampleDetailUITests.read
    write = support.SampleDetailUITests.write
    install = support.SampleDetailUITests.install
    pcm = support.SampleDetailUITests.pcm
    state = support.SampleDetailUITests.state
    window = support.SampleDetailUITests.window
    control = support.SampleDetailUITests.control
    field = support.SampleDetailUITests.field
    idle = support.SampleDetailUITests.idle
    ready = support.SampleDetailUITests.idle
    press = support.SampleDetailUITests.press
    page = support.SampleDetailUITests.page
    select = support.SampleDetailUITests.select
    start = support.SampleDetailUITests.start
    region = support.SampleDetailUITests.region
    view = support.SampleDetailUITests.view
    mouse = support.SampleDetailUITests.mouse
    key = support.SampleDetailUITests.key

    def text(self, identifier):
        value = ctypes.create_unicode_buffer(1024)
        self.desktop.send(self.control(identifier), 0xD, len(value), ctypes.addressof(value))
        return value.value

    def loop_draft(self, kind, first, last, direction=0, enabled=True):
        self.page('loops')
        toggle, start, end, mode = (5006, 5007, 5008, 5009) if kind == 'normal' else (5010, 5011, 5012, 5013)
        self.field(start, first)
        self.field(end, last)
        if self.state()['loops'][kind]['enabled'] != enabled:
            self.press(toggle)
        self.select(mode, direction)

    def clipboard(self, values=(16000, -16000, 12000, -12000, 8000, -8000, 4000, -4000), rate=24000):
        return self.write('sample.clipboard.set', format='s16le', channels=1, rate=rate,
                          data=base64.b64encode(struct.pack('<' + str(len(values)) + 'h', *values)).decode())

    def test_joint_loop_preview_is_read_only_and_apply_is_one_undo_with_native_storage(self):
        original = self.read('sample.get', sample=1)
        other = self.read('sample.get', sample=2)
        document = self.doc()
        self.page('loops')
        self.assertFalse(self.state()['loops']['normal']['enabled'])
        self.select(5009, 2)
        self.press(5006)
        self.assertTrue(self.state()['loops']['normal']['reverse'])
        self.press(5006)
        self.assertFalse(self.state()['loops']['normal']['enabled'])
        self.press(5006)
        self.assertTrue(self.state()['loops']['normal']['reverse'], 'Disabling a loop retains its selected direction')
        self.loop_draft('normal', 8, 56, direction=2)
        self.loop_draft('sustain', 24, 112, direction=1)
        self.assertTrue(self.state()['loops']['dirty'])
        self.assertEqual(self.doc(), document)
        self.press(5016)
        report = self.state()['report']
        self.assertTrue(report['dryRun'])
        self.assertTrue(report['loopsChanged'])
        self.assertEqual(self.doc(), document)
        self.assertEqual(self.pcm(), self.raw)
        self.press(5017)
        saved = self.read('sample.get', sample=1)
        self.assertEqual((saved['loopStart'], saved['loopEnd'], saved['sustainStart'], saved['sustainEnd']), (8, 56, 24, 112))
        self.assertTrue(saved['loop'] and saved['reverseLoop'] and saved['sustainLoop'] and saved['sustainPingpong'])
        self.assertFalse(saved['pingpong'] or saved['sustainReverse'])
        self.assertFalse(self.state()['loops']['dirty'])
        self.assertEqual(self.pcm(), self.raw)
        self.assertEqual(self.read('sample.get', sample=2), other)
        committed = self.doc()
        self.press(5017)
        self.assertEqual(self.doc(), committed)
        self.press(4804)
        self.assertEqual(self.read('sample.get', sample=1), original)
        self.press(4805)
        self.assertEqual(self.read('sample.get', sample=1), saved)
        path = self.folder / 'joint-loop-draft.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.read('sample.get', sample=1), saved)
        self.assertEqual(self.pcm(), self.raw)

    def test_raw_loop_drafts_survive_pages_hide_stale_edits_and_reload_is_explicit(self):
        self.loop_draft('normal', '1e', 56, direction=2)
        self.loop_draft('sustain', 24, 112, direction=1)
        raw = self.state()['loops']
        document = self.doc()
        for command in (5016, 5017):
            self.press(command)
            self.assertEqual(self.doc(), document)
            self.assertEqual(self.text(5007), '1e')
        self.page('clipboard')
        self.field(5021, '-')
        self.page('loops')
        self.assertEqual(self.state()['loops'], raw)
        self.press(4806)
        self.start()
        self.assertEqual(self.text(5007), '1e')
        self.select(4801, 1)
        self.assertEqual(self.state()['sample'], 1)
        self.assertEqual(self.text(5007), '1e')
        self.field(4856, 'Uncommitted name from the old revision')
        self.write('document.patch', title='Other edit while loop draft is captured')
        document = self.doc()
        self.field(5007, 8)
        self.press(5017)
        self.assertEqual(self.doc(), document)
        self.assertTrue(self.state()['loops']['stale'])
        self.assertEqual(self.text(5007), '8')
        self.press(5018)
        self.assertFalse(self.state()['loops']['dirty'])
        self.assertFalse(self.state()['loops']['stale'])
        saved = self.read('sample.get', sample=1)
        self.assertEqual((self.text(5007), self.text(5008)), (str(saved['loopStart']), str(saved['loopEnd'])))
        self.page('clipboard')
        self.assertEqual(self.text(5021), '-')
        self.assertEqual(self.text(4856), 'Uncommitted name from the old revision')
        self.press(4860)
        self.assertEqual(self.read('sample.get', sample=1)['name'], saved['name'])
        self.assertEqual(self.doc(), document)

        self.press(4803)
        self.loop_draft('normal', 8, 56)
        self.loop_draft('sustain', 24, 112)
        self.field(4819, '-')
        self.press(5017)
        self.assertTrue(self.read('sample.get', sample=1)['loop'])
        self.assertEqual(self.text(4819), '-', 'Applying loop metadata preserves an unfinished viewport field')
        self.press(4803)
        self.field(4819, '-')
        self.region(8, 24)
        self.assertEqual(self.text(4819), '-')
        self.press(4849)
        self.assertTrue(self.read('sample.clipboard.get')['available'])
        self.assertEqual(self.text(4819), '-')
        self.write('sample.patch', sample=1, values={'name': 'Latest saved sample name'})
        document = self.doc()
        self.press(5018)
        self.assertEqual(self.text(4856), 'Latest saved sample name')
        self.assertEqual(self.text(4819), '-')
        self.assertEqual(self.doc(), document)

        user = private_desktop.user
        w = ctypes.wintypes
        window, edit = self.window(), self.control(4819)
        self.desktop.send(edit, 0x201, 1, 4 | (4 << 16))
        self.desktop.send(edit, 0x202, 0, 4 | (4 << 16))
        self.desktop.send(edit, 0xB1, 0, 1)
        self.assertEqual(self.desktop.focus(window), edit)
        self.assertEqual(self.desktop.send(edit, 0xB0), 1 << 16)
        user.GetWindowRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
        user.SetWindowPos.argtypes = [w.HWND, w.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, w.UINT]
        rect = w.RECT()
        private_desktop.check(user.GetWindowRect(window, ctypes.byref(rect)))
        private_desktop.check(user.SetWindowPos(window, None, 0, 0, rect.right - rect.left - 40, rect.bottom - rect.top - 40, 0x16))
        self.idle()
        self.assertEqual(self.control(4819), edit)
        self.assertEqual(self.desktop.focus(window), edit, 'Resizing retains focus in the surviving visible field')
        self.assertEqual(self.desktop.send(edit, 0xB0), 1 << 16)
        self.assertEqual(self.text(4819), '-')

    def test_paste_options_preview_and_pcm_match_shared_api_for_rate_modes_gains_and_channels(self):
        cases = [(0, 0, 'both', -6, 0), (0, 1, 'both', -6, 0),
                 (1, 1, 'right', -6, 0), (2, 0, 'right', -6, -9),
                 (2, 1, 'both', 24, 0), (3, 0, 'both', -6, 0)]
        self.page('clipboard')
        for mode, rate, channel, source_gain, destination_gain in cases:
            with self.subTest(mode=mode, rate=rate, channel=channel, gain=source_gain):
                self.install(1, self.raw, 2)
                self.install(2, self.raw, 2)
                self.press(4803)
                clip = self.clipboard()
                self.region(16, 28)
                self.select(4813, 2 if channel == 'right' else 0)
                self.select(4852, mode)
                self.select(5020, rate)
                self.field(5021, source_gain)
                if mode == 2:
                    self.field(5022, destination_gain)
                params = dict(sample=2, at=16, mode=['insert', 'overwrite', 'mix', 'replace'][mode],
                              rateMode='keep-frames' if rate else 'resample', channels=channel,
                              sourceGainDB=source_gain, clipboardId=clip['clipboardId'])
                if mode == 2:
                    params['destinationGainDB'] = destination_gain
                if mode == 3:
                    params['end'] = 28
                document = self.doc()
                self.press(4853)
                self.assertEqual(self.doc(), document, 'Paste requires an explicit current review')
                self.press(5019)
                preview = self.state()['report']
                self.assertTrue(self.state()['pastePreview'])
                self.assertTrue(preview['dryRun'])
                self.assertEqual(self.doc(), document)
                expected = self.write('sample.paste', dryRun=True, **params)
                for key in ('insertedFrames', 'removedFrames', 'resultFrames', 'changedSamples', 'clippedSamples', 'peakAfter', 'changes', 'before', 'after'):
                    self.assertEqual(preview[key], expected[key], key)
                self.press(4853)
                self.assertFalse(self.state()['pastePreview'])
                actual = self.pcm()
                self.write('sample.paste', **params)
                self.assertEqual(actual, self.pcm(2))
                if channel == 'right':
                    self.assertEqual(actual[::2], self.raw[::2])
                if source_gain == 24:
                    self.assertGreater(preview['clippedSamples'], 0)

    def test_reviewed_paste_rejects_replaced_clipboard_and_changed_options_without_losing_drafts(self):
        self.page('clipboard')
        clip = self.clipboard(rate=48000)
        self.region(8, 16)
        self.select(4852, 2)
        self.field(5021, '-6.0')
        self.field(5022, '-3.0')
        self.press(5019)
        self.assertTrue(self.state()['pastePreview'])
        self.assertEqual(self.state()['report']['clipboardId'], clip['clipboardId'])
        self.clipboard((2000, -2000, 6000, -6000), rate=48000)
        document = self.doc()
        self.press(4853)
        self.assertEqual(self.doc(), document)
        self.assertEqual(self.pcm(), self.raw)
        self.assertIn('clipboard', self.state()['status'].lower())
        self.assertEqual((self.text(5021), self.text(5022)), ('-6.0', '-3.0'))
        self.assertFalse(self.state()['pastePreview'])
        self.press(5019)
        self.field(5021, '-')
        self.press(4853)
        self.assertEqual(self.doc(), document)
        self.assertEqual(self.text(5021), '-')
        self.field(5021, '-9')
        self.press(4853)
        self.assertEqual(self.doc(), document, 'Changed options require a new preview')
        self.press(5019)
        self.press(4853)
        self.assertNotEqual(self.pcm(), self.raw)
        self.press(4804)
        self.assertEqual(self.pcm(), self.raw)
        self.select(4852, 0)
        self.select(4813, 2)
        document = self.doc()
        self.press(5019)
        self.assertEqual(self.doc(), document)
        self.assertFalse(self.state()['pastePreview'])
        self.assertIn('both', self.state()['status'].lower())

    def test_grid_origin_zero_crossings_and_loop_snap_only_stage_boundaries(self):
        document = self.doc()
        self.page('snap')
        self.select(4840, 1)
        self.field(4842, 8)
        self.field(5023, 2)
        self.region(11, 53)
        self.press(4843)
        self.assertEqual((self.state()['start'], self.state()['end']), (10, 50))
        self.loop_draft('normal', 11, 53)
        self.loop_draft('sustain', 19, 99, direction=1)
        self.press(5014)
        self.press(5015)
        self.assertEqual((self.text(5007), self.text(5008), self.text(5011), self.text(5012)), ('10', '50', '18', '98'))
        self.assertEqual(self.doc(), document)
        self.loop_draft('normal', 17, 18)
        self.press(5014)
        self.assertEqual((self.text(5007), self.text(5008)), ('17', '18'))
        self.assertIn('collapse', self.state()['status'].lower())
        self.page('snap')
        self.select(4840, 0)
        self.field(4842, 32)
        self.select(4813, 2)
        self.region(11, 53)
        expected = self.read('sample.snap.get', sample=1, positions=[11, 53], mode='zero', radius=32, channels='right')
        self.press(4843)
        self.assertEqual([self.state()['start'], self.state()['end']], [value['after'] for value in expected['positions']])
        self.assertEqual(self.doc(), document)
        self.page('loops')
        self.press(4844)
        self.press(5017)
        saved = self.read('sample.get', sample=1)
        self.assertEqual([saved['loopStart'], saved['loopEnd']], [value['after'] for value in expected['positions']])
        self.assertEqual((saved['sustainStart'], saved['sustainEnd']), (18, 98))

    def test_automatic_selection_snap_runs_on_release_and_escape_or_capture_loss_restores_selection(self):
        self.page('snap')
        self.select(4840, 1)
        self.field(4842, 8)
        self.field(5023, 2)
        self.press(5024)
        self.assertTrue(self.state()['autoSnap'])
        self.view(0, 128)
        document = self.doc()
        x, y, width, height = self.state()['canvas']
        at = lambda frame: x + (frame + .2) * width / 128
        self.mouse(0x201, at(11), y + height / 2)
        self.mouse(0x200, at(53), y + height / 2)
        self.assertEqual((self.state()['start'], self.state()['end']), (11, 53), 'Do not snap an unfinished gesture')
        self.mouse(0x202, at(53), y + height / 2)
        self.idle()
        self.assertEqual((self.state()['start'], self.state()['end']), (10, 50))
        for cancel in ('escape', 'capture'):
            with self.subTest(cancel=cancel):
                self.mouse(0x201, at(31), y + height / 2)
                self.mouse(0x200, at(75), y + height / 2)
                if cancel == 'escape':
                    self.key(0x1B)
                else:
                    self.desktop.send(self.window(), 0x215, 0, self.control(5024))
                self.mouse(0x202, at(75), y + height / 2)
                self.idle()
                self.assertEqual((self.state()['start'], self.state()['end']), (10, 50))
        self.assertEqual(self.doc(), document)
        self.assertEqual(self.pcm(), self.raw)
        self.assertEqual(self.state()['points'], [])
        self.press(5024)
        self.mouse(0x201, at(11), y + height / 2)
        self.mouse(0x200, at(53), y + height / 2)
        self.mouse(0x202, at(53), y + height / 2)
        self.idle()
        self.assertEqual((self.state()['start'], self.state()['end']), (11, 53))

    def test_preview_buttons_and_ctrl_enter_keep_native_edit_focus_and_selection(self):
        user = private_desktop.user
        w = ctypes.wintypes
        user.SetFocus.argtypes = [w.HWND]
        user.SetFocus.restype = w.HWND
        user.SetActiveWindow.argtypes = [w.HWND]
        user.SetActiveWindow.restype = w.HWND
        user.GetDlgCtrlID.argtypes = [w.HWND]

        def focus(hwnd, active):
            caller = private_desktop.kernel.GetCurrentThreadId()
            target = user.GetWindowThreadProcessId(hwnd, None)
            private_desktop.check(user.AttachThreadInput(caller, target, True))
            try:
                user.SetActiveWindow(active)
                user.SetFocus(hwnd)
            finally:
                private_desktop.check(user.AttachThreadInput(caller, target, False))
            self.assertEqual(self.desktop.focus(active), hwnd)

        def retains(ident, action, clicked_button=None):
            edit, window = self.control(ident), self.window()
            focus(edit, window)
            self.desktop.send(edit, 0xB1, 0, 1)
            before = self.text(ident)
            self.assertEqual(self.desktop.send(edit, 0xB0), 1 << 16)
            action(edit)
            self.idle()
            self.assertEqual(self.control(ident), edit)
            focused = self.desktop.focus(window)
            # An actual button click takes focus; keyboard Apply keeps the EDIT.
            expected_focus = self.control(clicked_button) if clicked_button else edit
            self.assertEqual(focused, expected_focus, dict(status=self.state()['status'], focusedControl=user.GetDlgCtrlID(focused) if focused else None,
                                               focusIsTool=focused==window))
            self.assertEqual(self.desktop.send(edit, 0xB0), 1 << 16)
            self.assertEqual(self.text(ident), before)

        self.loop_draft('normal', 8, 56, direction=2)
        self.loop_draft('sustain', 24, 112, direction=1)
        before = self.doc()
        retains(5007, lambda edit: self.desktop.send(self.control(5016), 0xF5), clicked_button=5016)
        self.assertTrue(self.state()['report']['dryRun'])
        self.assertEqual(self.doc(), before)
        retains(5007, lambda edit: shortcuts.WorkspaceShortcutTests.key(self, 0x0D, hwnd=edit, ctrl=True))
        self.assertTrue(self.read('sample.get', sample=1)['reverseLoop'])
        self.assertFalse(self.state()['loops']['dirty'])
        self.assertEqual(self.pcm(), self.raw)

        # Completion must also respect an already active different window.
        # This exercises the activation guard without depending on worker timing.
        self.field(5007, 9)
        main = self.desktop.hwnd(self.pid)
        focus(main, main)
        before = self.doc()
        self.desktop.send(self.window(), 0x111, 5016, self.control(5016))
        self.idle()
        self.assertEqual(self.state()['report']['after']['normal']['start'], 9)
        self.assertEqual(self.desktop.focus(main), main)
        self.assertEqual(self.doc(), before)
        self.press(5018)

        self.clipboard()
        self.page('clipboard')
        self.select(4852, 2)
        self.field(5021, '-6')
        before = self.doc()
        retains(5021, lambda edit: self.desktop.send(self.control(5019), 0xF5), clicked_button=5019)
        self.assertTrue(self.state()['pastePreview'])
        self.assertTrue(self.state()['report']['dryRun'])
        self.assertEqual(self.doc(), before)
        retains(5021, lambda edit: shortcuts.WorkspaceShortcutTests.key(self, 0x0D, hwnd=edit, ctrl=True))
        self.assertFalse(self.state()['pastePreview'])
        self.assertNotEqual(self.pcm(), self.raw)


if __name__ == '__main__':
    unittest.main()
