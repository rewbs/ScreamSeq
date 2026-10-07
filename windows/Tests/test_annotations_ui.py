"""Sections and retained annotation drafts through the real owned Windows app."""
import ctypes
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest

import private_desktop
import test_song_tools_ui as song_tools
import test_recording as recording


class AnnotationTests(unittest.TestCase):
    # Reuse native-HWND helpers without inheriting unrelated test cases.
    setUp = recording.RecordingTests.setUp
    launch = recording.RecordingTests.launch
    doc = recording.RecordingTests.doc
    read = recording.RecordingTests.read
    write = recording.RecordingTests.write
    command = recording.RecordingTests.command
    reject = recording.RecordingTests.reject
    tools = song_tools.SongToolsUITests.tools
    state = song_tools.SongToolsUITests.state
    window = song_tools.SongToolsUITests.window
    control = song_tools.SongToolsUITests.control
    wait_workspace = song_tools.SongToolsUITests.wait_workspace
    idle = song_tools.SongToolsUITests.idle
    open_tool = song_tools.SongToolsUITests.open_tool
    text = song_tools.SongToolsUITests.text
    press = song_tools.SongToolsUITests.press
    select = song_tools.SongToolsUITests.select
    order = song_tools.SongToolsUITests.order
    context = song_tools.SongToolsUITests.context
    import_arrangement_fixture = song_tools.SongToolsUITests.import_arrangement_fixture
    resize_client = song_tools.SongToolsUITests.resize_client
    assert_visible_geometry = song_tools.SongToolsUITests.assert_visible_geometry
    modified_key = song_tools.SongToolsUITests.modified_key
    focus_control = song_tools.SongToolsUITests.focus_control

    def field(self, identifier, value):
        if identifier != 8027:
            return song_tools.SongToolsUITests.field(self, identifier, value)
        control = self.control(identifier)
        self.assertTrue(private_desktop.user.IsWindowVisible(control))
        self.assertTrue(private_desktop.user.IsWindowEnabled(control))
        # Multiline WM_SETTEXT omits EN_CHANGE. Native replacement exercises
        # the same generation/dirty notification as interactive text editing.
        before = self.state()['patternDraft']['generation']
        text = ctypes.create_unicode_buffer(str(value))
        self.desktop.send(control, 0xB1, 0, -1)  # EM_SETSEL, whole text.
        self.desktop.send(control, 0xC2, 1, ctypes.addressof(text))  # EM_REPLACESEL.
        self.assertGreater(self.state()['patternDraft']['generation'], before)

    def test_api_discovery_partial_patch_replay_validation_noop_and_redo(self):
        desc = self.read('api.describe')
        self.assertIn('arrangement.get', desc['reads'])
        self.assertIn('song.annotate', desc['writes'])
        self.assertEqual(desc['revisionGuards']['song.annotate'], ['expectedRevision'])
        before = self.doc(); target = before['data']['patterns'][0]['id']
        base = dict(expectedRevision=before['revision'], id=target)
        for patch in ({}, {'unknown': 1}, {'dryRun': True, 'name': 'x'}, {'name': None},
                      {'name': '\U0001f3b5' * 129}, {'annotation': '\U0001f3b5' * 2049},
                      {'color': True}, {'color': -1}, {'color': 0x1000000}, {'color': 1.5},
                      {'id': 'n01', 'name': 'bad'}, {'id': target[1:], 'name': 'bad'},
                      {'id': 'n18446744073709551615', 'name': 'missing'}):
            self.reject(-32602, 'song.annotate', **dict(base, **patch))
            self.assertEqual(self.doc(), before)
        self.reject(-32602, 'arrangement.get', unexpected=True)
        self.reject(-32001, 'song.annotate', expectedRevision='old', id=target, name='stale')
        params = dict(base, name='Theme / 序奏 🎵', annotation='First line\nSecond line', color=0x396AC7)
        changed = self.client.call('song.annotate', params, request_id='annotation-once')
        self.assertTrue(changed['changed']); self.assertFalse(changed['playbackStopped'])
        self.assertEqual(changed, self.client.call('song.annotate', params, request_id='annotation-once'))
        self.assertEqual(changed['data'], dict(id=target, name=params['name'], annotation=params['annotation'], color=params['color']))
        revision = self.doc()['revision']
        self.write('song.annotate', id=target, name=params['name'])
        self.assertEqual(self.doc()['revision'], revision)
        self.write('history.undo', domain='document')
        undone = self.doc(); original = before['data']['patterns'][0]
        self.assertTrue(undone['data']['canRedo'])
        noop = self.write('song.annotate', id=target, name=original['name'])
        self.assertFalse(noop['changed']); self.assertEqual(self.doc(), undone)
        self.write('history.redo', domain='document')
        self.assertEqual({k: self.doc()['data']['patterns'][0][k] for k in changed['data']}, changed['data'])
        patched = self.write('song.annotate', id=target, annotation='Only notes changed')['data']
        self.assertEqual(patched, dict(changed['data'], annotation='Only notes changed'))

    def test_sections_inactive_sequence_targets_and_native_roundtrip(self):
        self.import_arrangement_fixture()
        original = self.read('arrangement.get'); self.assertEqual(original['sections'], [])
        self.assertEqual(len(original['orders']), 6)
        first, marker, end = [original['orders'][i]['id'] for i in (1, 2, 4)]
        self.write('song.annotate', id=first, name='Verse', annotation='Occurrence notes', color=0xABCDEF)
        self.write('song.annotate', id=marker, name=' ', color=0x654321)
        self.write('song.annotate', id=end, name='Outro')
        arrangement = self.read('arrangement.get')
        self.assertEqual([(x['firstOrder'], x['lastOrder'], x['name']) for x in arrangement['sections']],
                         [(1, 1, 'Verse'), (2, 3, ' '), (4, 5, 'Outro')])
        self.assertNotIn('patternID', arrangement['orders'][2])
        self.assertNotIn('patternID', arrangement['orders'][3])
        self.assertEqual(arrangement['orders'][0]['patternID'], arrangement['orders'][1]['patternID'])
        self.assertNotEqual(arrangement['orders'][0]['id'], first)
        self.write('sequence.select', sequence=1)
        second = self.read('arrangement.get'); inactive = second['orders'][0]['id']
        self.write('sequence.select', sequence=0)
        self.write('song.annotate', id=inactive, name='Second sequence section', annotation='Retained inactive notes')
        doc = self.doc()['data']; sequence = doc['sequences'][1]['id']; track = doc['tracks'][0]['id']
        self.write('song.annotate', id=sequence, name='Sequence / 序列', annotation='Sequence notes', color=0x204060)
        self.write('song.annotate', id=track, name='Lead', annotation='Track notes', color=0x709050)
        self.write('song.annotate', id=marker, name='')
        self.assertEqual([(x['firstOrder'], x['lastOrder']) for x in self.read('arrangement.get')['sections']], [(1, 3), (4, 5)])
        before = self.doc()['data']; arrangement = self.read('arrangement.get')
        path = self.folder / 'Annotations.screamseq'
        self.write('document.save', path=str(path)); self.write('document.open', path=str(path), discard=True)
        after = self.doc()['data']
        for key in ('orderMetadata', 'patterns', 'sequences', 'tracks'):
            self.assertEqual(after[key], before[key], key)
        self.assertEqual(self.read('arrangement.get'), arrangement)
        self.write('sequence.select', sequence=1)
        reopened = self.read('arrangement.get')
        self.assertEqual(reopened['orders'][0]['id'], inactive)
        self.assertEqual(reopened['orders'][0]['name'], 'Second sequence section')
        self.assertEqual(reopened['orders'][0]['annotation'], 'Retained inactive notes')

    def test_native_section_navigation_sentinels_and_shortcut_catalog(self):
        self.import_arrangement_fixture()
        metadata = self.doc()['data']['orderMetadata']
        for index, name in ((1, 'Verse'), (2, 'Break'), (4, 'Outro')):
            self.write('song.annotate', id=metadata[index]['id'], name=name)
        catalog = {item['id']: item for item in self.read('workspace.commands.get')['commands']}
        for identifier in range(557, 561):
            self.assertIn(f'windows.command.{identifier}', catalog)
        self.open_tool('arrangement'); self.order(0)
        before = self.doc()
        self.command(558); self.idle()
        self.assertEqual(self.read('workspace.get')['arrangementSelection']['order'], 1)
        context = self.read('context.get')
        self.press(8019)
        self.assertEqual(self.read('workspace.get')['arrangementSelection']['order'], 2)
        after = self.read('context.get')
        for key in ('pattern', 'row', 'channel', 'column'):
            self.assertEqual(after[key], context[key])
        self.assertFalse(private_desktop.user.IsWindowEnabled(self.control(8010)))
        self.press(8019)
        self.assertEqual(self.read('workspace.get')['arrangementSelection']['order'], 4)
        self.command(558); self.idle()
        self.assertEqual(self.read('workspace.get')['arrangementSelection']['order'], 4)
        self.command(557); self.idle()
        self.assertEqual(self.read('workspace.get')['arrangementSelection']['order'], 2)
        self.press(8018)
        self.assertEqual(self.read('workspace.get')['arrangementSelection']['order'], 1)
        self.command(557); self.idle()
        self.assertEqual(self.read('workspace.get')['arrangementSelection']['order'], 1)
        self.assertEqual(self.doc(), before)
        # The same global command must reveal a distant destination in a compact list.
        for _ in range(30):
            self.write('order.edit', order=len(self.doc()['data']['orders']) - 1, operation='after', pattern=0)
        last = self.doc()['data']['orderMetadata'][-1]['id']
        self.write('song.annotate', id=last, name='Distant ending')
        self.resize_client(760, 600); self.order(4)
        self.command(558); self.idle()
        selected = self.read('workspace.get')['arrangementSelection']['order']
        top = self.desktop.send(self.control(8001), 0x1027)  # LVM_GETTOPINDEX.
        page = self.desktop.send(self.control(8001), 0x1028)  # LVM_GETCOUNTPERPAGE.
        self.assertEqual(selected, len(self.doc()['data']['orders']) - 1)
        self.assertGreater(top, 0)
        self.assertTrue(top <= selected <= top + page, (selected, top, page))

    def test_retained_section_and_pattern_drafts_selection_staleness_and_minimum(self):
        self.write('pattern.create', rows=32)
        self.open_tool('arrangement', 559); self.resize_client(760, 600)
        self.assertEqual(self.state()['page'], 'section')
        self.assertTrue({8001, 8018, 8019, 8020, 8021, 8022, 8023, 8024, 8025}.issubset(self.assert_visible_geometry()))
        section = self.state()['sectionDraft']; native = self.window()
        self.field(8023, 'Unfinished section / 序奏')
        self.focus_control(8023); self.desktop.send(self.control(8023), 0xB1, 2, 9)
        self.command(559); self.idle()
        self.assertEqual(self.desktop.send(self.control(8023), 0xB0), 2 | (9 << 16))
        self.order(1)
        self.assertEqual(self.state()['sectionDraft']['targetID'], section['targetID'])
        self.assertTrue(self.state()['sectionDraft']['selectionDiffers'])
        self.assertEqual(self.text(8023), 'Unfinished section / 序奏')
        self.command(560); self.idle()
        self.assertEqual(self.state()['page'], 'pattern')
        self.assertTrue({8026, 8027, 8028, 8029}.issubset(self.assert_visible_geometry()))
        notes = '\r\n'.join(f'{i:03} — Café / 旋律 🎵' for i in range(80))
        self.field(8026, 'Retained theme'); self.field(8027, notes)
        pattern = self.state()['patternDraft']; edit = self.control(8027)
        self.focus_control(8027); self.desktop.send(edit, 0xB1, 10, 20)
        self.desktop.send(edit, 0xB6, 0, 28)  # Native multiline scroll; no clipboard.
        scroll = self.desktop.send(edit, 0xCE)
        self.command(560); self.idle()
        self.assertEqual(self.desktop.focus(native), edit)
        self.assertEqual(self.desktop.send(edit, 0xB0), 10 | (20 << 16))
        self.assertEqual(self.desktop.send(edit, 0xCE), scroll)
        self.press(8017); self.open_tool('arrangement', 560)
        self.assertEqual(self.window(), native)
        self.assertEqual(self.text(8027), notes)
        self.assertEqual(self.state()['patternDraft']['targetID'], pattern['targetID'])
        self.write('song.annotate', id=section['targetID'], color=0x336699)
        self.wait_workspace(lambda state: state['arrangementWindow']['patternDraft']['stale'], 'Pattern notes did not become stale')
        self.assertEqual(self.text(8027), notes)
        self.assertFalse(private_desktop.user.IsWindowEnabled(self.control(8028)))
        self.press(8029)
        self.assertFalse(self.state()['patternDraft']['stale'])
        self.assertEqual(self.text(8027), '')
        self.field(8026, 'Saved theme'); self.field(8027, notes)
        self.press(8028)
        saved = next(p for p in self.doc()['data']['patterns'] if p['id'] == pattern['targetID'])
        self.assertEqual(saved['name'], 'Saved theme'); self.assertEqual(saved['annotation'], notes)
        self.assertFalse(self.state()['patternDraft']['dirty'])
        self.command(559); self.idle()
        self.assertEqual(self.text(8023), 'Unfinished section / 序奏')
        self.assertTrue(self.state()['sectionDraft']['stale'])
        self.press(8025)
        self.assertEqual(self.state()['sectionDraft']['targetID'], self.read('workspace.get')['arrangementSelection']['id'])
        self.field(8023, 'Second section'); self.press(8024)
        self.assertEqual(self.read('arrangement.get')['sections'][0]['name'], 'Second section')
        self.write('history.undo', domain='document')
        self.assertEqual(self.read('arrangement.get')['sections'], [])
        self.write('history.redo', domain='document')
        self.assertEqual(self.read('arrangement.get')['sections'][0]['name'], 'Second section')

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO') == '1', 'Explicit silent output required')
    def test_annotations_and_annotation_history_keep_advancing_playback(self):
        self.pid, self.client = self.launch(audio=True)
        self.open_tool('arrangement', 559)
        initial = self.read('transport.get')
        self.assertTrue(initial['playing'] and initial['audioActive'])
        epoch = initial['playbackEpoch']; target = self.state()['sectionDraft']['targetID']
        self.field(8023, 'Live section'); self.press(8024)
        operations = [('song.annotate', dict(id=target, name='Live section')),
                      ('history.undo', dict(domain='document')), ('history.redo', dict(domain='document'))]
        for method, params in operations:
            result = self.write(method, **params)
            self.assertFalse(result['playbackStopped'], method)
            transport = self.read('transport.get')
            self.assertTrue(transport['playing'] and transport['audioActive'], transport)
            self.assertEqual(transport['playbackEpoch'], epoch)
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            transport = self.read('transport.get')
            self.assertFalse(transport['fault'], transport)
            if transport['callbacks'] > initial['callbacks'] + 2 and transport['frames'] > initial['frames']:
                break
            time.sleep(.02)
        else:
            self.fail('Audio did not advance through annotation edits and history')
        self.assertEqual(transport['playbackEpoch'], epoch)
        # Musical timing still uses the stop-and-publish path.
        self.write('document.patch', tempo=151)
        self.assertFalse(self.read('transport.get')['playing'])
        self.write('pattern.performance.set', pattern=0, columns=[dict(channel=0, count=2)])
        self.write('transport.play')
        self.assertTrue(self.read('transport.get')['playing'])
        self.write('history.undo', domain='document')
        self.assertFalse(self.read('transport.get')['playing'], 'Musical native history must still stop playback')


class AnnotationControllerTests(unittest.TestCase):
    def test_annotation_catalog_budget_history_and_native_persistence(self):
        executable = Path(os.environ['SCREAMSEQ_TEST_EXE']).with_name('document-controller-tests.exe')
        with tempfile.TemporaryDirectory(prefix='annotation-controller-', dir=os.environ['TMPDIR']) as directory:
            result = subprocess.run([str(executable), '--annotations', directory],
                                    capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for marker in ('PASS annotation catalogs,', 'PASS four-target Unicode cache accounting,',
                       'PASS inactive sequence metadata growth,', 'PASS annotated pattern duplicates',
                       'PASS structural annotation history after independent plugin growth,'):
            self.assertIn(marker, result.stdout)


if __name__ == '__main__':
    unittest.main()
