"""Incoming main APIs through the actual Windows process and guarded history."""
import unittest
import ctypes
import test_graph_mixer_app as support
from client import ApiError


class MainIntegrationTests(unittest.TestCase):
    setUp = support.GraphMixerAppTests.setUp
    doc = support.GraphMixerAppTests.doc
    read = support.GraphMixerAppTests.read
    write = support.GraphMixerAppTests.write
    add_gain = support.GraphMixerAppTests.add_gain
    control = support.GraphMixerAppTests.control

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
