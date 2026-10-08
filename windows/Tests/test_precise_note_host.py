"""Actual-app acceptance for the independent Precise Notes host.

Uses existing public API and real native controls only.
"""
import unittest

import test_pattern_performance as support
from precise_note_native_support import PreciseNoteNativeMixin, user


class PreciseNoteHostTests(PreciseNoteNativeMixin, unittest.TestCase):
    setUp = support.PatternPerformanceTests.setUp
    doc = support.PatternPerformanceTests.doc
    read = support.PatternPerformanceTests.read
    write = support.PatternPerformanceTests.write
    navigate = support.PatternPerformanceTests.navigate

    def state(self):
        return self.note_idle()

    def seed(self):
        self.write('pattern.notes.set', pattern=0, events=[
            dict(channel=0, position=4 * 65536, note=65),
            dict(channel=1, position=20 * 65536, note=69)])

    def layout(self, name, **fields):
        self.note_idle()
        self.client.call('workspace.layout', dict(name=name, **fields))
        return self.note_idle()

    def retained(self, owner, baseline):
        self.assertEqual(self.note_hwnd(), owner)
        current = self.editor()
        for key in ('document', 'expectedRevision', 'patternID', 'trackID', 'pattern',
                    'row', 'channel', 'generation', 'draftCount', 'selectedEvent',
                    'raw', 'tools', 'retainedDraft'):
            self.assertEqual(current[key], baseline[key], key)
        self.assertNotIn('draft', current)  # Polls expose count/selection, not all hits.

    def test_simultaneous_hosts_compact_focus_and_saved_layout_retain_native_drafts(self):
        self.seed()
        self.write('instrument.create', sample=1)
        self.write('graph.create', name='Precise row beside routing')
        self.resize_main(1440, 852)
        self.main_command(430)
        self.open_row()
        self.native_panel(placement='right', pinned=True, focus=True)
        wide = self.native_panel('instruments', placement='secondary', focus=False)
        self.assertEqual(wide['editorDock']['mode'], 'regions')
        self.assertTrue(wide['editorDock']['trackerVisible'])
        self.assertTrue(wide['graphEditor']['visible'])
        self.assertTrue(wide['preciseNotes']['visible'])
        self.assertTrue(wide['instrumentEnvelope']['visible'])
        hosts = wide['editorDock']['hosts']
        self.assertEqual([hosts[key]['selected'] for key in ('right', 'bottom', 'secondary')],
                         ['preciseNotes', 'graph', 'instruments'])
        self.assertTrue(all(hosts[key]['visible'] for key in hosts))
        bodies = [hosts[key]['body'] for key in ('right', 'bottom', 'secondary')]
        for index, a in enumerate(bodies):
            for b in bodies[index + 1:]:
                self.assertFalse(min(a['x'] + a['width'], b['x'] + b['width']) > max(a['x'], b['x'])
                    and min(a['y'] + a['height'], b['y'] + b['height']) > max(a['y'], b['y']))

        owner = self.note_hwnd()
        offset = self.text(364, '.')  # Invalid draft must survive all presentation changes.
        repeat = self.text(376, '3.')
        self.desktop.send(repeat, 0xB1, 1, 2)
        selection = self.desktop.send(repeat, 0xB0)
        baseline, song = self.editor(), self.doc()
        self.assertTrue(baseline['retainedDraft'])
        saved = self.layout('Save custom', savedName='Precise and routing')
        self.assertEqual(saved['editorDock']['configuration']['version'], 4)
        self.assertEqual(set(saved['editorDock']['configuration']['locations']),
                         {'automation', 'instruments', 'graphCurve', 'preciseNotes'})
        # Observe actual field focus immediately after a width-driven collapse.
        compact = self.resize_main(1000, 720)
        self.assertEqual(compact['editorDock']['mode'], 'tabs')
        self.assertEqual(compact['editorDock']['compactSelection'], 'preciseNotes')
        self.assertEqual(self.desktop.focus(owner), repeat)
        self.assertEqual(self.desktop.send(repeat, 0xB0), selection)
        compact = self.native_panel('instruments', placement='secondary', focus=False)
        self.assertEqual(compact['editorDock']['compactSelection'], 'preciseNotes')
        self.assertEqual(self.desktop.focus(owner), repeat)
        self.assertEqual(self.desktop.send(repeat, 0xB0), selection)
        self.retained(owner, baseline)
        self.assertEqual(self.doc(), song)

        self.resize_main(1440, 852)
        for destination in ('bottom', 'secondary', 'float', 'hide', 'right'):
            with self.subTest(placement=destination):
                state = self.native_panel(placement=destination, focus=destination != 'hide')
                self.assertEqual(state['locations']['preciseNotes'], destination)
                self.assertEqual(bool(user.IsWindowVisible(owner)), destination != 'hide')
                self.retained(owner, baseline)
                self.assertEqual(self.field_text(offset), '.')
                self.assertEqual(self.field_text(repeat), '3.')
                self.assertEqual(self.desktop.send(repeat, 0xB0), selection)
                self.assertEqual(self.doc(), song)
        self.navigate(row=28, channel=2, following=False)
        before = self.state()
        cursor = self.read('context.get')
        restored = self.layout('Restore custom', savedName='Precise and routing')
        self.assertEqual(restored['editorDock']['configuration'], saved['editorDock']['configuration'])
        self.assertEqual(restored['pins'], before['pins'])
        self.assertEqual(restored['returnPoints'], before['returnPoints'])
        self.assertEqual(self.read('context.get'), cursor)
        self.retained(owner, baseline)
        self.assertEqual(self.doc(), song)

    def test_legacy_inspector_and_precise_owner_pin_follow_and_return_are_independent(self):
        self.seed()
        self.resize_main(1440, 852)
        self.navigate(row=12, channel=1, following=False)
        inspector = self.native_panel('notes', focus=True, pinned=True)['inspection']['notes']
        self.open_row(row=4, channel=0)
        self.native_panel(placement='right', pinned=True, focus=True)
        owner = self.note_hwnd()
        self.text(363, '92')
        baseline, song = self.editor(), self.doc()
        self.navigate(row=20, channel=1, following=False)
        state = self.state()
        self.assertEqual(state['inspection']['notes'], inspector)
        self.assertEqual((state['preciseNotes']['row'], state['preciseNotes']['channel']), (4, 0))
        self.assertTrue(state['pins']['notes'] and state['pins']['preciseNotes'])
        state = self.native_panel('notes', follow=True)
        self.assertEqual((state['inspection']['notes']['row'], state['inspection']['notes']['channel']), (20, 1))
        self.assertFalse(state['pins']['notes'])
        self.assertTrue(state['pins']['preciseNotes'])
        self.retained(owner, baseline)
        state = self.native_panel(follow=True)  # Dirty native Follow retains its captured row.
        self.assertFalse(state['pins']['preciseNotes'])
        self.retained(owner, baseline)
        self.command(9203)
        self.assertEqual((self.editor()['row'], self.editor()['channel']), (4, 0))
        self.assertEqual(self.editor()['selectedEvent']['velocity'], 127)
        self.native_panel(follow=True)
        self.assertEqual((self.editor()['row'], self.editor()['channel']), (20, 1))
        self.resize_main(1000, 720)
        self.command(9204)  # Tools / Return resolves the owner's opening row/track.
        context = self.read('context.get')
        self.assertEqual((context['pattern'], context['row'], context['channel']), (0, 4, 0))
        state = self.state()
        self.assertTrue(state['editorDock']['trackerVisible'])
        self.assertEqual(state['focus'], 'pattern')
        self.assertEqual(self.desktop.focus(self.desktop.hwnd(self.pid)), self.desktop.hwnd(self.pid))
        self.assertEqual(self.note_hwnd(), owner)
        self.assertEqual(self.doc(), song)

    def test_entry_aliases_reuse_one_owner_and_leave_legacy_inspector_api_distinct(self):
        self.seed()
        self.assertEqual(self.note_windows(), [])
        self.navigate(row=12, channel=1)
        self.main_command(569)  # Existing read-only Notes inspector, no new editor.
        self.assertEqual(self.note_windows(), [])
        self.native_panel('notes', pinned=True)
        inspector = self.state()['inspection']['notes']
        self.open_row(row=4, channel=0)
        owner = self.note_hwnd()
        self.text(364, '.')
        baseline, song = self.editor(), self.doc()
        for identifier in (520, 567, 568, 107):
            with self.subTest(entry=identifier):
                self.main_command(identifier)
                self.retained(owner, baseline)
                state = self.state()
                self.assertEqual(state['noteEditor'], state['preciseNotes'])
                self.assertEqual(state['inspection']['notes'], inspector)
                self.assertTrue(state['pins']['notes'])
                self.assertEqual(self.doc(), song)
        for identifier in range(360, 378):
            self.assertFalse(user.GetDlgItem(self.desktop.hwnd(self.pid), identifier), identifier)
            self.assertTrue(user.GetDlgItem(owner, identifier), identifier)
        with self.assertRaises(support.ApiError) as rejected:
            self.client.call('workspace.panel', dict(panel='notes', placement='bottom'))
        self.assertEqual(rejected.exception.code, -32602)
        self.retained(owner, baseline)
        self.assertEqual(self.doc(), song)


if __name__ == '__main__':
    unittest.main()
