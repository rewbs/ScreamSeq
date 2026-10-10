"""Arrangement and song timing through retained native HWNDs on an owned desktop."""
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile
import time
import unittest

import private_desktop
import test_midi_recording_ui as midi_ui
import test_parameter_compact_ui as compact
import test_recording as recording
from client import Client, TransportError


class SongToolsUITests(unittest.TestCase):
    setUp = recording.RecordingTests.setUp
    launch = recording.RecordingTests.launch
    doc = recording.RecordingTests.doc
    read = recording.RecordingTests.read
    write = recording.RecordingTests.write
    command = recording.RecordingTests.command
    resize_client = compact.ParameterCompactUITests.resize_client
    assert_visible_geometry = compact.ParameterCompactUITests.assert_visible_geometry
    modified_key = compact.ParameterCompactUITests.modified_key
    focus_control = midi_ui.MidiRecordingUITests.focus_control

    tools = {'arrangement': ('ScreamSeq.Arrangement', 'arrangementWindow', 553),
             'timing': ('ScreamSeq.SongTiming', 'songTimingWindow', 554)}

    def state(self, tool=None):
        return self.read('workspace.get')[self.tools[tool or self.active_tool][1]]

    def window(self, tool=None):
        kind = self.tools[tool or self.active_tool][0]
        found = []

        @private_desktop.callback
        def visit(hwnd, _):
            owner = wintypes.DWORD()
            private_desktop.user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            name = ctypes.create_unicode_buffer(128)
            private_desktop.user.GetClassNameW(hwnd, name, len(name))
            if owner.value == self.pid and name.value == kind:
                found.append(hwnd)
            return True

        private_desktop.check(private_desktop.user.EnumDesktopWindows(self.desktop.desktop, visit, 0))
        self.assertEqual(len(found), 1, (kind, found))
        return found[0]

    def control(self, identifier):
        result = private_desktop.user.GetDlgItem(self.window(), identifier)
        self.assertTrue(result, identifier)
        return result

    def wait_workspace(self, predicate, reason):
        deadline = time.monotonic() + 8
        observations = []
        while time.monotonic() < deadline:
            try:
                workspace = Client(self.client.pipe, timeout=min(.5, max(.001, deadline - time.monotonic()))).call('workspace.get')['data']
                observations.append({key: workspace.get(key) for key in (
                    'documentBusy', 'arrangementSelection', 'arrangementWindow', 'songTimingWindow', 'recording', 'status')})
                observations = observations[-4:]
                if predicate(workspace):
                    return workspace
            except TransportError as error:
                observations.append({'readError': str(error)})
                observations = observations[-4:]
            remaining = deadline - time.monotonic()
            if remaining > 0:
                time.sleep(min(.03, remaining))
        self.fail(f'{reason}: {observations}')

    def idle(self):
        key = self.tools[self.active_tool][1]
        return self.wait_workspace(lambda state: not state['documentBusy'] and not state.get(key, {}).get('pending'),
                                   'Song tool did not become idle')

    def open_tool(self, tool, command=None):
        self.active_tool = tool
        self.command(command or self.tools[tool][2])
        key = self.tools[tool][1]
        self.wait_workspace(lambda state: not state['documentBusy'] and state[key]['visible'] and not state[key]['pending'],
                            f'{tool} did not open')

    def field(self, identifier, value):
        control = self.control(identifier)
        self.assertTrue(private_desktop.user.IsWindowVisible(control), identifier)
        self.assertTrue(private_desktop.user.IsWindowEnabled(control), identifier)
        text = ctypes.create_unicode_buffer(str(value))
        self.desktop.send(control, 0xC, 0, ctypes.addressof(text))

    def text(self, identifier):
        result = ctypes.create_unicode_buffer(8193)
        self.desktop.send(self.control(identifier), 0xD, len(result), ctypes.addressof(result))
        return result.value

    def press(self, identifier):
        key = self.tools[self.active_tool][1]
        control = self.control(identifier)
        self.wait_workspace(lambda state: not state['documentBusy'] and not state[key]['pending']
                            and private_desktop.user.IsWindowEnabled(control), f'Button {identifier} did not enable')
        self.assertTrue(private_desktop.user.IsWindowVisible(control), identifier)
        self.desktop.send(control, 0xF5)  # One native BM_CLICK, after observing readiness.
        self.idle()

    def select(self, identifier, index):
        self.idle()
        control = self.control(identifier)
        self.assertTrue(private_desktop.user.IsWindowVisible(control), identifier)
        self.assertTrue(private_desktop.user.IsWindowEnabled(control), identifier)
        self.focus_control(identifier)
        self.assertNotEqual(self.desktop.send(control, 0x14E, index), ctypes.c_size_t(-1).value)
        self.desktop.send(self.window(), 0x111, identifier | (1 << 16), control)  # CBN_SELCHANGE.
        self.idle()

    def order(self, index):
        self.assertEqual(self.active_tool, 'arrangement')
        self.focus_control(8001)
        self.desktop.send(self.control(8001), 0x100, 0x24)  # Native Home/Down; no cross-process LVITEM pointer.
        for _ in range(index):
            self.desktop.send(self.control(8001), 0x100, 0x28)
        result = self.wait_workspace(lambda state: not state['documentBusy']
                                     and state['arrangementSelection']['order'] == index,
                                     f'Native list did not select order {index}')
        self.assertEqual(result['arrangementWindow']['selectedOrderID'], result['arrangementSelection']['id'])
        return result['arrangementSelection']

    def context(self, **patch):
        current = self.client.call('context.get')
        return self.client.call('context.set', dict(expectedRevision=current['revision'],
                               expectedContext=current['data']['contextRevision'], **patch))

    def import_arrangement_fixture(self):
        source = os.environ.get('SCREAMSEQ_TEST_ARRANGEMENT_PROJECT')
        self.assertTrue(source and Path(source).is_file(),
                        'SCREAMSEQ_TEST_ARRANGEMENT_PROJECT must name the generated native arrangement-catalog.screamseq fixture')
        path = self.folder / 'Owned arrangement fixture.screamseq'
        shutil.copyfile(source, path)
        self.write('document.open', path=str(path), discard=True)

    def timing(self):
        return self.read('document.timing.get')

    def test_arrangement_minimum_raw_draft_focus_reopen_and_explicit_reload(self):
        self.open_tool('arrangement', 555)
        self.assertEqual(self.state()['mode'], 'new')
        self.resize_client(760, 600)
        self.assertTrue(set(range(8001, 8018)).issubset(self.assert_visible_geometry()))
        before = self.doc(); native = self.window(); field = self.control(8011)
        self.field(8011, '-')
        self.focus_control(8011); self.desktop.send(field, 0xB1, 0, 1)
        draft = self.state()
        self.command(556)  # Reopening through Duplicate must retain the existing creation draft.
        self.idle()
        self.assertEqual(self.window(), native)
        self.assertEqual(self.state()['rowsText'], '-')
        self.assertEqual(self.state()['sourcePatternID'], draft['sourcePatternID'])
        self.assertEqual(self.desktop.focus(native), field)
        self.assertEqual(self.desktop.send(field, 0xB0), 1 << 16)
        self.resize_client(860, 650); self.resize_client(760, 600)
        self.assertEqual(self.desktop.focus(native), field)
        self.assertEqual(self.desktop.send(field, 0xB0), 1 << 16)
        self.modified_key(field, 13, ctrl=True); self.idle()
        self.assertEqual(self.doc(), before, 'Enter in a row field must not implicitly create a pattern')
        self.press(8013)
        self.assertEqual(self.doc(), before)
        self.assertTrue(self.state()['error'])
        self.assertEqual(self.text(8011), '-')
        self.press(8017); self.assertFalse(self.state()['visible'])
        self.open_tool('arrangement'); self.assertEqual(self.window(), native)
        self.assertEqual(self.text(8011), '-')
        self.write('document.patch', title='Changed while arrangement draft retained')
        after = self.doc()
        self.wait_workspace(lambda state: state['arrangementWindow']['stale'], 'Arrangement draft did not become stale')
        self.assertEqual(self.state()['draftRevision'], draft['draftRevision'])
        self.assertEqual(self.text(8011), '-')
        self.assertFalse(private_desktop.user.IsWindowEnabled(self.control(8013)))
        self.assertFalse(private_desktop.user.IsWindowEnabled(self.control(8014)))
        self.press(8015)
        self.assertFalse(self.state()['stale']); self.assertFalse(self.state()['dirty'])
        self.assertEqual(self.state()['draftRevision'], after['revision'])
        self.assertEqual(self.doc(), after)
        self.field(8011, '16'); self.press(8013)
        created = self.doc()['data']; index = created['orders'][-1]
        self.assertEqual(len(created['patterns']), len(after['data']['patterns']) + 1)
        self.assertEqual(next(pattern['rows'] for pattern in created['patterns'] if pattern['index'] == index), 16)
        self.assertEqual(self.read('pattern.notes.get', pattern=index)['events'], [])
        self.write('history.undo', domain='document'); self.idle()
        self.assertEqual(self.doc()['data']['patterns'], after['data']['patterns'])
        self.assertEqual(self.doc()['data']['orderMetadata'], after['data']['orderMetadata'])
        self.client.call('workspace.panel', dict(panel='automation', placement='right', focus=True))
        self.assertFalse(self.read('workspace.get')['editorDock']['trackerVisible'])
        self.open_tool('arrangement'); self.press(8016)
        self.assertTrue(self.read('workspace.get')['editorDock']['trackerVisible'])
        self.assertEqual(self.desktop.focus(native), self.desktop.hwnd(self.pid))

    def test_repeated_occurrence_moves_undo_and_native_duplicate_persistence(self):
        self.write('pattern.notes.set', pattern=0, events=[dict(channel=0, position=3 * 65536 + 8192, note=61, instrument=1, velocity=93)])
        created = self.write('pattern.create', rows=32, source=0)['data']['pattern']
        self.write('order.edit', order=0, operation='after', pattern=0)
        self.open_tool('arrangement')
        original = self.doc()['data']; selected = self.order(1); selected_id = selected['id']
        self.assertNotEqual(original['orderMetadata'][0]['id'], selected_id)
        self.press(8007)
        moved = self.doc()['data']
        self.assertEqual(moved['orders'], original['orders'])
        self.assertEqual(moved['orderMetadata'][0]['id'], selected_id)
        self.assertEqual(self.read('workspace.get')['arrangementSelection']['order'], 0)
        self.write('history.undo', domain='document'); self.idle()
        self.assertEqual(self.doc()['data']['orderMetadata'], original['orderMetadata'])
        self.assertEqual(self.read('workspace.get')['arrangementSelection']['id'], selected_id)
        self.assertEqual(self.read('workspace.get')['arrangementSelection']['order'], 1)
        self.write('history.redo', domain='document'); self.idle()
        self.assertEqual(self.doc()['data']['orderMetadata'], moved['orderMetadata'])
        self.focus_control(8001)
        self.modified_key(self.control(8001), 0x28, ctrl=True); self.idle()
        self.assertEqual(self.doc()['data']['orderMetadata'], original['orderMetadata'])
        self.select(8003, 1); self.press(8004)
        self.assertEqual(self.doc()['data']['orders'][1], created)
        self.assertEqual(self.doc()['data']['orderMetadata'][1]['id'], selected_id)
        self.write('history.undo', domain='document'); self.idle()
        self.assertEqual(self.doc()['data']['orders'], original['orders'])
        self.press(8006)
        inserted = self.doc()['data']; inserted_id = self.read('workspace.get')['arrangementSelection']['id']
        self.assertEqual(len(inserted['orders']), len(original['orders']) + 1)
        self.assertNotIn(inserted_id, [entry['id'] for entry in original['orderMetadata']])
        self.press(8009)
        self.assertEqual(self.doc()['data']['orderMetadata'], original['orderMetadata'])
        self.write('history.undo', domain='document'); self.idle()
        self.assertEqual(self.doc()['data']['orderMetadata'], inserted['orderMetadata'])
        self.press(8015); self.select(8012, 1); self.field(8011, '32')
        source_notes = self.read('pattern.notes.get', pattern=created)['events']
        before_duplicate = self.doc()['data']
        self.press(8014)
        duplicated = self.doc()['data']; index = duplicated['orders'][-1]
        self.assertEqual(len(duplicated['patterns']), len(before_duplicate['patterns']) + 1)
        self.assertNotEqual(index, created)
        self.assertEqual(self.read('pattern.notes.get', pattern=index)['events'], source_notes)
        self.assertEqual(self.read('workspace.get')['arrangementSelection']['id'], duplicated['orderMetadata'][-1]['id'])
        self.write('history.undo', domain='document'); self.idle()
        self.assertEqual(self.doc()['data']['patterns'], before_duplicate['patterns'])
        self.assertEqual(self.doc()['data']['orderMetadata'], before_duplicate['orderMetadata'])
        self.write('history.redo', domain='document'); self.idle()
        self.assertEqual(self.doc()['data']['orderMetadata'], duplicated['orderMetadata'])
        path = self.folder / 'Arranged and duplicated.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.doc()['data']['orderMetadata'], duplicated['orderMetadata'])
        self.assertEqual(self.read('pattern.notes.get', pattern=index)['events'], source_notes)

        # The API's arbitrary destination must retain the native selected
        # occurrence by stable ID, just like the adjacent move buttons.
        self.open_tool('arrangement')
        selected_id = self.order(0)['id']
        before_move = self.doc()['data']
        destination = len(before_move['orders']) - 1
        self.write('order.edit', order=0, operation='move', destination=destination)
        self.idle()
        moved = self.doc()['data']
        expected_orders = before_move['orders'][1:] + before_move['orders'][:1]
        expected_metadata = before_move['orderMetadata'][1:] + before_move['orderMetadata'][:1]
        self.assertEqual(moved['orders'], expected_orders)
        self.assertEqual(moved['orderMetadata'], expected_metadata)
        self.assertEqual(self.read('workspace.get')['arrangementSelection']['id'], selected_id)
        self.write('history.undo', domain='all')
        self.assertEqual(self.doc()['data']['orderMetadata'], before_move['orderMetadata'])
        revision = self.doc()['revision']
        self.write('order.edit', order=0, operation='move', destination=0)
        self.assertEqual(self.doc()['revision'], revision)
        self.write('history.redo', domain='all')
        self.assertEqual(self.doc()['data']['orderMetadata'], expected_metadata)
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.doc()['data']['orderMetadata'], expected_metadata)

    def test_sequence_and_sentinel_selection_keep_occurrence_and_other_tool_draft(self):
        self.import_arrangement_fixture()
        self.open_tool('arrangement')
        document = self.doc(); orders = document['data']['orders']
        self.assertEqual(orders[2:4], [65535, 65534])
        before_context = self.read('context.get')
        for index in (2, 3):
            self.order(index)
            self.assertFalse(private_desktop.user.IsWindowEnabled(self.control(8010)))
            context = self.read('context.get')
            for key in ('pattern', 'row', 'channel', 'column'):
                self.assertEqual(context[key], before_context[key])
        self.assertEqual(self.doc(), document)
        self.order(1); selected = self.read('workspace.get')['arrangementSelection']
        self.open_tool('timing'); self.field(7602, '-')
        captured = self.state()['captured']
        self.open_tool('arrangement'); self.field(8011, '-')
        draft = self.state(); self.select(8002, 1)
        second = self.doc()['data']; selection = self.read('workspace.get')['arrangementSelection']
        self.assertEqual(second['sequence'], 1)
        self.assertEqual(selection['sequenceID'], second['sequences'][1]['id'])
        self.assertNotEqual(selection['id'], selected['id'])
        self.assertIn(selection['id'], [entry['id'] for entry in second['orderMetadata']])
        self.assertEqual(self.state()['rowsText'], '-')
        self.assertEqual(self.state()['draftRevision'], draft['draftRevision'])
        self.assertTrue(self.state()['stale'])
        self.assertEqual(self.desktop.focus(self.window()), self.control(8002))
        timing = self.state('timing')
        self.assertEqual(timing['draft']['tempo'], '-')
        self.assertEqual(timing['captured'], captured)
        self.assertTrue(timing['stale'])
        self.press(8015); self.assertFalse(self.state()['stale'])
        self.open_tool('timing'); self.press(7612)
        self.assertEqual(self.state()['captured']['sequence'], 1)
        self.assertEqual(float(self.text(7602)), 155.25)

    def test_timing_minimum_invalid_draft_native_caret_reopen_and_stale_reload(self):
        self.open_tool('timing'); self.resize_client(660, 560)
        self.assertTrue(set(range(7601, 7615)).issubset(self.assert_visible_geometry()))
        before = self.doc(); native = self.window(); field = self.control(7602)
        self.field(7602, '-'); self.field(7606, '1, unfinished')
        self.focus_control(7602); self.desktop.send(field, 0xB1, 0, 1)
        captured = self.state()['captured']
        self.command(554); self.idle()
        self.assertEqual(self.window(), native)
        self.assertEqual(self.desktop.focus(native), field)
        self.assertEqual(self.desktop.send(field, 0xB0), 1 << 16)
        self.resize_client(760, 660); self.resize_client(660, 560)
        self.assertEqual(self.desktop.focus(native), field)
        self.assertEqual(self.desktop.send(field, 0xB0), 1 << 16)
        self.press(7610)
        self.assertEqual(self.doc(), before); self.assertTrue(self.state()['error'])
        self.assertEqual(self.text(7602), '-'); self.assertEqual(self.text(7606), '1, unfinished')
        self.press(7614); self.assertFalse(self.state()['visible'])
        self.open_tool('timing'); self.assertEqual(self.window(), native)
        self.assertEqual(self.text(7602), '-')
        self.write('document.patch', title='External edit while timing is captured')
        after = self.doc()
        self.wait_workspace(lambda state: state['songTimingWindow']['stale'], 'Timing draft did not become stale')
        self.assertEqual(self.state()['captured'], captured)
        self.assertFalse(private_desktop.user.IsWindowEnabled(self.control(7611)))
        self.focus_control(7602); self.modified_key(field, 13, ctrl=True); self.idle()
        self.assertEqual(self.doc(), after); self.assertEqual(self.text(7602), '-')
        self.press(7612)
        self.assertFalse(self.state()['stale']); self.assertFalse(self.state()['dirty'])
        self.assertEqual(self.state()['captured']['revision'], after['revision'])
        self.assertEqual(float(self.text(7602)), self.timing()['tempo'])
        self.assertEqual(self.doc(), after)

    def test_fractional_timing_preview_one_undo_normalized_groove_and_persistence(self):
        self.open_tool('timing'); original = self.timing(); before = self.doc()
        self.select(7601, 2); self.field(7602, '143.25'); self.field(7603, '5')
        self.field(7604, '4'); self.field(7605, '12'); self.field(7606, '2, 1, 2, 1')
        self.focus_control(7606); edit = self.control(7606)
        self.desktop.send(edit, 0xB1, 3, 7)
        self.press(7610)
        preview = self.state()['preview']
        self.assertTrue(preview['dryRun'] and preview['wouldChange'])
        self.assertEqual(self.doc(), before); self.assertEqual(self.timing(), original)
        self.assertEqual(self.text(7606), '2, 1, 2, 1')
        self.assertEqual(self.desktop.send(edit, 0xB0), 3 | (7 << 16))
        self.assertAlmostEqual(sum(preview['after']['groove']), 4, places=3)
        self.assertEqual(preview['after']['tempo'], 143.25)
        self.focus_control(7602); tempo = self.control(7602); self.desktop.send(tempo, 0xB1, 1, 4)
        self.modified_key(tempo, 13, ctrl=True); self.idle()
        saved = self.timing()
        self.assertEqual(saved, preview['after'])
        self.assertEqual(self.desktop.focus(self.window()), tempo)
        self.assertEqual(self.desktop.send(tempo, 0xB0), 1 | (4 << 16))
        self.assertFalse(self.state()['dirty'])
        committed = self.doc(); self.press(7611); self.assertEqual(self.doc(), committed)
        self.write('history.undo', domain='document'); self.assertEqual(self.timing(), original)
        self.write('history.redo', domain='document'); self.assertEqual(self.timing(), saved)
        path = self.folder / 'Fractional timing.screamseq'
        self.write('document.save', path=str(path)); self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.timing(), saved)
        self.assertEqual(self.doc()['data']['tempo'], 143.25)

    def test_swing_straight_are_local_and_changed_timing_retains_incompatible_take(self):
        path = self.folder / 'Owned take for timing.screamseq'
        self.write('document.save', path=str(path))
        tree = plistlib.loads(path.read_bytes()); native = tree['native']
        tree['recoveryTake'] = dict(compatible=True, missingTime=0, exhaustedVoices=0, overflow=0, events=[
            dict(pattern=native['patterns'][0][1]['id'], track=native['tracks'][0][1]['id'],
                 position=3 * 65536 + 8192, instrument=1, note=61, velocity=93)])
        path.write_bytes(plistlib.dumps(tree, fmt=plistlib.FMT_BINARY))
        self.write('document.open', path=str(path), discard=True)
        self.open_tool('timing'); before = self.doc(); timing = self.timing(); take = self.read('recording.get')
        self.assertTrue(take['compatible']); self.assertEqual(take['eventCount'], 1)
        self.field(7604, '4'); self.field(7607, '62.5'); self.press(7608)
        self.assertEqual(self.state()['draft']['mode'], 'modern')
        self.assertEqual([float(v.strip()) for v in self.text(7606).split(',')], [1.25, .75, 1.25, .75])
        self.assertEqual(self.doc(), before); self.assertEqual(self.timing(), timing)
        self.press(7609); self.assertEqual(self.text(7606), '')
        self.assertEqual(self.doc(), before)
        self.field(7604, '3'); self.press(7608)
        self.assertTrue(self.state()['error']); self.assertEqual(self.doc(), before)
        self.field(7604, '4'); self.press(7608); self.press(7610)
        self.assertEqual(self.doc(), before)
        self.press(7611)
        workspace = self.wait_workspace(lambda state: not state['documentBusy'] and not state['recording']['capturing'],
                                        'Timing edit did not settle the retained take')
        retained = self.read('recording.get')
        self.assertEqual(retained['take'], take['take']); self.assertEqual(retained['events'], take['events'])
        self.assertFalse(retained['compatible']); self.assertFalse(workspace['recording']['compatible'])
        self.assertEqual(self.timing()['mode'], 'modern')
        self.assertTrue(self.doc()['data']['hasRecoveryTake'])

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO') == '1', 'Explicit silent output required')
    def test_selected_order_play_keeps_cursor_and_preview_noop_keeps_transport(self):
        self.pid, self.client = self.launch(audio=True)
        self.write('transport.stop')
        self.write('order.edit', order=0, operation='after', pattern=0)
        self.context(pattern=0, row=19, channel=2, column=0, following=False)
        self.open_tool('arrangement'); selection = self.order(1)
        self.context(pattern=0, row=19, channel=2, column=0, following=False)
        cursor = self.read('context.get'); self.press(8010)
        deadline = time.monotonic() + 8; observations = []
        while time.monotonic() < deadline:
            transport = self.read('transport.get'); workspace = self.read('workspace.get')
            observations.append(transport); observations = observations[-8:]
            self.assertFalse(transport['fault'], transport)
            if transport['playing'] and transport['audioActive'] and not workspace['documentBusy'] and transport['order'] == selection['order']:
                break
            time.sleep(.03)
        else:
            self.fail(f'Selected-order playback did not start: {observations}')
        epoch = transport['playbackEpoch']; context = self.read('context.get')
        for key in ('pattern', 'row', 'channel', 'column', 'following'):
            self.assertEqual(context[key], cursor[key])
        self.open_tool('timing'); before = self.doc()
        self.press(7610)
        self.assertFalse(self.state()['preview']['wouldChange'])
        self.assertEqual(self.doc(), before)
        self.press(7611)
        after = self.read('transport.get')
        self.assertTrue(after['playing'] and after['audioActive'])
        self.assertEqual(after['playbackEpoch'], epoch)
        self.assertEqual(self.doc(), before)
        self.field(7602, '151.25'); self.press(7611)
        self.assertFalse(self.read('transport.get')['playing'])
        self.assertEqual(self.timing()['tempo'], 151.25)


class ArrangementControllerTests(unittest.TestCase):
    def test_stable_catalog_limits_sequence_budget_history_and_reopen(self):
        executable = Path(os.environ['SCREAMSEQ_TEST_EXE']).with_name('document-controller-tests.exe')
        with tempfile.TemporaryDirectory(prefix='arrangement-controller-', dir=os.environ['TMPDIR']) as directory:
            result = subprocess.run([str(executable), '--arrangement', directory],
                                    capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for marker in ('PASS arrangement identities,', 'PASS actual MOD/XM/S3M/IT/MPTM limits',
                       'PASS sequence identity metadata budget,'):
            self.assertIn(marker, result.stdout)
