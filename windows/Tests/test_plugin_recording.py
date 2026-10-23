"""Plugin recording through the real PID pipe and native controls on an owned desktop."""
import ctypes
import json
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
private_desktop.user.IsWindowEnabled.argtypes = [wintypes.HWND]
private_desktop.user.IsWindowEnabled.restype = wintypes.BOOL


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

    def trace_recording_clock(self, stage, plugin, parameter):
        """Retain original event/clock evidence before disposable sessions close.

        Diagnostics only: never resolve, acknowledge or drop an unmapped input.
        Reads are sequential snapshots, not an atomic view of the audio thread.
        """
        root = Path(os.environ.get('SCREAMSEQ_COMPOSING_EVIDENCE_DIR', self.folder / 'composing-evidence'))
        folder = root / 'recording-clock'
        folder.mkdir(parents=True, exist_ok=True)
        state = self.take()
        observation = dict(stage=stage, monotonicNS=str(time.monotonic_ns()),
                           recording=state, transport=self.read('transport.get'))
        if state['retained']:
            observation['events'] = self.read('automation.recording.events', take=state['take'],
                                               plugin=plugin, parameter=parameter, limit=16, includeTiming=True)
        with (folder / (self._testMethodName + '.jsonl')).open('a', encoding='utf-8') as output:
            output.write(json.dumps(observation, sort_keys=True) + '\n')

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
        self.assertEqual(description['pluginRecording']['timingInspection'],
                         dict(field='includeTiming', maximumPage=16, readOnly=True))
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
        self.record_keep_history_reopen_and_discard(plugin, project, parameter=2, conflicting_parameter=1, slider_sweep=True)
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
            checks=['six-domain composing history', 'occupied Gain refusal preserves music', 'native Balance capture and 25 original slider inputs',
                    'Stop preserves playback', 'Keep is one Undo', 'Redo',
                    'Discard preserves preceding music', 'save/reopen',
                    'exact unrelated project preservation']))

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO') == '1', 'Explicit owned silent WASAPI qualification')
    def test_sample_layered_vst3_composing_and_recording_journey(self):
        # This mandatory fixture path must not depend on an installed commercial
        # synth. Keep the optional Surge coverage as a separate vendor check.
        self.assertTrue(os.environ.get('SCREAMSEQ_TEST_PROVIDER_CACHE'), 'Native VST3 fixture cache is required')
        sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'editor/Tests/Conformance'))
        from composing_history import run
        from project_tree import differences, read_project, digest
        from native_roundtrip import write_json
        root = Path(os.environ.get('SCREAMSEQ_COMPOSING_EVIDENCE_DIR', self.folder / 'composing-evidence'))
        evidence = root.parent / (root.name + '-layered-instrument')
        report = run(self.client, evidence)
        self.assertTrue(report['passed'])
        descriptor = next(p for p in self.read('plugin.discover', format='VST3')
                          if p['classID'] == '5245534F4E414E43494E535452550001')
        self.assertTrue(descriptor['isInstrument'])
        self.write('plugin.add', descriptor=descriptor)
        plugin = self.doc()['data']['nativePlugins'][-1]['instanceID']
        instrument = self.read('instrument.get', instrument=1)
        self.assertTrue(any(instrument['mapping']), 'Journey needs a real sample layer')
        opaque = self.read('plugin.state.get', plugin=plugin)
        unassigned = self.read('plugin.instruments.get', plugin=plugin)
        # Assign the new synth to the existing composing voice, retaining its
        # sample map/envelopes and the preceding six-domain history.
        self.write('instrument.plugin.set', instrument=1, plugin=plugin, channel=3)
        assigned = self.read('plugin.instruments.get', plugin=plugin)
        self.assertEqual([(a['instrument'], a['channel']) for a in assigned['assignments']], [(1, 3)])
        self.assertEqual(self.read('instrument.get', instrument=1), instrument)
        self.write('history.undo')
        self.assertEqual(self.read('plugin.instruments.get', plugin=plugin), unassigned)
        self.assertEqual(self.read('instrument.get', instrument=1), instrument)
        self.write('history.redo')
        self.assertEqual(self.read('plugin.instruments.get', plugin=plugin), assigned)
        unchanged = self.doc()
        self.write('instrument.plugin.set', instrument=1, plugin=plugin, channel=3)
        self.assertEqual(self.doc(), unchanged, 'Repeated assignment must not consume history')
        group = self.write('mixer.bus.add', kind='group', name='Recorded synth layer')['bus']
        routes_before = self.read('mixer.get')['instruments']
        self.write('mixer.instrument.route', plugin=plugin, target=group, output=0)
        routed = self.read('mixer.get')['instruments']
        self.assertTrue(any(r['plugin'] == plugin and r['target'] == group and r['output'] == 0 for r in routed))
        self.write('history.undo')
        self.assertEqual(self.read('mixer.get')['instruments'], routes_before)
        self.write('history.redo')
        self.assertEqual(self.read('mixer.get')['instruments'], routed)
        self.assertEqual(self.read('plugin.state.get', plugin=plugin), opaque)
        project = evidence / 'layered-routed-source.screamseq'
        self.write('document.save', path=str(project))
        before = read_project(project)
        self.record_keep_history_reopen_and_discard(plugin, project, parameter=7, values=(.25, .75, .5))
        self.assertEqual(self.read('instrument.get', instrument=1), instrument)
        self.assertEqual(self.read('plugin.instruments.get', plugin=plugin), assigned)
        self.assertEqual(self.read('mixer.get')['instruments'], routed)
        saved = evidence / 'recorded-layered-instrument.screamseq'
        self.write('document.save', path=str(saved))
        after = read_project(saved)
        comparison = differences(dict(before, automation=after['automation']), after)
        self.assertTrue(comparison['equal'], comparison)
        self.assertNotEqual(before['automation'], after['automation'])
        from recording_render import run as render_recording
        replay = render_recording(self.client, project, saved, evidence / 'audio-replay')
        self.assertTrue(replay['passed'], replay)
        write_json(evidence / 'recording-journey.json', dict(
            passed=True, parityComplete=False, silentNativePlayback=True,
            foregroundVisualQualified=False, layeredPCMQualified=False,
            recordedReplayPCM=replay,
            plugin=plugin, classID=descriptor['classID'], instrument=1, midiChannel=3,
            source=project.name, saved=saved.name, beforeTypedSHA256=digest(before), afterTypedSHA256=digest(after),
            onlyRecordedAutomationChanged=comparison,
            checks=['six-domain composing history', 'sample-preserving VST3 assignment', 'group output route history',
                    'native VST3 parameter capture', 'Stop preserves playback', 'Keep is one Undo',
                    'Redo', 'Discard', 'opaque state and aliases retained', 'exact save/reopen preservation',
                    'recorded automation changes unsaturated PCM', 'reopened playback PCM exact',
                    'resample Undo preserves source project with exactly one allocated stable identity']))

    def record_keep_history_reopen_and_discard(self, plugin, project, parameter=1, conflicting_parameter=None, sustain_seconds=0,
                                              values=(-6.5, -3.25, -12.25), slider_sweep=False):
        first_value, later_value, discard_value = values
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
        # The rack's Editor button (304) opens a VST3 vendor editor, while
        # built-ins use it for Params. The explicit Parameter controls command
        # opens this native ten-row editor for either plugin format.
        self.main_command(335)
        self.wait_for(lambda: self.read('workspace.get'),
                      lambda s: any(p.get('visible') and p.get('plugin') == plugin
                                    for p in s['graphPluginParameters']) and not s['pendingViewCommands'])
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
            self.assertGreater(int(refused[0]['timestamp']), 0)
            self.assertIn('Original host time: ' + refused[0]['timestamp'], review['inputDetails'])
            self.assertIn(refused[0]['error'], review['inputDetails'])
            self.wait_for(lambda: next(p for p in self.read('workspace.get')['graphPluginParameters'] if p['plugin'] == plugin),
                          lambda p: 'envelopes' in p['status'])
            self.assertEqual(self.take()['eventCount'], 0)
            self.assertEqual(self.doc(), before_refusal)
            self.assertEqual(self.read('plugin.state.get', plugin=plugin), baseline)
            self.assertEqual(self.read('automation.get'), points_before)
            self.recording_button(9722)
            self.recording_button(9724)
            self.assertFalse(self.take()['retained'])
            self.assertEqual(self.read('workspace.get')['status'], 'Plugin automation take discarded')
            self.idle()
            self.recording_button(9721)
            take = self.wait_for(self.take, lambda s: s['retained'] and s['capturing'])
            self.assertEqual(self.read('workspace.get')['status'], 'Recording original parameter input / Stop the take to review')

        enter(parameter, str(first_value))
        try:
            captured = self.wait_for(self.take, lambda s: s['eventCount'] >= 1)
        except AssertionError as failure:
            # Preserve the native target/draft/admission state, not only the
            # worker's empty event list, when a composing journey finds a gap.
            self.fail(str(dict(failure=str(failure), expectedPlugin=plugin,
                               recording=self.read('workspace.get')['parameterRecording'])))
        sweep_values = []
        if slider_sweep:
            editor = next(p for p in self.read('workspace.get')['graphPluginParameters'] if p['plugin'] == plugin)
            row = next(r for r in editor['rows'] if r['id'] == parameter)
            slider = private_desktop.user.GetDlgItem(parameters, row['sliderControl'])
            self.assertTrue(slider and private_desktop.user.IsWindowVisible(slider))
            self.assertTrue(private_desktop.user.IsWindowEnabled(slider))
            # This journey uses F04 Balance (-100..100). Deliver the actual
            # trackbar notifications without waiting for the document worker
            # after each input. Every original event must survive that backlog.
            count_before = captured['eventCount']
            for position in range(2000, 8001, 250):
                self.desktop.send(slider, 0x405, 1, position)  # TBM_SETPOS
                self.desktop.send(parameters, 0x114, 5 | (position << 16), slider)  # TB_THUMBTRACK
                sweep_values.append(-100 + position / 50)
                time.sleep(.003)
            self.desktop.send(parameters, 0x114, 8, slider)  # TB_ENDTRACK must not duplicate an input
            captured = self.wait_for(self.take, lambda s: s['eventCount'] >= count_before + len(sweep_values))
            self.assertEqual(captured['eventCount'], count_before + len(sweep_values), captured)
        if sustain_seconds:
            self.trace_recording_clock('first-input', plugin, parameter)
            until = time.monotonic() + sustain_seconds
            next_trace = time.monotonic() + 5
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
                if time.monotonic() >= next_trace:
                    self.trace_recording_clock('sustained-hold', plugin, parameter)
                    next_trace = time.monotonic() + 5
                time.sleep(.5)
            self.trace_recording_clock('before-second-input', plugin, parameter)
            enter(parameter, str(later_value))
            self.wait_for(self.take, lambda s: s['eventCount'] > initial_count)
            self.trace_recording_clock('second-input', plugin, parameter)
        self.assertEqual(self.doc()['revision'], revision, 'Recording input must not add manual edit history')
        self.assertEqual(self.read('plugin.state.get', plugin=plugin), baseline)
        events = self.read('automation.recording.events', take=take['take'], plugin=plugin, parameter=parameter)
        inspected = self.read('automation.recording.events', take=take['take'], plugin=plugin,
                              parameter=parameter, limit=16, includeTiming=True)
        self.assertEqual(inspected['total'], events['total'])
        for original, observed in zip(events['events'], inspected['events']):
            self.assertEqual({k: observed[k] for k in ('timestamp', 'value')},
                             {k: original[k] for k in ('timestamp', 'value')})
            if original['frame'] is not None:
                self.assertEqual(observed['frame'], original['frame'])
                self.assertEqual(observed['timing']['status'], 'retained-frame')
        for fields in (dict(includeTiming=1, limit=1), dict(includeTiming=True, limit=17)):
            with self.subTest(fields=fields), self.assertRaises(ApiError) as raised:
                self.read('automation.recording.events', take=take['take'], plugin=plugin, parameter=parameter, **fields)
            self.assertEqual(raised.exception.code, -32602)
        self.assertEqual(self.doc()['revision'], revision, 'Timing inspection must not change history')
        self.assertTrue(any(e['value'] == first_value for e in events['events']))
        if sweep_values:
            self.assertEqual([e['value'] for e in events['events'][-len(sweep_values):]], sweep_values, events)
            self.assertEqual([int(e['timestamp']) for e in events['events']],
                             sorted(int(e['timestamp']) for e in events['events']))
        if sustain_seconds:
            self.assertTrue(any(e['value'] == later_value for e in events['events']))
        self.assertTrue(all(int(e['timestamp']) >= int(take['startTime']) for e in events['events']))
        self.recording_button(9722)
        stopped = self.wait_for(self.take, lambda s: s['retained'] and not s['capturing'])
        if sustain_seconds:
            self.trace_recording_clock('stopped-take', plugin, parameter)
        self.assertIsNotNone(stopped['endTime'])
        if sweep_values:
            self.trace_recording_clock('stopped-slider-sweep', plugin, parameter)
            page = self.read('automation.recording.events', take=take['take'], plugin=plugin, parameter=parameter)
            self.assertTrue(all(e['frame'] is not None for e in page['events']), page)
            self.assertTrue(all(t['unmapped'] == 0 and t['overflow'] == 0 for t in stopped['targets']), stopped)
        self.assertEqual(self.read('workspace.get')['status'], 'Plugin automation take stopped / Keep or Discard to finish')
        self.assertTrue(self.read('transport.get')['playing'])
        self.recording_button(9723)
        try:
            self.wait_for(self.take, lambda s: not s['retained'])
        finally:
            if sustain_seconds:
                self.trace_recording_clock('after-keep', plugin, parameter)
        self.assertEqual(self.read('workspace.get')['status'], 'Plugin automation take kept')
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
                      parameter=parameter, value=discard_value, timestamp=timestamp)
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
        original_events = self.read('automation.recording.events', take=second['take'], plugin=plugin, parameter=parameter)
        self.recording_button(9740)
        self.assertFalse(self.read('transport.get')['playing'])
        playback_stopped = self.take()
        self.assertTrue(playback_stopped['retained'] and playback_stopped['discardable'], playback_stopped)
        self.assertEqual(playback_stopped['take'], second['take'])
        self.assertEqual(self.read('automation.recording.events', take=second['take'], plugin=plugin, parameter=parameter), original_events)
        self.assertEqual(self.doc(), state_before, 'Stopping playback must not Keep or Discard the retained music')
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
