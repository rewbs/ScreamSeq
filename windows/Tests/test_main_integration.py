"""Incoming main APIs through the actual Windows process and guarded history."""
import unittest
import ctypes
import time
import private_desktop
import test_graph_mixer_app as support
from client import ApiError


class MainIntegrationTests(unittest.TestCase):
    setUp = support.GraphMixerAppTests.setUp
    doc = support.GraphMixerAppTests.doc
    read = support.GraphMixerAppTests.read
    write = support.GraphMixerAppTests.write
    add_gain = support.GraphMixerAppTests.add_gain
    control = support.GraphMixerAppTests.control

    def test_direct_selection_render_has_one_history_entry_and_exact_reopen(self):
        for command, instrument in [(578, False), (579, True)]:
            before = self.doc()
            self.desktop.send(self.desktop.hwnd(self.pid), 0x111, command)
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                workspace = self.read('workspace.get')
                state = workspace['patternSampleRenderAction']
                if (not workspace['pendingViewCommands'] and not state['pending'] and state['report']
                        and state['target'].get('expectedRevision') == before['revision']):
                    break
                time.sleep(.01)
            else:
                self.fail('Direct render did not complete')
            self.assertIsNone(state['completion'])
            self.assertEqual(state['target']['createInstrument'], instrument)
            result = state['report']
            self.assertGreater(result['frames'], 0)
            self.assertEqual(bool(result.get('instrument')), instrument)
            self.assertEqual(len(self.doc()['data']['samples']), len(before['data']['samples']) + 1)
            sample = self.read('sample.get', sample=result['sample'])
            pcm = self.read('sample.pcm.get', sample=result['sample'], frames=4096)
            self.write('history.undo', domain='all')
            self.assertEqual(self.doc()['data']['samples'], before['data']['samples'])
            self.write('history.redo', domain='all')
            self.assertEqual(self.read('sample.get', sample=result['sample']), sample)
            path = self.folder / ('direct-instrument.screamseq' if instrument else 'direct-sample.screamseq')
            self.write('document.save', path=str(path))
            self.write('document.open', path=str(path), discard=True)
            self.assertEqual(self.read('sample.get', sample=result['sample']), sample)
            self.assertEqual(self.read('sample.pcm.get', sample=result['sample'], frames=4096), pcm)

    def test_native_selection_render_one_import_history_and_reopen(self):
        self.desktop.send(self.desktop.hwnd(self.pid), 0x111, 580)
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            state = self.read('workspace.get')
            if state['patternSampleRender']['visible'] and not state['pendingViewCommands']:
                break
            time.sleep(.01)
        else:
            self.fail('Render options did not open')
        found = []
        @private_desktop.callback
        def visit(hwnd, _):
            pid = ctypes.wintypes.DWORD()
            private_desktop.user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            name = ctypes.create_unicode_buffer(100)
            private_desktop.user.GetClassNameW(hwnd, name, 100)
            if pid.value == self.pid and name.value == 'ScreamSeq.PatternSampleRender':
                found.append(hwnd)
            return True
        private_desktop.check(private_desktop.user.EnumDesktopWindows(self.desktop.desktop, visit, 0))
        self.assertEqual(len(found), 1)
        hwnd = found[0]
        def press(identifier):
            child = private_desktop.user.GetDlgItem(hwnd, identifier)
            self.assertTrue(child)
            self.desktop.send(hwnd, 0x111, identifier, child)
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                view = self.read('workspace.get')['patternSampleRender']
                if not view['pending']:
                    return view
                time.sleep(.01)
            self.fail('Render request did not complete')
        before = self.doc()
        press(6605)
        self.assertEqual(self.doc(), before)
        rendered = press(6606)
        self.assertIsNone(rendered['completion'])
        result = rendered['report']
        self.assertGreater(result['frames'], 0)
        after = self.doc()
        self.assertEqual(len(after['data']['samples']), len(before['data']['samples']) + 1)
        sample = self.read('sample.get', sample=result['sample'])
        pcm = self.read('sample.pcm.get', sample=result['sample'], frames=4096)
        # The retained selection is stale after its own import; no second write.
        press(6606)
        self.assertEqual(self.doc(), after)
        self.write('history.undo', domain='all')
        self.assertEqual(self.doc()['data']['samples'], before['data']['samples'])
        self.write('history.redo', domain='all')
        self.assertEqual(self.read('sample.get', sample=result['sample']), sample)
        path = self.folder / 'native-selection-render.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.read('sample.get', sample=result['sample']), sample)
        self.assertEqual(self.read('sample.pcm.get', sample=result['sample'], frames=4096), pcm)

    def test_input_is_atomic_context_only_and_supports_empty_slots(self):
        before = self.doc()
        context = self.read('context.get')
        guards = dict(expectedRevision=before['revision'], expectedContext=context['contextRevision'])
        for invalid in [dict(instrument=2, octave=True), dict(instrument=0), dict(octave=9), dict(octave=1.5), dict(instrument=2, extra=1)]:
            with self.assertRaises(ApiError):
                self.client.call('workspace.input', {**guards, **invalid})
            self.assertEqual(self.read('context.get')['contextRevision'], context['contextRevision'])
        result = self.client.call('workspace.input', {**guards, 'instrument': 255.0, 'octave': 6.0})
        self.assertFalse(result['changed'])
        self.assertFalse(result['playbackStopped'])
        self.assertEqual(result['revision'], before['revision'])
        self.assertEqual((result['data']['instrument'], result['data']['octave']), (255, 6))
        self.assertNotEqual(result['data']['contextRevision'], context['contextRevision'])
        with self.assertRaises(ApiError) as rejected:
            self.client.call('workspace.input', {**guards, 'instrument': 3})
        self.assertEqual(rejected.exception.code, -32001)
        self.assertEqual(self.doc(), before)
        current = self.read('context.get')
        self.assertEqual((current['instrument'], current['octave']), (255, 6))
        self.assertIn('workspace.input', self.read('api.describe')['writes'])
        chooser = self.control(135)
        selected = self.desktop.send(chooser, 0x147)
        self.assertEqual(self.desktop.send(chooser, 0x150, selected), 255)
        text = ctypes.create_unicode_buffer(128)
        self.desktop.send(chooser, 0x148, selected, ctypes.addressof(text))
        self.assertIn('255', text.value)
        self.assertIn('empty slot', text.value)

        window = self.desktop.hwnd(self.pid)
        at_limit = self.read('context.get')
        self.desktop.send(window, 0x111, 595)
        self.assertEqual(self.read('context.get'), at_limit)
        self.desktop.send(window, 0x111, 594)
        self.assertEqual(self.read('context.get')['instrument'], 254)
        self.desktop.send(window, 0x111, 593)
        self.assertEqual(self.read('context.get')['octave'], 7)
        self.desktop.send(window, 0x111, 592)
        self.assertEqual(self.read('context.get')['octave'], 6)
        self.assertEqual(self.doc(), before)
        self.write('pattern.apply', cells=[dict(pattern=0, row=12, channel=0, instrument=3)])
        context = self.client.call('context.get')
        self.client.call('context.set', dict(expectedRevision=context['revision'], expectedContext=context['data']['contextRevision'],
                                             pattern=0, row=12, channel=0, column=1, following=False))
        music = self.doc()
        self.desktop.send(window, 0x111, 596)
        self.assertEqual(self.read('context.get')['instrument'], 3)
        self.assertEqual(self.read('context.get')['row'], 12)
        self.assertEqual(self.doc(), music)

    def test_activity_is_truthful_before_preparation_and_ui_opens(self):
        plugin = self.add_gain()
        before = self.doc()
        targets = self.read('parameter.activity.targets')
        self.assertEqual(targets['targets'], [])
        self.assertFalse(targets['active'])
        self.assertEqual(self.read('parameter.activity.get')['points'], [])
        with self.assertRaises(ApiError):
            self.client.call('parameter.activity.watch', dict(target='unavailable', parameter=1))
        self.assertEqual(self.doc(), before)
        description = self.read('api.describe')
        self.assertIn('parameter.activity.watch', description['writes'])
        self.assertEqual(description['revisionGuards']['parameter.activity.watch'], [])
        self.desktop.send(self.desktop.hwnd(self.pid), 0x111, 575)
        activity = self.read('workspace.get')['parameterActivity']
        self.assertTrue(activity['visible'])
        self.assertEqual(activity['targets'], [])
        self.assertIn('playback', activity['status'])

    def test_recorded_point_edit_dry_run_move_history_and_save(self):
        plugin = self.add_gain()
        self.write('automation.recorded.edit', plugin=plugin, parameter=1, frame=0, value=-6)
        before = self.doc()
        self.write('automation.recorded.edit', plugin=plugin, parameter=1, frame=0, value=-3, newFrame=24000, dryRun=True)
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.read('automation.recorded.get', plugin=plugin, parameter=1)['points'], [dict(frame=0, value=-6)])
        self.write('automation.recorded.edit', plugin=plugin, parameter=1, frame=0, value=-3, newFrame=24000)
        lane = self.read('automation.recorded.get', plugin=plugin, parameter=1)
        self.assertEqual(lane['sampleRate'], 48000)
        self.assertEqual(lane['points'], [dict(frame=24000, value=-3)])
        self.write('history.undo', domain='plugins')
        self.assertEqual(self.read('automation.recorded.get', plugin=plugin, parameter=1)['points'], [dict(frame=0, value=-6)])
        self.write('history.redo', domain='plugins')
        path = self.folder / 'main-merge.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.read('automation.recorded.get', plugin=plugin, parameter=1), lane)


if __name__ == '__main__':
    unittest.main()
