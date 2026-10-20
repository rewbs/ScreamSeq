"""Actual MIDI/recording application path on owned never-switched desktops."""
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest

import private_desktop
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Api'))
from client import Client, ApiError, TransportError


class RecordingTests(unittest.TestCase):
    def setUp(self):
        self.folder = Path(self.enterContext(tempfile.TemporaryDirectory(prefix='recording-app-', dir=os.environ['TMPDIR'])))
        self.desktop = self.enterContext(private_desktop.PrivateDesktop())
        self.pid, self.client = self.launch()

    def launch(self, audio=False, midi=True):
        args = [os.environ['SCREAMSEQ_TEST_EXE'], '--audio-test-silent' if audio else '--inspection', '--automation', '--seconds', '120',
                '--recovery-test-directory', str(self.folder / ('LiveRecovery' if audio else 'Recovery'))]
        if midi: args += ['--midi-test-input']
        if audio: args += ['--audio-test-allow-stop']
        pid = self.desktop.launch(args)
        client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(pid), timeout=20)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            try:
                client.call('document.get')
                if not client.call('workspace.get')['data']['documentBusy'] and (not audio or client.call('transport.get')['data']['recordingClock']['valid']):
                    return pid, client
            except TransportError: pass
            time.sleep(.03)
        self.fail('recording fixture did not reach a valid playback clock')

    def doc(self): return self.client.call('document.get')
    def read(self, method, **params): return self.client.call(method, params)['data']
    def write(self, method, **params): return self.client.call(method, dict(expectedRevision=self.doc()['revision'], **params))
    def take(self): return self.read('recording.get')
    def settings(self): return self.read('midi.settings.get')
    @staticmethod
    def stable(value): return {k: v for k, v in value.items() if k != 'hostTime'}
    def reject(self, code, method, **params):
        with self.assertRaises(ApiError) as raised: self.client.call(method, params)
        self.assertEqual(raised.exception.code, code, str(raised.exception))
    def wait(self, condition):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            result = condition()
            if result: return result
            time.sleep(.03)
        self.fail('recording state did not settle')
    def wait_recording(self, condition):
        # Inject replies acknowledge the input queue. The MIDI service captures
        # it on the worker and may pump this read while that worker is busy.
        # Retry only this read's explicit busy response, never an input/write.
        deadline = time.monotonic() + 8; observations = []; busy_responses = 0
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            if remaining <= 0: break
            try:
                result = Client(self.client.pipe, timeout=min(.5, remaining)).call('recording.get')
            except ApiError as error:
                if error.code != -32002: raise
                busy_responses += 1
                observations.append(dict(readError=str(error), code=error.code)); observations = observations[-16:]
            else:
                take = result['data']
                observations.append(dict(revision=result.get('revision'), take=take.get('take'),
                                         capturing=take.get('capturing'), eventCount=take.get('eventCount'),
                                         compatible=take.get('compatible'), missingTime=take.get('missingTime'),
                                         overflow=take.get('overflow'), inputError=take.get('inputError')))
                observations = observations[-16:]
                if condition(take): return take
            remaining = deadline - time.monotonic()
            if remaining > 0: time.sleep(min(.03, remaining))
        self.fail(f'Recording read did not converge within 8 seconds; busy responses={busy_responses}: {observations}')
    def configure(self, **patch):
        old = self.settings()
        params = {k: old[k] for k in ('source', 'armed', 'channelsCount', 'quantization', 'latencyMS')}
        params.update(expectedMidiRevision=old['revision']); params.update(patch)
        return self.client.call('midi.settings.set', params)
    def connect(self, armed=False, columns=1):
        sources = self.read('midi.devices.get')['devices']
        self.assertEqual(len(sources), 1)
        self.configure(source=sources[0]['id'], armed=armed, channelsCount=columns)
        self.assertTrue(self.settings()['connected'])
    def live(self, columns=1):
        self.pid, self.client = self.launch(audio=True)
        self.connect(armed=True, columns=columns)
        self.assertTrue(self.take()['capturing'])
        time.sleep(.08)
    def inject(self, packed, **extra):
        settings = self.settings()
        milliseconds = (int(settings['hostTime']) - int(settings['qualificationInput']['anchorHostTime'])) // 10000
        return self.client.call('midi.test.inject', dict(expectedMidiRevision=settings['revision'], generation=settings['qualificationInput']['generation'],
            events=[dict(packed=value, milliseconds=milliseconds) if isinstance(value, int) else value for value in packed], **extra))
    def command(self, identifier): self.desktop.send(self.desktop.hwnd(self.pid), 0x111, identifier, 0)
    def musical(self):
        return self.read('pattern.notes.get', pattern=0), self.read('pattern.get', pattern=0, startRow=0, rowCount=64)

    def test_discovery_settings_guards_replay_noop_and_atomic_invalid_batch(self):
        before = self.doc(); desc = self.read('api.describe')
        self.assertIn('recording.get', desc['reads'])
        for method in ('start', 'capture', 'stop', 'commit', 'discard'): self.assertIn('recording.' + method, desc['writes'])
        self.assertNotIn('recording.loss', desc['writes'])
        self.assertEqual(desc['revisionGuards']['midi.settings.set'], ['expectedMidiRevision'])
        self.assertEqual(desc['revisionGuards']['midi.test.inject'], ['expectedMidiRevision', 'generation'])
        original = self.settings(); source = self.read('midi.devices.get')['devices'][0]['id']
        base = dict(expectedMidiRevision=original['revision'], source=source, armed=False, channelsCount=2, quantization=16384, latencyMS=-4.5)
        for patch in ({'channelsCount': True}, {'channelsCount': 0}, {'quantization': 65537}, {'quantization': 1.5},
                      {'latencyMS': 501}, {'source': 'x\0y'}, {'source': 'missing'}, {'armed': 1}, {'surprise': 1}):
            self.reject(-32602, 'midi.settings.set', **dict(base, **patch))
        self.reject(-32001, 'midi.settings.set', **dict(base, expectedMidiRevision='old'))
        dry = self.client.call('midi.settings.set', dict(base, dryRun=True))
        self.assertEqual(dry['revision'], original['revision']); self.assertFalse(self.settings()['connected'])
        applied = self.client.call('midi.settings.set', base, request_id='owned-midi-preference')
        self.assertEqual(applied, self.client.call('midi.settings.set', base, request_id='owned-midi-preference'))
        self.assertFalse(applied['changed']); self.assertFalse(applied['playbackStopped'])
        self.assertEqual(self.configure()['revision'], applied['revision']); self.assertEqual(before, self.doc())
        state = self.settings(); raw = dict(expectedMidiRevision=state['revision'], generation=state['qualificationInput']['generation'])
        self.reject(-32602, 'midi.test.inject', **raw, events=[dict(packed=0x643c90, milliseconds=0), dict(packed=-1, milliseconds=0)])
        self.assertEqual(self.settings()['lost'], 0); self.assertEqual(before, self.doc())
        _, normal = self.launch(midi=False)
        self.assertNotIn('midi.test.inject', normal.call('api.describe')['data']['writes'])

    def test_api_take_revision_batch_validation_stale_retention_and_save_open_guards(self):
        saved = self.folder / 'Original.screamseq'; self.write('document.save', path=str(saved))
        before = self.doc()
        params = dict(expectedRevision=before['revision'], channels=[0, 1], instrument=1)
        started = self.client.call('recording.start', params, request_id='start-once')
        self.assertEqual(started, self.client.call('recording.start', params, request_id='start-once'))
        take = self.take()['take']; self.assertEqual(before['revision'], self.doc()['revision'])
        baseline = self.stable(self.take())
        self.reject(-32602, 'recording.capture', expectedRevision=before['revision'], take=take,
                    events=[dict(timestamp='1', status=0x90, note=60, velocity=100), dict(timestamp='2', status=0x90, note=61)])
        self.assertEqual(baseline, self.stable(self.take()))
        self.reject(-32602, 'document.save', expectedRevision=before['revision'], path=str(self.folder/'Blocked.screamseq'))
        self.reject(-32602, 'document.open', expectedRevision=before['revision'], path=str(saved), discard=True)
        self.assertFalse((self.folder/'Blocked.screamseq').exists())
        patched = self.write('document.patch', title='Changed while take retained')
        # The edit stops transport; when its worker is busy, capture Stop is
        # deferred to the UI service. Observe that postcondition before making
        # the independent explicit Stop request below. Never retry a mutation.
        deadline = time.monotonic() + 8; observations = []
        while time.monotonic() < deadline:
            try:
                observed = Client(self.client.pipe, timeout=min(.5, max(.001, deadline-time.monotonic()))).call('workspace.get')
                workspace = observed['data']; current = workspace['recording']
                observations.append(dict(revision=observed['revision'], documentBusy=workspace['documentBusy'], recording=current))
                observations = observations[-16:]
                self.assertEqual(observed['revision'], patched['revision'])
                self.assertEqual(current['take'], take)
                self.assertEqual(current['baseRevision'], before['revision'])
                self.assertFalse(current['compatible'])
                if not workspace['documentBusy'] and not current['capturing']: break
            except TransportError as error:
                observations.append(dict(readError=str(error))); observations = observations[-16:]
            remaining = deadline-time.monotonic()
            if remaining > 0: time.sleep(min(.03, remaining))
        else: self.fail(f'Edit did not stop the retained take while idle within 8 seconds: {observations}')
        stopped = self.client.call('recording.get')
        self.assertEqual(stopped['revision'], patched['revision'])
        self.assertEqual(stopped['data']['take'], take); self.assertFalse(stopped['data']['capturing'])
        self.assertFalse(stopped['data']['compatible'])
        explicit = self.write('recording.stop', take=take)
        self.assertFalse(explicit['changed']); self.assertEqual(explicit['revision'], patched['revision'])
        self.assertEqual(self.stable(explicit['data']), self.stable(stopped['data']))
        self.assertEqual(self.stable(self.take()), self.stable(stopped['data']))
        self.reject(-32602, 'recording.commit', expectedRevision=self.doc()['revision'], take=take)
        self.assertEqual(self.take()['take'], take)
        self.command(120); self.assertTrue(self.read('workspace.get')['midiWindow']['visible'])
        self.write('recording.discard', take=take); self.assertFalse(self.take()['take'])
        self.write('document.open', path=str(saved), discard=True); self.assertEqual(self.doc()['data']['title'], 'Midnight Circuit')

    def test_armed_stopped_midi_steps_cursor_once_and_undo_restores_cells(self):
        self.connect(armed=True)
        context = self.client.call('context.get')
        self.client.call('context.set', dict(expectedRevision=context['revision'], expectedContext=context['data']['contextRevision'], pattern=0, row=1, channel=0, column=0, following=False))
        before = self.musical()
        self.inject([0x643c90, 0x003c80, 0x644090, 0x004080])
        self.wait(lambda: self.read('context.get')['row'] == 3)
        cells = self.read('pattern.get', pattern=0, startRow=1, rowCount=2, channelCount=1)['cells']
        self.assertEqual([cell['note'] for cell in cells], [61, 65])
        self.assertFalse(self.take()['take'])
        self.write('history.undo', domain='document'); self.assertEqual(self.musical(), before)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO') == '1', 'Explicit silent output required')
    def test_timestamped_chord_api_stop_dry_run_one_undo_reopen_and_native_review(self):
        self.live(columns=2); before = self.musical(); revision = self.doc()['revision']
        self.inject([0x643c90, 0x644090]); self.wait_recording(lambda take: take['eventCount'] == 2)
        time.sleep(.04); self.inject([0x003c80, 0x004080]); self.wait_recording(lambda take: take['eventCount'] == 4)
        self.write('transport.stop'); take = self.take()
        self.assertFalse(take['capturing']); self.assertEqual(take['missingTime'], 0)
        self.assertEqual(take['exhaustedVoices'], 0); self.assertEqual(take['overflow'], 0)
        self.assertEqual(self.doc()['revision'], revision); self.assertEqual(before, self.musical())
        ons = [e for e in take['events'] if e['note'] < 128]
        self.assertEqual({e['note'] for e in ons}, {61, 65}); self.assertEqual(len({e['track'] for e in ons}), 2)
        self.command(549)
        # Opening the window enumerates sources before its controls become ready.
        # Send one user-equivalent command only after Refresh review is enabled.
        review = private_desktop.user.GetDlgItem(self.tool(), 7214)
        self.wait(lambda: not self.read('workspace.get')['midiWindow']['pending']
                  and private_desktop.user.IsWindowEnabled(review))
        self.tool_command(7214)
        self.wait(lambda: self.read('workspace.get')['midiWindow']['reviewedCount'] == 4)
        self.assertTrue(self.write('recording.commit', take=take['take'], dryRun=True, replaceRows=True)['data']['wouldChange'])
        self.assertEqual(self.take()['take'], take['take']); self.assertEqual(before, self.musical())
        self.command(551); self.wait_recording(lambda take: not take['take']); after = self.musical()
        self.assertNotEqual(before, after)
        self.write('history.undo', domain='document'); self.assertEqual(before, self.musical())
        self.write('history.redo', domain='document'); self.assertEqual(after, self.musical())
        saved = self.folder/'Recorded.screamseq'; self.write('document.save', path=str(saved))
        self.write('document.open', path=str(saved), discard=True); self.assertEqual(after, self.musical()); self.assertFalse(self.take()['take'])

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO') == '1', 'Explicit silent output required')
    def test_stop_drains_multiple_batches_without_later_step_entry(self):
        self.live(); before = self.musical(); revision = self.doc()['revision']
        messages = [0x643c90, 0x003c80] * 1024
        result = self.inject(messages, after='apiStop'); self.assertEqual(result['data']['accepted'], 2048)
        take = self.take(); self.assertEqual(take['eventCount'], 2048, take)
        self.assertFalse(take['capturing']); self.assertEqual(take['missingTime'], 0)
        self.assertEqual(self.doc()['revision'], revision); self.command(551); self.wait(lambda: not self.take()['take'])
        after = self.doc(); time.sleep(.25); self.assertEqual(after, self.doc())
        self.write('history.undo', domain='document'); self.assertEqual(before, self.musical())
        self.write('transport.play'); time.sleep(.08)
        self.assertTrue(self.take()['capturing'])
        self.assertEqual(self.inject(messages, after='nativeStop')['data']['accepted'], 2048)
        self.assertFalse(self.take()['take']); after = self.doc(); time.sleep(.25); self.assertEqual(after, self.doc())
        self.write('history.undo', domain='document'); self.assertEqual(before, self.musical())

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO') == '1', 'Explicit silent output required')
    def test_queue_loss_retains_stopped_take_until_explicit_review_and_finish(self):
        self.live(); before = self.musical(); revision = self.doc()['revision']
        result = self.inject([0x643c90]*5000, after='nativeStop')
        self.assertLess(result['data']['accepted'], 5000)
        take = self.take(); self.assertTrue(take['take']); self.assertFalse(take['capturing'])
        self.assertGreater(take['overflow'], 0); self.assertTrue(take['inputError'])
        self.assertEqual(self.doc()['revision'], revision); self.assertEqual(before, self.musical())
        self.assertTrue(self.read('workspace.get')['midiWindow']['visible'])
        time.sleep(.2); self.assertEqual(self.take()['take'], take['take'])
        self.command(552); self.assertFalse(self.take()['take']); self.assertEqual(before, self.musical())
        self.configure(source=''); self.connect(armed=True); self.write('transport.play'); time.sleep(.08)
        self.inject([0x643c90, 0x003c80], after='apiStop')
        self.assertFalse(self.take()['inputError']); self.assertEqual(self.take()['overflow'], 0)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO') == '1', 'Explicit silent output required')
    def test_autosave_live_take_is_immutable_restore_hydrates_fresh_stopped_take(self):
        self.live(); self.inject([0x643c90])
        before = self.stable(self.wait_recording(lambda take: take['eventCount'] == 1)); revision = self.doc()['revision']
        copy = self.write('recovery.save')['data']['lastCopy']
        self.assertEqual(before, self.stable(self.take())); self.assertTrue(self.read('transport.get')['playing'])
        self.assertEqual(self.doc()['revision'], revision)
        self.write('recovery.restore', id=copy); restored = self.take()
        self.assertNotEqual(restored['take'], before['take']); self.assertFalse(restored['capturing']); self.assertTrue(restored['compatible'])
        self.assertEqual(restored['eventCount'], 2); self.assertEqual(restored['events'][0], before['events'][0]); self.assertEqual(restored['events'][1]['note'], 255)
        self.command(551); self.assertFalse(self.take()['take'])
        self.write('document.save', path=str(self.folder/'Recovered.screamseq'))

    def tool(self):
        found = []
        @private_desktop.callback
        def visit(hwnd, _):
            pid = wintypes.DWORD(); private_desktop.user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            name = ctypes.create_unicode_buffer(100); private_desktop.user.GetClassNameW(hwnd, name, 100)
            if pid.value == self.pid and name.value == 'ScreamSeq.MidiRecording': found.append(hwnd)
            return True
        private_desktop.check(private_desktop.user.EnumDesktopWindows(self.desktop.desktop, visit, 0))
        self.assertEqual(len(found), 1); return found[0]
    def tool_command(self, identifier):
        self.desktop.send(self.tool(), 0x111, identifier, private_desktop.user.GetDlgItem(self.tool(), identifier))
