"""Arrangement matrix through actual application HWNDs on owned private desktops."""
import os
import time
import unittest

import private_desktop
import test_recording as recording
import test_song_tools_ui as song_ui


class MatrixUITests(unittest.TestCase):
    setUp = recording.RecordingTests.setUp
    launch = recording.RecordingTests.launch
    doc = recording.RecordingTests.doc
    read = recording.RecordingTests.read
    write = recording.RecordingTests.write
    command = recording.RecordingTests.command
    state = song_ui.SongToolsUITests.state
    window = song_ui.SongToolsUITests.window
    control = song_ui.SongToolsUITests.control
    wait_workspace = song_ui.SongToolsUITests.wait_workspace
    idle = song_ui.SongToolsUITests.idle
    press = song_ui.SongToolsUITests.press
    select = song_ui.SongToolsUITests.select
    text = song_ui.SongToolsUITests.text
    context = song_ui.SongToolsUITests.context
    focus_control = song_ui.SongToolsUITests.focus_control
    modified_key = song_ui.SongToolsUITests.modified_key
    resize_client = song_ui.SongToolsUITests.resize_client
    assert_visible_geometry = song_ui.SongToolsUITests.assert_visible_geometry
    tools = {'matrix': ('ScreamSeq.ArrangementMatrix', 'arrangementMatrixWindow', 561),
             'arrangement': ('ScreamSeq.Arrangement', 'arrangementWindow', 553)}

    def open_tool(self, tool='matrix', command=None):
        song_ui.SongToolsUITests.open_tool(self, tool, command)
        if tool == 'matrix':
            self.fresh()

    def fresh(self):
        return self.wait_workspace(lambda value: not value['documentBusy']
                                   and value['arrangementMatrixWindow']['fresh']
                                   and not value['arrangementMatrixWindow']['pending'],
                                   'Matrix did not finish its current revision read')['arrangementMatrixWindow']

    def block(self, order, channel):
        state = self.fresh()
        self.assertLessEqual(state['startOrder'], order)
        self.assertLess(order, state['startOrder'] + state['orderCount'])
        self.assertLessEqual(state['startChannel'], channel)
        self.assertLess(channel, state['startChannel'] + state['trackCount'])
        self.focus_control(8401)
        control = self.control(8401)
        self.desktop.send(control, 0x100, 0x24)  # Home, then Down; no remote LVITEM pointers.
        for _ in range(order - state['startOrder']):
            self.desktop.send(control, 0x100, 0x28)
        for _ in range(state['trackCount']):
            self.desktop.send(control, 0x100, 0x25)
        for _ in range(channel - state['startChannel']):
            self.desktop.send(control, 0x100, 0x27)
        document = self.doc()['data']
        order_id = document['orderMetadata'][order]['id']
        track_id = next(t['id'] for t in document['tracks'] if t['index'] == channel)
        self.wait_workspace(lambda value: value['arrangementMatrixWindow']['selectedOrderID'] == order_id
                            and value['arrangementMatrixWindow']['selectedTrackID'] == track_id,
                            f'Matrix did not select block {order}/{channel}')
        return self.state()

    def test_minimum_geometry_options_focus_and_arrangement_entry(self):
        self.open_tool('arrangement')
        self.press(8030)
        self.active_tool = 'matrix'
        self.fresh()
        self.assertTrue(self.state()['visible'])
        native = self.window()
        self.resize_client(900, 620)
        self.assertTrue(set(range(8401, 8416)).issubset(self.assert_visible_geometry()))
        before = self.doc()
        self.select(8410, 2)
        self.press(8411)
        self.press(8412)
        self.focus_control(8410)
        self.command(561)
        self.fresh()
        self.assertEqual(self.window(), native)
        self.assertEqual(self.desktop.focus(native), self.control(8410))
        self.assertEqual((self.state()['mode'], self.state()['makeUnique'], self.state()['clip']),
                         ('mix', False, True))
        self.resize_client(1080, 760)
        self.resize_client(900, 620)
        self.assert_visible_geometry()
        self.press(8415)
        self.assertFalse(self.state()['visible'])
        self.open_tool()
        self.assertEqual(self.window(), native)
        self.assertEqual((self.state()['mode'], self.state()['makeUnique'], self.state()['clip']),
                         ('mix', False, True))
        self.assertEqual(self.doc(), before)

    def test_native_only_keyboard_copy_independent_paste_undo_and_reopen(self):
        self.write('pattern.notes.set', pattern=0, events=[
            dict(channel=4, position=17, note=61, instrument=1, velocity=100),
            dict(channel=4, position=2 * 65536, note=255)])
        self.write('pattern.effects.set', pattern=0, columns=[dict(channel=4, count=2)],
                   commands=[dict(channel=4, position=65536 + 31, column=1, kind='note-cut')])
        self.write('order.edit', order=0, operation='after', pattern=0)
        before = self.doc()['data']
        original_notes = self.read('pattern.notes.get', pattern=0)
        original_effects = self.read('pattern.effects.get', pattern=0)
        self.open_tool()
        source = self.block(0, 4)
        self.modified_key(self.control(8401), ord('C'), ctrl=True)
        self.assertEqual(self.state()['copied']['trackID'], source['selectedTrackID'])
        self.block(1, 5)
        self.modified_key(self.control(8401), ord('V'), ctrl=True)
        self.fresh()
        self.assertIsNone(self.state()['copied'])
        after = self.doc()['data']
        target = after['orders'][1]
        self.assertNotEqual(target, 0)
        self.assertEqual(after['orders'][0], 0)
        self.assertEqual(len(after['patterns']), len(before['patterns']) + 1)
        self.assertEqual(self.read('pattern.notes.get', pattern=0), original_notes)
        # Visible FX column counts belong to the track across the song. The
        # original pattern's commands remain exact while destination FX2 appears.
        expected_columns = [dict(item, count=2) if item['channel'] == 5 else item
                            for item in original_effects['columns']]
        self.assertEqual(self.read('pattern.effects.get', pattern=0),
                         dict(original_effects, columns=expected_columns))
        notes = self.read('pattern.notes.get', pattern=target)
        effects = self.read('pattern.effects.get', pattern=target)
        copied_notes = [dict(event, channel=4) for event in notes['events'] if event['channel'] == 5]
        self.assertEqual(copied_notes, [event for event in original_notes['events'] if event['channel'] == 4])
        copied_effects = [dict(command, channel=4) for command in effects['commands'] if command['channel'] == 5]
        self.assertEqual(copied_effects, [command for command in original_effects['commands'] if command['channel'] == 4])
        self.write('history.undo', domain='document')
        self.fresh()
        restored = self.doc()['data']
        for key in ('orders', 'orderMetadata', 'patterns'):
            self.assertEqual(restored[key], before[key], key)
        self.write('history.redo', domain='document')
        self.fresh()
        self.assertEqual(self.read('pattern.notes.get', pattern=target), notes)
        self.assertEqual(self.read('pattern.effects.get', pattern=target), effects)
        path = self.folder / 'Matrix native copy.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path))
        self.fresh()
        self.assertEqual(self.read('pattern.notes.get', pattern=target), notes)
        self.assertEqual(self.read('pattern.effects.get', pattern=target), effects)

    def test_stale_copy_survives_refresh_hide_and_document_replacement(self):
        path = self.folder / 'Matrix replacement.screamseq'
        self.write('document.save', path=str(path))
        self.open_tool()
        self.block(0, 0)
        self.press(8408)
        captured = self.state()['copied']
        self.write('document.patch', title='External edit while source retained')
        self.fresh()
        self.assertTrue(self.state()['copied']['stale'])
        self.assertEqual(self.state()['copied']['revision'], captured['revision'])
        self.assertFalse(private_desktop.user.IsWindowEnabled(self.control(8409)))
        before = self.doc()
        self.focus_control(8401)
        self.modified_key(self.control(8401), ord('V'), ctrl=True)
        self.press(8406)
        self.fresh()
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.state()['copied']['generation'], captured['generation'])
        self.press(8415)
        self.open_tool()
        self.assertTrue(self.state()['copied']['stale'])
        self.write('document.open', path=str(path), discard=True)
        self.fresh()
        self.assertTrue(self.state()['copied']['stale'])
        self.assertEqual(self.state()['copied']['documentId'], captured['documentId'])
        self.press(8408)
        self.assertFalse(self.state()['copied']['stale'])
        self.assertTrue(private_desktop.user.IsWindowEnabled(self.control(8409)))
        self.press(8413)
        self.assertIsNone(self.state()['copied'])

    def test_selection_is_independent_and_open_returns_to_pattern(self):
        self.write('order.edit', order=0, operation='after', pattern=0)
        self.context(pattern=0, row=7, channel=1, column=2, following=True)
        self.open_tool()
        before = self.read('context.get')
        self.block(1, 4)
        after = self.read('context.get')
        for key in ('pattern', 'row', 'channel', 'column', 'following'):
            self.assertEqual(after[key], before[key], key)
        self.client.call('workspace.panel', dict(panel='automation', placement='right', focus=True))
        self.assertFalse(self.read('workspace.get')['editorDock']['trackerVisible'])
        self.focus_control(8401)
        self.desktop.send(self.control(8401), 0x100, 13)
        self.idle()
        cursor = self.read('context.get')
        self.assertEqual((cursor['pattern'], cursor['row'], cursor['channel'], cursor['column'], cursor['following']),
                         (0, 0, 4, 0, False))
        workspace = self.read('workspace.get')
        self.assertTrue(workspace['editorDock']['trackerVisible'])
        self.assertEqual(workspace['arrangementSelection']['order'], 1)
        self.assertEqual(self.desktop.focus(self.window()), self.desktop.hwnd(self.pid))
        self.focus_control(8401)
        self.desktop.send(self.control(8401), 0x100, 0x75)  # F6.
        self.idle()
        self.assertFalse(self.state()['visible'])

    def test_track_pages_reveal_selection_and_preserve_copied_source(self):
        self.write('document.patch', channels=25)
        self.open_tool()
        document = self.doc()['data']
        self.block(0, 11)
        self.press(8408)
        copied = self.state()['copied']
        self.press(8405)
        self.fresh()
        self.assertEqual((self.state()['startChannel'], self.state()['trackCount']), (12, 12))
        self.assertEqual(self.state()['selectedTrackID'], document['tracks'][12]['id'])
        self.assertEqual(self.state()['copied'], copied)
        self.block(0, 23)
        self.press(8405)
        self.fresh()
        self.assertEqual((self.state()['startChannel'], self.state()['trackCount']), (24, 1))
        self.assertEqual(self.state()['selectedTrackID'], document['tracks'][24]['id'])
        self.assertFalse(private_desktop.user.IsWindowEnabled(self.control(8405)))
        self.press(8404)
        self.fresh()
        self.assertEqual(self.state()['selectedTrackID'], document['tracks'][12]['id'])
        self.assertEqual(self.state()['copied'], copied)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO') == '1', 'Explicit silent output required')
    def test_navigation_copy_and_noop_keep_playing_changed_paste_stops(self):
        self.pid, self.client = self.launch(audio=True)
        self.open_tool()
        initial = self.read('transport.get')
        self.assertTrue(initial['playing'] and initial['audioActive'])
        self.block(0, 0)
        before = self.doc()
        self.press(8408)
        self.press(8409)  # Exact self-copy must not clone, publish, or stop playback.
        self.fresh()
        self.assertEqual(self.doc(), before)
        self.press(8408)
        self.block(0, 1)
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            transport = self.read('transport.get')
            self.assertTrue(transport['playing'] and transport['audioActive'], transport)
            self.assertFalse(transport['fault'], transport)
            self.assertEqual(transport['playbackEpoch'], initial['playbackEpoch'])
            if transport['callbacks'] > initial['callbacks'] + 2 and transport['frames'] > initial['frames']:
                break
            time.sleep(.02)
        else:
            self.fail('Silent playback did not advance during matrix navigation and no-op paste')
        self.press(8409)
        self.fresh()
        self.assertNotEqual(self.doc()['revision'], before['revision'])
        self.assertFalse(self.read('transport.get')['playing'])


if __name__ == '__main__':
    unittest.main()
