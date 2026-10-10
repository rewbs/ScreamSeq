"""Owned application loop controls; live case requires an actual WASAPI endpoint."""
import os
import time
import unittest

import private_desktop
import test_graph_mixer_app as support
from client import Client, ApiError, TransportError


class TransportLoopTests(unittest.TestCase):
    setUp = support.GraphMixerAppTests.setUp
    doc = support.GraphMixerAppTests.doc
    read = support.GraphMixerAppTests.read
    write = support.GraphMixerAppTests.write

    def test_stopped_loop_guards_native_toggle_replay_and_history(self):
        description = self.read('api.describe')
        self.assertIn('transport.loop', description['writes'])
        self.assertEqual(description['revisionGuards']['transport.loop'], ['expectedRevision'])
        before, context = self.doc(), self.read('context.get')
        transport = self.read('transport.get')
        for params, code in (({'enabled': False}, -32602),
                             ({'enabled': False, 'expectedRevision': 'stale'}, -32001),
                             ({'enabled': 1, 'expectedRevision': before['revision']}, -32602),
                             ({'expectedRevision': before['revision']}, -32602),
                             ({'enabled': False, 'extra': True, 'expectedRevision': before['revision']}, -32602)):
            with self.assertRaises(ApiError) as caught:
                self.client.call('transport.loop', params)
            self.assertEqual(caught.exception.code, code)
            self.assertEqual(self.read('transport.get')['loop'], transport['loop'])
        params = dict(enabled=False, expectedRevision=before['revision'])
        result = self.client.call('transport.loop', params, request_id='owned-loop-once')
        self.assertEqual(result['data'], {'loop': False})
        self.assertFalse(result['changed'])
        self.assertFalse(result['playbackStopped'])
        self.assertEqual(self.client.call('transport.loop', params, request_id='owned-loop-once'), result)
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.read('context.get'), context)
        commands = self.read('workspace.commands.get')['commands']
        command = next(c for c in commands if c['name'] == 'Playback / Toggle region or song loop')
        identifier = int(command['id'].rsplit('.', 1)[1])
        window = self.desktop.hwnd(self.pid)
        control = private_desktop.user.GetDlgItem(window, identifier)
        self.assertTrue(control)
        self.assertTrue(private_desktop.user.IsWindowVisible(control))
        self.assertTrue(private_desktop.user.IsWindowEnabled(control))
        self.desktop.send(window, 0x111, identifier, control)
        self.assertTrue(self.read('transport.get')['loop'])
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.read('context.get'), context)

    def test_live_loop_preserves_region_device_cursor_and_recording_take(self):
        path = self.folder / 'loop-region.screamseq'
        self.write('document.save', path=str(path))
        self.pid = self.desktop.launch([os.environ['SCREAMSEQ_TEST_EXE'], '--audio-test-silent', '--automation',
                                       '--audio-test-allow-stop', '--seconds', '30', '--project', str(path),
                                       '--recovery-test-directory', str(self.folder / 'LoopRecovery')])
        self.client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(self.pid), timeout=20)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            try:
                if self.read('transport.get')['audioActive'] and not self.read('workspace.get')['documentBusy']:
                    break
            except TransportError:
                pass
            time.sleep(.03)
        else:
            self.fail('Owned loop fixture did not start WASAPI; an endpoint is required')
        context = self.read('context.get')
        self.client.call('context.set', dict(expectedRevision=self.doc()['revision'], expectedContext=context['contextRevision'],
                                             pattern=0, row=16, following=False))
        region = dict(pattern=0, startRow=8, endRow=24, cursorRow=8, loop=True)
        self.write('transport.play', **region)
        self.write('recording.start', channels=[0], instrument=1)
        take = self.read('recording.get')
        before = self.doc()
        context = self.read('context.get')
        initial = self.read('transport.get')
        self.assertTrue(initial['playing'])
        self.assertEqual(initial['region'], region)
        for enabled in (False, True, False, True):
            result = self.client.call('transport.loop', dict(enabled=enabled, expectedRevision=before['revision']))
            self.assertFalse(result['changed'])
            self.assertFalse(result['playbackStopped'])
            current = self.read('transport.get')
            self.assertEqual(current['loop'], enabled)
            self.assertEqual(current['region'], region, 'Loop state must not replace the captured playback region')
            self.assertEqual(current['playbackEpoch'], initial['playbackEpoch'])
            self.assertEqual(current['recordingClock']['generation'], initial['recordingClock']['generation'])
            self.assertTrue(current['audioActive'])
            self.assertFalse(current['fault'])
            retained = self.read('recording.get')
            for key in ('take', 'capturing', 'eventCount', 'baseRevision'):
                self.assertEqual(retained[key], take[key], key)
            now = self.read('context.get')
            for key in ('pattern', 'row', 'channel', 'column', 'contextRevision', 'selection', 'following'):
                self.assertEqual(now[key], context[key], key)
            self.assertEqual(self.doc(), before)
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            current = self.read('transport.get')
            if current['frames'] > initial['frames'] + 4096:
                break
            time.sleep(.02)
        self.assertGreater(current['frames'], initial['frames'] + 4096)
        self.assertEqual(current['playbackEpoch'], initial['playbackEpoch'])
        self.assertGreaterEqual(current['row'], 8)
        self.assertLess(current['row'], 24)
        self.write('recording.stop', take=take['take'])
        self.write('recording.discard', take=take['take'])
        self.write('transport.stop')
        self.assertTrue(self.read('transport.get')['loop'], 'Stop must retain the loop setting')


if __name__ == '__main__':
    unittest.main()
