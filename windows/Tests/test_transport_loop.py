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

    def test_timeline_pipe_contract_occurrence_guards_and_purity(self):
        self.assertIn('pattern.timeline.get', self.read('api.describe')['reads'])
        self.write('order.edit', order=0, operation='after', pattern=0)
        before, context = self.doc(), self.read('context.get')
        transport = self.read('transport.get')
        first = self.read('pattern.timeline.get', pattern=0)
        second = self.read('pattern.timeline.get', pattern=0, order=1)
        self.assertEqual(first['order'], 0)
        self.assertEqual(second['order'], 1)
        self.assertEqual(len(first['positions']), len(second['positions']))
        self.assertEqual(first['positions'][0]['songSeconds'], 0)
        self.assertGreater(second['positions'][0]['songSeconds'], 0)
        self.assertEqual(second['positions'][0]['patternSeconds'], 0)
        for params in ({}, {'pattern': True}, {'pattern': .5}, {'pattern': 65535},
                       {'pattern': 0, 'order': True}, {'pattern': 0, 'order': -1},
                       {'pattern': 0, 'order': 4294967295}, {'pattern': 0, 'order': None},
                       {'pattern': 0, 'unknown': True}):
            with self.assertRaises(ApiError) as caught:
                self.read('pattern.timeline.get', **params)
            self.assertEqual(caught.exception.code, -32602)
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.read('context.get'), context)
        # Presentation counters advance while read requests are serviced.
        self.assertEqual({k: v for k, v in self.read('transport.get').items() if k != 'presentation'},
                         {k: v for k, v in transport.items() if k != 'presentation'})

        # Native view state has no song/context revision guard and is not history.
        viewport = self.read('workspace.get')['viewport']
        result = self.client.call('workspace.ruler', {'mode': 'songTime'})
        self.assertEqual(result['data'], {'mode': 'songTime'})
        self.assertFalse(result['changed'])
        self.assertFalse(result['playbackStopped'])
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            ruler = self.read('workspace.get')['ruler']
            if ruler['ready']:
                break
            time.sleep(.03)
        else:
            self.fail('Native time ruler did not complete its worker query')
        self.assertEqual(ruler['mode'], 'songTime')
        self.assertEqual(ruler['gutterWidth'], 104)
        self.assertFalse(ruler['error'])
        window = self.desktop.hwnd(self.pid)
        control = private_desktop.user.GetDlgItem(window, 590)
        self.assertTrue(control)
        self.assertTrue(private_desktop.user.IsWindowVisible(control))
        self.desktop.send(window, 0x111, 590, control)
        self.assertEqual(self.read('workspace.get')['positionMode'], 'rows')
        self.assertEqual(self.read('workspace.get')['geometry']['pattern']['gutterWidth'], 38)
        for params in ({}, {'mode': True}, {'mode': 'seconds'}, {'mode': 'rows', 'extra': True}):
            with self.assertRaises(ApiError) as caught:
                self.client.call('workspace.ruler', params)
            self.assertEqual(caught.exception.code, -32602)
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.read('context.get'), context)
        self.assertEqual(self.read('workspace.get')['viewport'], viewport)

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

    def start_live(self, name):
        path = self.folder / f'{name}.screamseq'
        self.write('document.save', path=str(path))
        self.pid = self.desktop.launch([os.environ['SCREAMSEQ_TEST_EXE'], '--audio-test-silent', '--automation',
                                       '--audio-test-allow-stop', '--seconds', '60', '--project', str(path),
                                       '--recovery-test-directory', str(self.folder / f'{name}-recovery')])
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
            self.fail('Owned transport fixture did not start WASAPI; an endpoint is required')

    def test_native_playback_commands_capture_occurrence_range_and_detached_cursor(self):
        self.write('order.edit', order=0, operation='after', pattern=0)
        self.start_live('native-regions')
        # The engine query runs on the document worker, independently of WASAPI.
        live_before = self.read('transport.get')
        document_before, context_before = self.doc(), self.read('context.get')
        self.assertEqual(self.read('pattern.timeline.get', pattern=0, order=1)['order'], 1)
        live_after = self.read('transport.get')
        self.assertTrue(live_after['audioActive'])
        self.assertFalse(live_after['fault'])
        self.assertEqual(live_after['region'], live_before['region'])
        self.assertEqual(live_after['playbackEpoch'], live_before['playbackEpoch'])
        self.assertEqual(live_after['recordingClock']['generation'], live_before['recordingClock']['generation'])
        self.assertGreaterEqual(live_after['frames'], live_before['frames'])
        self.assertEqual(self.doc(), document_before)
        self.assertEqual(self.read('context.get'), context_before)
        window = self.desktop.hwnd(self.pid)
        self.desktop.send(window, 0x111, 105)  # Pattern focus layout.
        order = private_desktop.user.GetDlgItem(window, 131)
        self.assertTrue(order)
        self.desktop.send(order, 0x14E, 1)  # Select the second occurrence of P0.
        self.desktop.send(window, 0x111, 131 | (1 << 16), order)
        context = self.read('context.get')
        self.client.call('context.set', dict(expectedRevision=self.doc()['revision'], expectedContext=context['contextRevision'],
                                             pattern=0, row=16, channel=0, column=0, following=False))
        self.write('transport.loop', enabled=True)
        rows = self.read('pattern.get', pattern=0, rowCount=1, channelCount=1)['rows']

        def play(identifier, expected):
            document, context = self.doc(), self.read('context.get')
            viewport = self.read('workspace.get')['viewport']
            self.desktop.send(window, 0x111, identifier)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                current = self.read('transport.get')
                if current['playing'] and current['region'] == expected:
                    break
                time.sleep(.02)
            self.assertTrue(current['playing'])
            self.assertEqual(current['region'], expected)
            self.assertTrue(current['audioActive'])
            self.assertFalse(current['fault'])
            self.assertEqual(self.doc(), document)
            self.assertEqual(self.read('context.get'), context)
            self.assertEqual(self.read('workspace.get')['viewport'], viewport)

        play(587, dict(order=1, cursorRow=16, loop=True))
        play(101, dict(order=1, cursorRow=0, loop=True))
        self.desktop.send(window, 0x111, 544)  # Select All keeps row 16 in place.
        play(588, dict(order=1, pattern=0, startRow=0, endRow=rows, cursorRow=0, loop=True))
        play(589, dict(order=1, pattern=0, startRow=0, endRow=rows, cursorRow=16, loop=True))
        self.write('transport.stop')
        context = self.read('context.get')
        self.client.call('context.set', dict(expectedRevision=self.doc()['revision'], expectedContext=context['contextRevision'], row=8))
        state = self.read('workspace.get')
        grid, scale = state['geometry']['pattern'], state['dpi'] / 96
        x = round((grid['x'] + 44) * scale)
        y = round((grid['y'] + grid['headerHeight'] + (23 - state['viewport']['firstRow']) * 18 + 5) * scale)
        self.assertLess(y, (grid['y'] + grid['height']) * scale)
        self.desktop.send(window, 0x201, 1 | 4, x | (y << 16))  # Shift-click row 23.
        self.desktop.send(window, 0x202, 4, x | (y << 16))
        self.assertEqual(self.read('context.get')['selection'], dict(startRow=8, endRow=23, startChannel=0, endChannel=0))
        play(588, dict(order=1, pattern=0, startRow=8, endRow=24, cursorRow=8, loop=True))
        play(589, dict(order=1, pattern=0, startRow=8, endRow=24, cursorRow=23, loop=True))
        self.write('transport.stop')

    def test_live_loop_preserves_region_device_cursor_and_recording_take(self):
        self.start_live('loop-region')
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
