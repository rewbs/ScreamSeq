"""Plugin recording through the real PID pipe and native controls on an owned desktop."""
import ctypes
import os
import plistlib
from pathlib import Path
import sys
import time
import unittest
from ctypes import wintypes

import private_desktop
import test_song_routing as routing
from client import ApiError

private_desktop.user.IsWindowVisible.argtypes = [wintypes.HWND]
private_desktop.user.IsWindowVisible.restype = wintypes.BOOL


class PluginRecordingTests(unittest.TestCase):
    # Helpers only: importing this fixture does not inherit its test inventory.
    setUp = routing.SongRoutingTests.setUp
    launch = routing.SongRoutingTests.launch
    doc = routing.SongRoutingTests.doc
    read = routing.SongRoutingTests.read
    write = routing.SongRoutingTests.write
    idle = routing.SongRoutingTests.idle
    local = routing.SongRoutingTests.local
    main_command = routing.SongRoutingTests.main_command
    window = routing.SongRoutingTests.window
    add_gain = routing.SongRoutingTests.add_gain

    def wait_for(self, read, predicate):
        deadline = time.monotonic() + 8
        while True:
            state = read()
            if predicate(state):
                return state
            if time.monotonic() >= deadline:
                self.fail(str(state))
            time.sleep(.02)

    def take(self):
        return self.read('automation.recording.get')

    def recording_button(self, identifier):
        self.idle()
        owner = self.window('ScreamSeq.ParameterRecording')
        child = private_desktop.user.GetDlgItem(owner, identifier)
        self.assertTrue(child)
        self.desktop.send(owner, 0x111, identifier, child)
        self.idle()

    def test_discoverable_windows_and_strict_recovery_reads(self):
        before = self.doc()
        layout = self.read('workspace.get')['editorDock']['configuration']
        description = self.read('api.describe')
        for method in ('automation.recording.get', 'automation.recording.events',
                       'automation.recovery.list', 'automation.recovery.get'):
            self.assertIn(method, description['reads'])
        for verb in ('start', 'capture', 'stop', 'commit', 'discard'):
            self.assertIn('automation.recording.' + verb, description['writes'])
        self.main_command(661)
        self.wait_for(lambda: self.read('workspace.get'),
                      lambda s: s['parameterRecording']['visible'] and not s['pendingViewCommands'])
        self.main_command(662)
        review = self.wait_for(lambda: self.read('workspace.get'),
                              lambda s: s['pluginRecoveryReview']['visible'] and not s['pendingViewCommands'])['pluginRecoveryReview']
        self.assertTrue(review['visible'])
        self.assertEqual(review['archiveTotal'], 0)
        self.assertEqual(self.read('workspace.get')['editorDock']['configuration'], layout)
        self.assertFalse(self.take()['retained'])
        for fields in (dict(limit=True), dict(offset=-1), dict(limit=129), dict(unexpected=1)):
            with self.subTest(fields=fields), self.assertRaises(ApiError) as raised:
                self.read('automation.recovery.list', **fields)
            self.assertEqual(raised.exception.code, -32602)
        for revision in ('', 'x' * 201, 'x\0y'):
            with self.subTest(revision=revision), self.assertRaises(ApiError) as raised:
                self.read('automation.recovery.get', expectedRevision=revision, kind='nativeControls', index=0)
            self.assertEqual(raised.exception.code, -32602)
        with self.assertRaises(ApiError) as raised:
            self.read('automation.recovery.get', expectedRevision='stale', kind='nativeControls', index=0)
        self.assertEqual(raised.exception.code, -32001)
        self.assertEqual(self.doc(), before)

    def test_recovered_inputs_remain_detached_and_exact_through_public_review(self):
        path = self.folder / 'archived-input.screamseq'
        self.write('document.save', path=str(path))
        project = plistlib.loads(path.read_bytes())
        original = dict(version=1, reviewOnly=True, document='original-document', revision='original-revision',
                        take='previous-process-take', accepted='9007199254740993', lost='2', previewError='',
                        events=[dict(plugin='removed-plugin', parameter=17, value=.123456789012345,
                                     hostTime='9007199254740995', state='unknown', error='Original delivery uncertain')])
        archives = [dict(original, version=2), original]
        project['screamseqPluginControlRecovery'] = archives
        path.write_bytes(plistlib.dumps(project, fmt=plistlib.FMT_BINARY))
        source = path.read_bytes()
        self.write('document.open', path=str(path), discard=True)
        before = self.doc()
        catalogue = self.read('automation.recovery.list')
        self.assertEqual(catalogue['total'], 2)
        self.assertEqual([e['headerSupported'] for e in catalogue['entries']], [False, True])
        with self.assertRaises(ApiError) as raised:
            self.read('automation.recovery.get', expectedRevision=before['revision'], kind='nativeControls', index=0)
        self.assertEqual(raised.exception.code, -32602)
        page = self.read('automation.recovery.get', expectedRevision=before['revision'], kind='nativeControls', index=1)
        self.assertEqual(page['summary']['accepted'], original['accepted'])
        self.assertEqual({k: page['events'][0][k] for k in original['events'][0]}, original['events'][0])
        self.main_command(662)
        owner = self.window('ScreamSeq.PluginRecoveryReview')
        selector = private_desktop.user.GetDlgItem(owner, 9781)
        self.desktop.send(selector, 0x14E, 1)
        self.desktop.send(owner, 0x111, 9781 | (1 << 16), selector)
        review = self.read('workspace.get')['pluginRecoveryReview']
        self.assertEqual(review['events'], page['events'])
        self.assertFalse(self.take()['retained'], 'Imported input must not arm a current-clock take')
        self.assertEqual(self.doc(), before)
        saved = self.folder / 'reviewed-copy.screamseq'
        self.write('document.save', path=str(saved))
        self.assertEqual(plistlib.loads(saved.read_bytes())['screamseqPluginControlRecovery'], archives)
        self.assertEqual(path.read_bytes(), source, 'Review must not overwrite the recovered source')

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO') == '1', 'Explicit owned silent WASAPI qualification')
    def test_native_record_keep_history_reopen_and_discard(self):
        plugin = self.add_gain()
        project = self.folder / 'recording-source.screamseq'
        self.write('document.save', path=str(project))
        self.record_keep_history_reopen_and_discard(plugin, project)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO') == '1', 'Explicit owned silent WASAPI qualification')
    def test_sustained_native_recording_keeps_clock_and_history(self):
        plugin = self.add_gain()
        project = self.folder / 'sustained-recording-source.screamseq'
        self.write('document.save', path=str(project))
        # The foreground failure occurred after the short lifecycle checks had
        # already passed. Retain real input on both sides of a 95-second hold.
        self.record_keep_history_reopen_and_discard(plugin, project, sustain_seconds=95)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO') == '1', 'Explicit owned silent WASAPI qualification')
    def test_composed_fixture_records_without_losing_cross_domain_edits(self):
        # Reuse the strict six-domain typed-tree journey rather than substituting
        # a blank song or checking only the final automation count.
        sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'editor/Tests/Conformance'))
        from composing_history import run
        from project_tree import differences, read_project, digest
        from native_roundtrip import write_json
        evidence = Path(os.environ.get('SCREAMSEQ_COMPOSING_EVIDENCE_DIR', self.folder / 'composing-evidence'))
        report = run(self.client, evidence)
        self.assertTrue(report['passed'])
        project = evidence / report['exchangeInput']
        before = read_project(project)
        plugin = before['plugins'][0]['instanceID']
        # F04 intentionally has a Gain envelope. Exercise that refusal first,
        # then record Balance without disabling or replacing existing music.
        self.record_keep_history_reopen_and_discard(plugin, project, parameter=2, conflicting_parameter=1)
        saved = evidence / 'recorded-composing-journey.screamseq'
        self.write('document.save', path=str(saved))
        after = read_project(saved)
        expected = dict(before, automation=after['automation'])
        comparison = differences(expected, after)
        self.assertTrue(comparison['equal'], comparison)
        self.assertNotEqual(before['automation'], after['automation'])
        write_json(evidence / 'recording-journey.json', dict(
            passed=True, parityComplete=False, silentNativePlayback=True,
            foregroundVisualQualified=False, source=project.name, saved=saved.name,
            beforeTypedSHA256=digest(before), afterTypedSHA256=digest(after),
            onlyRecordedAutomationChanged=comparison,
            checks=['six-domain composing history', 'occupied Gain refusal preserves music', 'native Balance capture',
                    'Stop preserves playback', 'Keep is one Undo', 'Redo',
                    'Discard preserves preceding music', 'save/reopen',
                    'exact unrelated project preservation']))

    def record_keep_history_reopen_and_discard(self, plugin, project, parameter=1, conflicting_parameter=None, sustain_seconds=0):
        self.launch(project, seconds=180 if sustain_seconds else 120)
        self.assertTrue(self.read('transport.get')['playing'])
        baseline = self.read('plugin.state.get', plugin=plugin)
        points_before = self.read('automation.get')
        revision = self.doc()['revision']
        self.main_command(661)
        self.wait_for(lambda: self.read('workspace.get'),
                      lambda s: s['parameterRecording']['visible'] and not s['pendingViewCommands'])
        self.recording_button(9721)
        take = self.wait_for(self.take, lambda s: s['retained'] and s['capturing'])
        self.assertEqual(take['clock'], 'windows-qpc-100ns')
        self.assertGreater(int(take['hostTime']), int(take['startTime']))
        self.assertIsNone(take['endTime'])
        # Open the standard rack parameter window, then edit its real value field.
        self.main_command(316)
        main = self.desktop.hwnd(self.pid)
        rack = private_desktop.user.GetDlgItem(main, 300)
        slot = next(i for i, p in enumerate(self.doc()['data']['nativePlugins']) if p['instanceID'] == plugin)
        self.desktop.send(rack, 0x186, slot)
        self.desktop.send(main, 0x111, 300 | (1 << 16), rack)
        self.main_command(304)
        parameters = self.window('ScreamSeq.PluginParameters')
        def enter(identifier, value):
            self.idle()
            editor = next(p for p in self.read('workspace.get')['graphPluginParameters'] if p['plugin'] == plugin)
            row = next(r for r in editor['rows'] if r['id'] == identifier)
            field = private_desktop.user.GetDlgItem(parameters, row['valueControl'])
            self.assertTrue(field)
            self.assertTrue(private_desktop.user.IsWindowVisible(field))
            text = ctypes.create_unicode_buffer(value)
            self.desktop.send(field, 0xC, 0, ctypes.addressof(text))

        if conflicting_parameter is not None:
            before_refusal = self.doc()
            enter(conflicting_parameter, '-6.5')
            review = self.wait_for(lambda: self.read('workspace.get')['parameterRecording'],
                                   lambda s: s['delivery']['needsReview'])
            refused = review['delivery']['events']
            self.assertEqual(len(refused), 1)
            self.assertEqual({k: refused[0][k] for k in ('plugin', 'parameter', 'value', 'state')},
                             dict(plugin=plugin, parameter=conflicting_parameter, value=-6.5, state='refused'))
            self.assertIn('envelopes', refused[0]['error'])
            self.wait_for(lambda: next(p for p in self.read('workspace.get')['graphPluginParameters'] if p['plugin'] == plugin),
                          lambda p: 'envelopes' in p['status'])
            self.assertEqual(self.take()['eventCount'], 0)
            self.assertEqual(self.doc(), before_refusal)
            self.assertEqual(self.read('plugin.state.get', plugin=plugin), baseline)
            self.assertEqual(self.read('automation.get'), points_before)
            self.recording_button(9722)
            self.recording_button(9724)
            self.assertFalse(self.take()['retained'])
            self.idle()
            self.recording_button(9721)
            take = self.wait_for(self.take, lambda s: s['retained'] and s['capturing'])

        enter(parameter, '-6.5')
        try:
            captured = self.wait_for(self.take, lambda s: s['eventCount'] >= 1)
        except AssertionError as failure:
            # Preserve the native target/draft/admission state, not only the
            # worker's empty event list, when a composing journey finds a gap.
            self.fail(str(dict(failure=str(failure), expectedPlugin=plugin,
                               recording=self.read('workspace.get')['parameterRecording'])))
        if sustain_seconds:
            until = time.monotonic() + sustain_seconds
            generation = self.read('transport.get')['recordingClock']['generation']
            initial_count = captured['eventCount']
            while time.monotonic() < until:
                clock = self.read('transport.get')['recordingClock']
                current = self.take()
                self.assertEqual(clock['generation'], generation, clock)
                self.assertEqual(clock['frameTimeline']['generation'], generation, clock)
                self.assertEqual(clock['frameTimeline']['failure'], 'none', clock)
                self.assertTrue(current['capturing'] and current['compatible'], current)
                self.assertEqual(current['eventCount'], initial_count, current)
                self.assertNotIn('intakeError', current)
                time.sleep(.5)
            enter(parameter, '-3.25')
            self.wait_for(self.take, lambda s: s['eventCount'] > initial_count)
        self.assertEqual(self.doc()['revision'], revision, 'Recording input must not add manual edit history')
        self.assertEqual(self.read('plugin.state.get', plugin=plugin), baseline)
        events = self.read('automation.recording.events', take=take['take'], plugin=plugin, parameter=parameter)
        self.assertTrue(any(e['value'] == -6.5 for e in events['events']))
        if sustain_seconds:
            self.assertTrue(any(e['value'] == -3.25 for e in events['events']))
        self.assertTrue(all(int(e['timestamp']) >= int(take['startTime']) for e in events['events']))
        self.recording_button(9722)
        stopped = self.wait_for(self.take, lambda s: s['retained'] and not s['capturing'])
        self.assertIsNotNone(stopped['endTime'])
        self.assertNotEqual(self.read('workspace.get')['status'], 'Recording original parameter input / Stop the take to review')
        self.assertTrue(self.read('transport.get')['playing'])
        self.recording_button(9723)
        self.wait_for(self.take, lambda s: not s['retained'])
        self.assertNotIn('Stop the take to review', self.read('workspace.get')['status'])
        points = self.read('automation.get')
        self.assertGreater(points['total'], points_before['total'])
        self.assertEqual(self.read('plugin.state.get', plugin=plugin), baseline)
        self.write('history.undo')
        self.assertEqual(self.read('automation.get'), points_before, 'Keep must be exactly one Undo step')
        self.write('history.redo')
        self.assertEqual(self.read('automation.get'), points)
        saved = self.folder / 'recorded.screamseq'
        self.write('document.save', path=str(saved))
        # Discard a second take and retain the first take's committed music/history.
        state_before = self.doc()
        second = self.write('automation.recording.start', plugins=[plugin])
        timestamp = self.take()['hostTime']
        params = dict(expectedRevision=self.doc()['revision'], take=second['take'], plugin=plugin,
                      parameter=parameter, value=-12.25, timestamp=timestamp)
        self.idle()
        accepted = self.client.call('automation.recording.capture', params, request_id='original-control-once')
        self.assertTrue(accepted['data']['accepted'])
        self.assertEqual(self.client.call('automation.recording.capture', params, request_id='original-control-once'), accepted)
        self.assertEqual(self.read('automation.recording.events', take=second['take'], plugin=plugin, parameter=parameter)['total'], 1)
        self.write('automation.recording.stop', take=second['take'])
        # A hidden review has no poll timer. Its departure summary must follow
        # the confirmed API Discard without needing to reopen/refresh it.
        self.recording_button(9725)
        self.assertEqual(self.read('workspace.get')['parameterRecording']['take']['take'], second['take'])
        self.recording_button(9727)
        self.write('automation.recording.discard', take=second['take'])
        self.assertFalse(self.take()['retained'])
        review = self.read('workspace.get')['parameterRecording']
        self.assertFalse(review['visible'])
        self.assertFalse(review['take']['retained'])
        self.assertIsNone(review['completion'])
        self.assertEqual(self.doc(), state_before)
        self.assertEqual(self.read('automation.get'), points)
        self.assertEqual(self.read('plugin.state.get', plugin=plugin), baseline)
        self.write('document.open', path=str(saved), discard=True)
        self.assertFalse(self.take()['retained'])
        self.assertEqual(self.read('automation.get'), points)
        self.assertEqual(self.read('plugin.state.get', plugin=plugin), baseline)
